// THE MIX MUST STAY INSIDE THE OUTPUT VOLTAGE RANGE, for every shape, at every
// voice count, whatever the panel asks for. voice.h hands each voice scale_,
// and scale_ is what one of them may put on the output.
//
// This is the contract nothing enforced. Each voice is given a share of the
// range and every shape ends in a clip, so the arithmetic works out ONLY while
// each shape stays inside its share -- and the mix accumulator is an int16
// carrying a uint16 DAC code, so a shape that does not is not clipped, it
// WRAPS: the code passes 65535 and the output jumps from -5 V to +7 V. Six
// shapes broke the contract silently the moment the range doubled (5f739a20),
// because the shapes the gain envelope EXCITES turn the chiff's overdrive into
// output past their share.
//
// So the check is at the DAC, on the real path: Voice::NoteOn through
// CVOutput::RenderSamples into the stub, reading the codes the hardware would
// have been handed. A wrap lands far outside the window on the opposite side,
// so one bounds check catches both a wrap and a plain overshoot.
//
// WHAT IT CANNOT DO: bound a NOISE peak. Those grow as sqrt(2 ln N) with how
// long you listen, so a passing run says the shape is inside its share over
// THIS run, not for ever. WHISTLE is the one shape whose bound is arithmetic
// rather than statistical -- it caps bp against its share in the render -- and
// that is why the cap stays.
//
// Usage: ./mixtest [verbose]
#define TEST 1
#define private public
#include "yarns/voice.h"
#include "tools/cvtest/dac_stub.h"
#include "tools/panel_chain.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace stmlib { uint32_t Random::rng_state_ = 0x21; }

using namespace yarns;
using namespace stmlib;

namespace {

const uint8_t kMaxVoices = 4;
const uint8_t kChannel = 0;

Voice voices[kMaxVoices];
CVOutput audio_output;
ADSR adsr;

// The corners a shape can be driven into. Pitch and TIMBRE span their settings;
// the chiff is swept because it is the exciter's overdrive, and it is what six
// shapes turned into output past their share.
const int kPitches[] = { 24, 48, 60, 84, 108 };
const int kTimbres[] = { 0, 8192, 16384, 32767 };
const int kChiffAmounts[] = { 0, 64, 127 };
// Long enough for the strike and the ring after it; the shapes that break the
// contract do it at the onset.
const int kBlocks = 45000 / 4 / kAudioBlockSize;

struct Worst {
  int32_t excursion;
  int shape, pitch, timbre, chiff, voices;
};

int32_t Excursion(int16_t sample, uint16_t zero_code) {
  // The buffer holds a uint16 DAC code; the excursion is its distance from the
  // 0 V code, in codes. Signed 16-bit arithmetic on the difference is exact for
  // anything the DAC can carry.
  return static_cast<int16_t>(static_cast<uint16_t>(sample) - zero_code);
}

void RunCase(int shape, int num_voices, int pitch, int timbre, int chiff,
             Worst* worst) {
  Random::Seed(0x21);
  for (uint8_t v = 0; v < kMaxVoices; ++v) voices[v].Init();
  audio_output.Init(true);
  for (uint8_t v = 0; v < num_voices; ++v) {
    voices[v].set_oscillator_mode(OSCILLATOR_MODE_ENVELOPED);
    voices[v].set_oscillator_shape(static_cast<uint8_t>(shape));
  }
  audio_output.AssignVoices(&voices[0], DC_PITCH,
                            num_voices, num_voices);

  adsr.peak_u16 = UINT16_MAX;
  adsr.sustain_u16 = static_cast<uint16_t>(65535L * 60 / 100);
  adsr.attack_u32 = PanelStageIncrement(0, 0, 0);
  adsr.decay_u32 = PanelStageIncrement(64, 0, 0);
  adsr.release_u32 = PanelStageIncrement(64, 0, 0);
  const uint32_t chiff_amount_q30 = PanelChiffAmount_q30(chiff, 0, 0);
  const uint32_t chiff_audible_samples = PanelChiffAudibleSamples(67, 0, 0);

  // A chord rather than a unison, so the voices are not one waveform times n.
  const int kIntervals[] = { 0, 7, 12, 16 };
  for (uint8_t v = 0; v < num_voices; ++v) {
    voices[v].NoteOn(static_cast<int16_t>((pitch + kIntervals[v]) << 7), 100,
                     0, 0, true, adsr, static_cast<int16_t>(timbre),
                     chiff_amount_q30, chiff_audible_samples);
  }

  const uint16_t zero_code = audio_output.zero_dac_code_;
  for (int block = 0; block < kBlocks; ++block) {
    for (uint8_t v = 0; v < num_voices; ++v) voices[v].Refresh();
    audio_output.RenderSamples(0, kChannel, 0);
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      int32_t e = Excursion(g_dac_block[kChannel][i], zero_code);
      if (e < 0) e = -e;
      if (e > worst->excursion) {
        worst->excursion = e;
        worst->shape = shape; worst->pitch = pitch; worst->timbre = timbre;
        worst->chiff = chiff; worst->voices = num_voices;
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  const bool verbose = argc > 1 && !strcmp(argv[1], "verbose");

  // THE ALLOWANCE IS READ BACK FROM voice.h, not restated here: one voice is
  // given the whole of it, and scale_ is what its envelope peaks at.
  // Stating that twice is how the check and the thing checked drift apart.
  voices[0].Init();
  audio_output.Init(true);
  audio_output.AssignVoices(&voices[0], DC_PITCH, 1, 1);
  const int32_t allowance = voices[0].oscillator()->scale_;

  int failures = 0;
  for (int shape = 0; shape <= OSC_SHAPE_FM; ++shape) {
    Worst worst; memset(&worst, 0, sizeof(worst));
    for (int n = 1; n <= kMaxVoices; ++n) {
      for (size_t p = 0; p < sizeof(kPitches)/sizeof(kPitches[0]); ++p) {
        for (size_t t = 0; t < sizeof(kTimbres)/sizeof(kTimbres[0]); ++t) {
          for (size_t c = 0; c < sizeof(kChiffAmounts)/sizeof(kChiffAmounts[0]); ++c) {
            RunCase(shape, n, kPitches[p], kTimbres[t], kChiffAmounts[c], &worst);
          }
        }
      }
    }
    const bool over = worst.excursion > allowance;
    if (over) ++failures;
    if (over || verbose) {
      printf("%s shape %2d  worst %6d of %d (%.2f)  "
             "MIDI %d, TIMBRE %d, EXCITER %d, %d voice(s)\n",
             over ? "FAIL" : "    ", shape, worst.excursion, allowance,
             worst.excursion / (double) allowance,
             worst.pitch, worst.timbre, worst.chiff, worst.voices);
    }
  }
  if (failures) {
    printf("\n%d shape(s) leave the output range the voices were given. Past it\n"
           "the DAC code wraps and the output inverts; there is no clipping in\n"
           "between.\n", failures);
    return 1;
  }
  printf("PASS %d shapes stay inside the output range at 1..%d voices "
         "(%lu cases each)\n",
         OSC_SHAPE_FM + 1, kMaxVoices,
         (unsigned long) (kMaxVoices * 5 * 4 * 3));
  return 0;
}
