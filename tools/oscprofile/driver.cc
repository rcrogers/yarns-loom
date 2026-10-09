// Drives every oscillator shape through a grid of notes under QEMU, so
// tools/osc_profile.py can price the instructions each audio block executes.
// Linked against the firmware's own objects (build/yarns/*.o), so the code
// measured is the code that ships.
//
// Each render call is bracketed by ProfileBlockBegin / ProfileBlockEnd; the
// trace is cut at those two addresses. Each case is announced on the output
// file before its blocks render: `case <shape> <pitch> <sweep> <gain> <blocks>`.
#include "yarns/oscillator.h"
#include "yarns/drivers/dac.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace yarns;

extern "C" {
__attribute__((noinline)) void ProfileBlockBegin() { __asm__ volatile(""); }
__attribute__((noinline)) void ProfileBlockEnd() { __asm__ volatile(""); }
}

namespace {

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

}  // namespace

// `shape=<n>` profiles that shape alone, so shapes can run in parallel.
int main(int argc, char** argv) {
  int only = -1;
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], "shape=", 6)) only = atoi(argv[i] + 6);
  }
  for (int shape = 0; shape <= OSC_SHAPE_FM; ++shape) {
    if (only >= 0 && shape != only) continue;
    for (int sweep = 0; sweep < kNumSweeps; ++sweep) {
      for (int gain = 0; gain < kNumGains; ++gain) {
        for (size_t p = 0; p < sizeof(kPitches) / sizeof(kPitches[0]); ++p) {
          RenderCase(shape, kPitches[p], sweep, gain);
        }
      }
    }
  }
  return 0;
}
