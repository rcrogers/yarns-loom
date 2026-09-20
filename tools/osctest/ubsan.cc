// EVERY SHAPE THROUGH THE UNDEFINED-BEHAVIOUR SANITISER, over the parameter
// space the firmware can actually reach: pitch across the keyboard, timbre
// across its signed range (a negative TIMBRE MOD ENVELOPE reaches below zero),
// gain from silence to full, every voice count, and the shape's own state
// carried between blocks.
//
// THE TIMBRE MOVES BETWEEN BLOCKS AND WITHIN THEM. A shape may derive a
// per-block scale from it and hold state in units of that scale, so a constant
// timbre exercises neither the derivation changing nor the state being carried
// across the change -- which is where WHISTLE overflowed int32 undetected.
#define TEST 1
#define private public
#include "yarns/oscillator.h"
#include "yarns/drivers/dac.h"
#include "stmlib/utils/random.h"
#include <cstdio>
#include <cstring>
using namespace yarns;
int main() {
  Oscillator osc;
  // The two amplitudes a voice is handed at one through four voices, derived
  // as yarns/voice.h derives them: the span's amplitude over n where the
  // voices add coherently, and the geometric mean of the whole and that where
  // they add in power. tools/cvtest span prints the firmware's own.
  const uint16_t kSpanAmplitudeCodes_u16 = 25665; // the output span's +/-5 V
  uint16_t scales[4][2];
  for (uint8_t n = 1; n <= 4; ++n) {
    scales[n - 1][0] = kSpanAmplitudeCodes_u16 / n;
    scales[n - 1][1] = static_cast<uint16_t>(IntegerSqrt(
        static_cast<uint32_t>(kSpanAmplitudeCodes_u16) * scales[n - 1][0]));
  }
  osc.Init(scales[0][0], scales[0][1]);
  const int pitches[] = { 0, 24 << 7, 60 << 7, 108 << 7, (128 << 7) - 1 };
  // The values a shape's render can actually be handed: WarpTimbre's output,
  // over the whole signed range its own caller can reach. Feeding a render a
  // raw value its warp would never produce tests something the firmware cannot
  // do, and every such "finding" is noise.
  const int warp_inputs[] = { -32768, -16384, -1, 0, 1, 8192, 16384, 32767 };
  const int gains[] = { 0, 1, 16384, 32767 };
  // Every allocation: scale_ divides into what a shape derives per block, so
  // one voice count tests one set of derived values. voice.h's pairing.
  long cases = 0;
  // EVERY SHAPE, the runs past OSC_SHAPE_FM included: those are where the
  // ratio tables and the signed edge arithmetic live, and the sweep stopped
  // at the first of them.
  for (int s = 0; s < kOscShapeLast; ++s) {
    for (size_t p = 0; p < sizeof(pitches)/sizeof(pitches[0]); ++p) {
      for (size_t t = 0; t < sizeof(warp_inputs)/sizeof(warp_inputs[0]); ++t) {
          const int16_t timbre = osc.WarpTimbre(
              static_cast<int16_t>(warp_inputs[t]),
              static_cast<OscillatorShape>(s),
              static_cast<int16_t>(pitches[p]));
          // The far end of the same warp, so the sweep below crosses the whole
          // map and any per-block derivation crosses its whole range with it.
          const int16_t timbre_far = osc.WarpTimbre(
              static_cast<int16_t>(warp_inputs[t] < 0 ? 32767 : -32768),
              static_cast<OscillatorShape>(s),
              static_cast<int16_t>(pitches[p]));
        for (size_t g = 0; g < sizeof(gains)/sizeof(gains[0]); ++g)
        for (size_t v = 0; v < sizeof(scales)/sizeof(scales[0]); ++v) {
          stmlib::Random::Seed(0x21);
          osc.Init(scales[v][0], scales[v][1]);
          osc.set_shape(static_cast<OscillatorShape>(s));
          osc.Refresh(static_cast<int16_t>(pitches[p]), 0, 0);
          // Start the filter at its rail. From rest a resonator needs hundreds
          // of ms to reach it and this runs for eight, so a sweep that starts
          // at zero only ever tests a quiet state -- and anything a shape
          // derives from that state is then tested at one end of its range.
          osc.svf_.bp = INT16_MAX;
          osc.svf_.lp = INT16_MAX;
          osc.svf_.notch = INT16_MAX;
          osc.svf_.hp = INT16_MAX;
          // Several blocks, so state carried between them is exercised too.
          for (int b = 0; b < 6; ++b) {
            int16_t timbre_gain[2 * kAudioBlockSize];
            int16_t mix[kAudioBlockSize];
            memset(mix, 0, sizeof(mix));
            // Alternate ends block to block, and ramp between them within a
            // block: the first moves anything derived per block, the second
            // moves what the loop reads per sample.
            const int16_t from = (b & 1) ? timbre_far : timbre;
            const int16_t to = (b & 1) ? timbre : timbre_far;
            for (size_t i = 0; i < kAudioBlockSize; ++i) {
              // Signed divisor: kAudioBlockSize is size_t, and `to` is below
              // `from` on every other block, so an unsigned divisor turned
              // half these ramps into noise -- the values the sweep is here
              // to walk were never walked.
              timbre_gain[i] = static_cast<int16_t>(
                  from + (to - from) * static_cast<int>(i) /
                      static_cast<int>(kAudioBlockSize));
              timbre_gain[i + kAudioBlockSize] = static_cast<int16_t>(gains[g]);
            }
            (osc.*Oscillator::render_fn(s))(timbre_gain, mix);
          }
          ++cases;

        }
      }
    }
  }
  printf("%ld cases clean\n", cases);
  return 0;
}
