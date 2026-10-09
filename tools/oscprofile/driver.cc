// Drives every oscillator shape, or the envelope, through a grid under QEMU,
// so tools/osc_profile.py and tools/env_profile.py can price the instructions
// each call executes. Linked against the firmware's own objects
// (build/yarns/*.o), so the code measured is the code that ships.
//
// Each measured call is bracketed by ProfileBlockBegin / ProfileBlockEnd; the
// trace is cut at those two addresses. Each case is announced on the output
// file before its calls run:
//   case <shape> <pitch> <sweep> <gain> <blocks>
//   env <target> <bias> <attack> <decay> <sustain> <release> <peak> <amount>
//       <duration> <calls>
#include "yarns/oscillator.h"
#include "yarns/envelope.h"
#include "yarns/drivers/dac.h"
#include "tools/panel_chain.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace yarns;

extern "C" {
__attribute__((noinline)) void ProfileBlockBegin() { __asm__ volatile(""); }
__attribute__((noinline)) void ProfileBlockEnd() { __asm__ volatile(""); }
}

namespace {

#define COUNT(array) (sizeof(array) / sizeof(array[0]))

Oscillator osc;

const int kBlocks = 8;
// Both ends of the keyboard, middle C, and closer steps toward the top, where
// edges come fastest. Against every fourth semitone, these eight found every
// shape's dearest block.
const int16_t kPitches[] = {
  0 << 7, 36 << 7, 60 << 7, 84 << 7, 100 << 7, 112 << 7, 124 << 7,
  kHighestNote - 1,
};

// What TIMBRE does across a case, as the knob plus the envelope hand it to the
// shape: raw 0..full scale, warped by the shape's own map.
enum Sweep { kRising, kFalling, kHeldLow, kHeldHigh, kNumSweeps };
// What the gain envelope does across a case.
enum Gain { kGainFull, kGainRising, kGainFalling, kNumGains };

int16_t Ramp(int step, int steps) {
  return static_cast<int16_t>(static_cast<int32_t>(INT16_MAX) * step / steps);
}

int16_t RawTimbre(int sweep, int step, int steps) {
  switch (sweep) {
    case kRising: return Ramp(step, steps);
    case kFalling: return INT16_MAX - Ramp(step, steps);
    case kHeldLow: return INT16_MAX / 8;
    default: return INT16_MAX;
  }
}

int16_t GainAt(int gain, int step, int steps) {
  switch (gain) {
    case kGainRising: return Ramp(step, steps);
    case kGainFalling: return INT16_MAX - Ramp(step, steps);
    default: return INT16_MAX;
  }
}

void RenderCase(int shape, int16_t pitch, int sweep, int gain) {
  printf("case %d %d %d %d %d\n", shape, pitch, sweep, gain, kBlocks);
  osc.Init(INT16_MAX >> 1, 1 << 14);
  osc.set_shape(static_cast<OscillatorShape>(shape));
  const int steps = kBlocks * kAudioBlockSize;
  for (int b = 0; b < kBlocks; ++b) {
    osc.Refresh(pitch, 0, 0, 0);
    int16_t timbre_gain[2 * kAudioBlockSize];
    int16_t mix[kAudioBlockSize] = { 0 };
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      const int step = b * kAudioBlockSize + i;
      timbre_gain[i] = osc.WarpTimbre(RawTimbre(sweep, step, steps),
                                      static_cast<OscillatorShape>(shape));
      timbre_gain[i + kAudioBlockSize] = GainAt(gain, step, steps);
    }
    ProfileBlockBegin();
    (osc.*Oscillator::fn_table_[shape])(timbre_gain, mix);
    ProfileBlockEnd();
  }
}

Envelope envelope;
// The envelope keeps a pointer to its ADSR, as Voice keeps its own.
ADSR adsr;

// What an envelope drives, as its callers set it: Oscillator::NoteOn's gain
// envelope at one voice's peak and its TIMBRE envelope either way, and
// CVOutput's, whose DAC codes fall as its volts rise.
enum Target { kGain, kTimbreUp, kTimbreDown, kCv, kNumTargets };
struct Range { int32_t min_s16, max_s16, ceiling_s16; };
const Range kRanges[kNumTargets] = {
  { 0, INT16_MAX >> 1, INT16_MAX >> 1 },
  { 0, 12000, kEnvelopeSampleMax },
  { 0, -12000, kEnvelopeSampleMax },
  { 27000, 2500, kEnvelopeSampleMax },
};
// The bias held, or stepped the most it can between blocks.
enum Bias { kBiasHeld, kBiasStepping, kNumBiases };
const int32_t kBiasStep_q31 = 8000 << 16;
// Stage settings from the fastest, which hand off inside a block, to the
// slowest; a peak from the panel or equal to the sustain, which makes Trigger
// chain; EXCITER AMOUNT and DURATION from none to the most.
const int kAttacks[] = { 0, 8, 32, 127 };
const int kDecays[] = { 0, 24, 127 };
const int kSustains[] = { 0, 64, 127 };
const int kReleases[] = { 0, 24, 127 };
enum Peak { kPeakPanel, kPeakAtSustain, kNumPeaks };
const int kChiffs[][2] = {
  { 0, 0 }, { 16, 40 }, { 64, 80 }, { 127, 127 }, { 127, 0 },
};
// A case's calls: NoteOn, held blocks, a legato NoteOn, held blocks, NoteOff,
// release blocks, a NoteOn while releasing, and the blocks after it.
const int kHeldBlocks = 6;
const int kReleaseBlocks = 8;
const int kRetriggerBlocks = 3;
const int kEnvelopeCalls =
    1 + kHeldBlocks + 1 + kHeldBlocks + 1 + kReleaseBlocks + 1 + kRetriggerBlocks;

