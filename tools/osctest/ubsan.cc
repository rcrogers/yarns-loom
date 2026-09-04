// EVERY SHAPE THROUGH THE UNDEFINED-BEHAVIOUR SANITISER, over the parameter
// space the firmware can actually reach: pitch across the keyboard, timbre
// across its signed range (a negative TIMBRE MOD ENVELOPE reaches below zero),
// gain from silence to full, and the shape's own state carried between blocks.
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
  osc.Init(51330, 51330);
  const int pitches[] = { 0, 24 << 7, 60 << 7, 108 << 7, (128 << 7) - 1 };
  // The values a shape's render can actually be handed: WarpTimbre's output,
  // over the whole signed range its own caller can reach. Feeding a render a
  // raw value its warp would never produce tests something the firmware cannot
  // do, and every such "finding" is noise.
  const int warp_inputs[] = { -32768, -16384, -1, 0, 1, 8192, 16384, 32767 };
  const int gains[] = { 0, 1, 16384, 32767 };
  long cases = 0;
  for (int s = 0; s <= OSC_SHAPE_FM; ++s) {
    for (size_t p = 0; p < sizeof(pitches)/sizeof(pitches[0]); ++p) {
      for (size_t t = 0; t < sizeof(warp_inputs)/sizeof(warp_inputs[0]); ++t) {
          const int16_t timbre = osc.WarpTimbre(
              static_cast<int16_t>(warp_inputs[t]),
              static_cast<OscillatorShape>(s),
              static_cast<int16_t>(pitches[p]));
        for (size_t g = 0; g < sizeof(gains)/sizeof(gains[0]); ++g) {
          stmlib::Random::Seed(0x21);
          osc.Init(51330, 51330);
          osc.set_shape(static_cast<OscillatorShape>(s));
          osc.Refresh(static_cast<int16_t>(pitches[p]), 0, 0);
          // Several blocks, so state carried between them is exercised too.
          for (int b = 0; b < 6; ++b) {
            int16_t timbre_gain[2 * kAudioBlockSize];
            int16_t mix[kAudioBlockSize];
            memset(mix, 0, sizeof(mix));
            for (size_t i = 0; i < kAudioBlockSize; ++i) {
              timbre_gain[i] = timbre;
              timbre_gain[i + kAudioBlockSize] = static_cast<int16_t>(gains[g]);
            }
            (osc.*Oscillator::fn_table_[s])(timbre_gain, mix);
          }
          ++cases;

        }
      }
    }
  }
  printf("%ld cases clean\n", cases);
  return 0;
}
