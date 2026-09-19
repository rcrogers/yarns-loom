// THE WARP ON ITS OWN, because it is where the faults have been. Three so far:
// WHISTLE's shift underflowed for a negative timbre and answered a filter with
// no loss in it (15dd7115); the NOISE branch let the cutoff go negative and
// CutoffFromFreq shifted it into its table index; the soft-knee branch returned
// a negative unchanged and the transfer render shifted it as an unsigned.
//
// Each was found by a different check, downstream, by accident. What the warp
// owes its callers can be said directly, so this says it.
//
// A WARP IS AN ABSOLUTE-POSITION MAP -- a cutoff, a damp, a width, a ratio --
// from the timbre the panel and its modulation produce onto the parameter a
// render consumes. Two properties follow, and both are checked here for every
// shape at every pitch that changes the answer:
//
//   MONOTONE over the WHOLE signed input range. A control moves one way, and
//   that one property catches every fault this file was written for:
//     - a wrap REVERSES the map, which is what it is.
//     - a clamp goes FLAT, which is monotone, and is the fix.
//     - a map that simply continues below zero stays monotone, and is fine --
//       the render gets a smaller parameter, not a wrong one.
//   Asking instead that a negative stay inside the range a knob can reach is
//   too strict: LP PULSE's cutoff continues below its knob's bottom and is
//   right to.
//
// A shape whose warp is the IDENTITY has no map to check. The raw value reaches
// its render, and osctest's `negative` mode is what holds that end.

#define TEST 1
#define private public
#include "yarns/oscillator.h"
#include "yarns/drivers/dac.h"
#include <cstdio>
#include <cstdlib>
using namespace yarns;

namespace {

// Pitch changes the answer for the shapes whose map tracks it, so every claim
// is made at each of these.
const int kPitches[] = { 0, 24 << 7, 60 << 7, 96 << 7, (128 << 7) - 1 };
// The control's own range, at a stride that still catches a one-count reversal
// where the map is steep.
const int kStride = 7;

Oscillator osc;

}  // namespace

int main() {
  int failures = 0;
  for (int s = 0; s <= OSC_SHAPE_FM; ++s) {
    const OscillatorShape shape = static_cast<OscillatorShape>(s);
    // WarpTimbre reads no scale, so what Init is handed cannot reach it.
    osc.Init(0, 0);
    osc.set_shape(shape);
    for (size_t p = 0; p < sizeof(kPitches) / sizeof(kPitches[0]); ++p) {
      const int16_t pitch = static_cast<int16_t>(kPitches[p]);

      // Identity is no map: osctest's `negative` mode owns those shapes.
      bool identity = true;
      for (int32_t t = -32768; t < 0; t += 1024) {
        if (osc.WarpTimbre(static_cast<int16_t>(t), shape, pitch) != t) {
          identity = false;
          break;
        }
      }
      if (identity) continue;

      int32_t rises = 0, falls = 0;
      int32_t worst_step = 0, worst_at = 0;
      int32_t previous = osc.WarpTimbre(-32768, shape, pitch);
      for (int32_t t = -32768 + kStride; t <= 32767; t += kStride) {
        const int32_t w = osc.WarpTimbre(static_cast<int16_t>(t), shape, pitch);
        const int32_t step = w - previous;
        if (step > 0) ++rises;
        if (step < 0) ++falls;
        previous = w;
      }
      // Monotone means one direction never happens. The smaller count is how
      // much of the sweep went backwards.
      const int32_t reversals = rises < falls ? rises : falls;
      if (reversals > 0) {
        // Name the largest single reversal, which is where the wrap is.
        previous = osc.WarpTimbre(-32768, shape, pitch);
        const bool rising = falls < rises;
        for (int32_t t = -32768 + kStride; t <= 32767; t += kStride) {
          const int32_t w = osc.WarpTimbre(static_cast<int16_t>(t), shape, pitch);
          const int32_t step = rising ? previous - w : w - previous;
          if (step > worst_step) { worst_step = step; worst_at = t; }
          previous = w;
        }
        printf("FAIL shape %2d at MIDI %3d: the map reverses on %d of %d steps, "
               "worst %d counts at timbre %d\n",
               s, kPitches[p] >> 7, reversals, rises + falls, worst_step, worst_at);
        ++failures;
      }
    }
  }
  if (failures) {
    printf("\n%d warp(s) reverse. A control that goes backwards is a value that\n"
           "wrapped, and the render is handed a parameter no setting asked for.\n",
           failures);
    return 1;
  }
  printf("PASS %d shapes: every warp is monotone across the whole signed range\n",
         OSC_SHAPE_FM + 1);
  return 0;
}