void RenderEnvelopeBlocks(int blocks, int bias, int* block) {
  int16_t samples[kAudioBlockSize];
  for (int b = 0; b < blocks; ++b, ++*block) {
    const int32_t bias_q31 = bias == kBiasHeld ? 0
        : (*block & 1) ? kBiasStep_q31 : -kBiasStep_q31;
    ProfileBlockBegin();
    envelope.RenderSamples(samples, bias_q31);
    ProfileBlockEnd();
  }
}

void NoteOn(const Range& range, uint32_t amount_q30, uint32_t audible_samples) {
  ProfileBlockBegin();
  envelope.NoteOn(adsr, range.min_s16, range.max_s16, range.ceiling_s16,
                  amount_q30, audible_samples);
  ProfileBlockEnd();
}

void EnvelopeCase(int target, int bias, int attack, int decay, int sustain,
                  int release, int peak, int amount, int duration) {
  printf("env %d %d %d %d %d %d %d %d %d %d\n", target, bias, attack, decay,
         sustain, release, peak, amount, duration, kEnvelopeCalls);
  PanelAdsr(&adsr, attack, decay, sustain, release, 0, 127, 0, 0, 0, 0);
  if (peak == kPeakAtSustain) adsr.peak_u16 = adsr.sustain_u16;
  const uint32_t amount_q30 = PanelChiffAmount_q30(amount, 0, 127);
  const uint32_t audible_samples = PanelChiffAudibleSamples(duration, 0, 127);
  const Range& range = kRanges[target];
  envelope.Init(0);
  int block = 0;
  NoteOn(range, amount_q30, audible_samples);
  RenderEnvelopeBlocks(kHeldBlocks, bias, &block);
  NoteOn(range, amount_q30, audible_samples);
  RenderEnvelopeBlocks(kHeldBlocks, bias, &block);
  ProfileBlockBegin();
  envelope.NoteOff();
  ProfileBlockEnd();
  RenderEnvelopeBlocks(kReleaseBlocks, bias, &block);
  NoteOn(range, amount_q30, audible_samples);
  RenderEnvelopeBlocks(kRetriggerBlocks, bias, &block);
}

// Every envelope case whose index is `part` modulo `parts`.
void EnvelopeCases(int part, int parts) {
  int index = 0;
  for (int target = 0; target < kNumTargets; ++target)
  for (int bias = 0; bias < kNumBiases; ++bias)
  for (size_t a = 0; a < COUNT(kAttacks); ++a)
  for (size_t d = 0; d < COUNT(kDecays); ++d)
  for (size_t s = 0; s < COUNT(kSustains); ++s)
  for (size_t r = 0; r < COUNT(kReleases); ++r)
  for (int peak = 0; peak < kNumPeaks; ++peak)
  for (size_t c = 0; c < COUNT(kChiffs); ++c) {
    if (index++ % parts != part) continue;
    EnvelopeCase(target, bias, kAttacks[a], kDecays[d], kSustains[s],
                 kReleases[r], peak, kChiffs[c][0], kChiffs[c][1]);
  }
}

int Option(int argc, char** argv, const char* key, int fallback) {
  const size_t length = strlen(key);
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], key, length) && argv[i][length] == '=') {
      return atoi(argv[i] + length + 1);
    }
  }
  return fallback;
}

}  // namespace

// `shape=<n>` profiles that shape alone; `env part=<k> parts=<n>` the envelope
// cases whose index is k modulo n. Either way, runs split to go in parallel.
int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "env")) {
      EnvelopeCases(Option(argc, argv, "part", 0), Option(argc, argv, "parts", 1));
      return 0;
    }
  }
  const int only = Option(argc, argv, "shape", -1);
  for (int shape = 0; shape <= OSC_SHAPE_FM; ++shape) {
    if (only >= 0 && shape != only) continue;
    for (int sweep = 0; sweep < kNumSweeps; ++sweep) {
      for (int gain = 0; gain < kNumGains; ++gain) {
        for (size_t p = 0; p < COUNT(kPitches); ++p) {
          RenderCase(shape, kPitches[p], sweep, gain);
        }
      }
    }
  }
  return 0;
}
