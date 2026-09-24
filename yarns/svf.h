// Copyright 2013 Emilie Gillet.
// Copyright 2025 Chris Rogers.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Chamberlin state variable filter.
//
// cutoff is Q0.15 (range [0, 1.0), encodes 2*sin(pi*fc/fs)).
// damp is Q1.14 (range [0, 2.0), encodes 2*(1-resonance)).

#ifndef YARNS_SVF_H_
#define YARNS_SVF_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/dsp.h"

#include "yarns/resources.h"

using namespace stmlib;

namespace yarns {

// Which of the four outputs a caller takes. notch and hp are formed and
// consumed inside one call -- notch feeds hp, hp feeds the band-pass step -- so
// neither is state, and naming the output is what lets the one asked for be
// returned rather than stored.
enum SvfOutput { SVF_LP, SVF_BP, SVF_NOTCH, SVF_HP };

struct SVF {
  int32_t bp, lp;
  // The remainder of the shift that puts each wide product back in the state's
  // units -- what integer division by 2^14 or 2^15 leaves behind. Named for the
  // product each one belongs to: the damping term subtracted to form notch, and
  // the two integrator steps.
  //
  // Unsigned: each is `x & ((1 << N) - 1)`, so it lands in [0, 2^N) whatever
  // the sign of the product it came from. Each is a fraction of ONE state
  // count, so every bit is fractional -- 14 against the damp's 14, 15 against
  // the cutoff's 15.
  int32_t damping_term_remainder_u14;
  int32_t lp_step_remainder_u15, bp_step_remainder_u15;

  void Init() {
    bp = lp = 0;
    damping_term_remainder_u14 =
        lp_step_remainder_u15 = bp_step_remainder_u15 = 0;
  }

  // Chamberlin needs the damp range 0..2, which is what its one integer bit
  // buys. Neither parameter is ever negative: both come from tables built from
  // non-negative expressions.
  // A last bit of damping, and of integration, wherever the product would
  // truncate away. Every product here is wider than the state it lands in, and
  // what the shift drops is not noise -- it is the whole of the signal wherever
  // the product is smaller than one count, which is most of a quiet ring and
  // ALL of a slow one. Carry that remainder into the next sample so each step
  // is exact on average. Below |bp| < 16384/damp the damping term is otherwise
  // exactly zero and the resonator lossless, the band WIDER at low Q, and the
  // bp integrator strands a DC offset in lp the same way.
  //   - rounding the damping term instead leaves every setting stuck between 16
  //     and 48.
  //   - faking a minimum step makes the filter lossy by construction: a forced
  //     count per sample caps Q at 32 however small the damping asked for.
  //
  // kMustReachSilence is the caller's. A remainder carrying the SIGNED damping
  // term cancels against itself over an oscillation and never crosses one
  // count, so the ring stops short of zero and holds there -- measured at up to
  // 3468 counts, for ever. The MAGNITUDE only ever rises, so it always crosses,
  // and bp's sign put back on it always opposes bp. Ring times are within 1.2%
  // either way.
  template<SvfOutput kOutput, bool kMustReachSilence>
  inline int32_t Process(int32_t in, int16_t cutoff_u15, int16_t damp_u1_14) {
    int32_t this_notch;
    if (kMustReachSilence) {
      const int32_t magnitude_q16_14 =
          (bp < 0 ? -bp : bp) * damp_u1_14 + damping_term_remainder_u14;
      damping_term_remainder_u14 = magnitude_q16_14 & ((1 << 14) - 1);
      this_notch = Clip16(in - (bp < 0
          ? -(magnitude_q16_14 >> 14) : (magnitude_q16_14 >> 14)));
    } else {
      int32_t damped_q16_14 = bp * damp_u1_14 + damping_term_remainder_u14;
      damping_term_remainder_u14 = damped_q16_14 & ((1 << 14) - 1);
      this_notch = Clip16(in - (damped_q16_14 >> 14));
    }
    int32_t lp_moved_q15_15 = cutoff_u15 * bp + lp_step_remainder_u15;
    lp_step_remainder_u15 = lp_moved_q15_15 & ((1 << 15) - 1);
    lp = Clip16(lp + (lp_moved_q15_15 >> 15));
    const int32_t this_hp = Clip16(this_notch - lp);
    int32_t bp_moved_q15_15 = cutoff_u15 * this_hp + bp_step_remainder_u15;
    bp_step_remainder_u15 = bp_moved_q15_15 & ((1 << 15) - 1);
    bp = Clip16(bp + (bp_moved_q15_15 >> 15));
    if (kOutput == SVF_NOTCH) return this_notch;
    if (kOutput == SVF_HP) return this_hp;
    return kOutput == SVF_LP ? lp : bp;
  }

  static inline int16_t CutoffFromFreq(int16_t freq_u15) {
    uint32_t index = freq_u15 << (32 - 15);
    int16_t cutoff_u15 = Interpolate824(lut_svf_cutoff_u15, index);
    return cutoff_u15;
  }
};

}  // namespace yarns

#endif  // YARNS_SVF_H_
