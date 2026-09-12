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

struct SVF {
  int32_t bp, lp, notch, hp;
  // THE REMAINDER OF THE SHIFT THAT PUTS EACH WIDE PRODUCT BACK IN THE STATE'S
  // UNITS -- what integer division by 2^14 or 2^15 leaves behind. Named for the
  // product each one belongs to: the damping term subtracted to form notch, and
  // the two integrator steps.
  // UNSIGNED: each is `x & ((1 << N) - 1)`, so it lands in [0, 2^N) whatever
  // the sign of the product it came from. Each is a fraction of ONE state
  // count, so every bit is fractional -- 14 against the damp's 14, 15 against
  // the cutoff's 15.
  int32_t damping_term_remainder_u14;
  int32_t lp_step_remainder_u15, bp_step_remainder_u15;

  void Init() {
    bp = lp = notch = hp = 0;
    damping_term_remainder_u14 =
        lp_step_remainder_u15 = bp_step_remainder_u15 = 0;
  }

  // Chamberlin needs the damp range 0..2, which is what its one integer bit
  // buys. Neither parameter is ever negative: both come from tables built from
  // non-negative expressions.
  // A LAST BIT OF DAMPING, AND OF INTEGRATION, WHEREVER THE PRODUCT WOULD
  // TRUNCATE AWAY. Below |bp| < 16384/damp the damping term is exactly zero and
  // the resonator is lossless -- it rings at that amplitude for ever, and the
  // band is WIDER at low Q. The bp integrator strands a DC offset in lp the
  // same way. Rounding alone does not do it: rounding the damping term as well
  // leaves every setting stuck between 16 and 48.
  // EVERY PRODUCT HERE IS WIDER THAN THE STATE IT LANDS IN, and what the shift
  // drops is not noise -- it is the whole of the signal wherever the product is
  // smaller than one count, which is most of a quiet ring and ALL of a slow one.
  // Carry that remainder into the next sample so each step is exact on average.
  // Faking a minimum step instead makes the filter lossy by construction: a
  // forced count per sample caps Q at 32 however small the damping asked for.
  // NOTCH AND HP ARE NOT STATE. Each is formed and consumed inside one call --
  // notch feeds hp, hp feeds the band-pass step -- and neither is read on the
  // next. They are members only because the NOISE shapes take their output from
  // them, so kKeepNotchAndHp says whether this caller is one of those. A caller
  // that is not stores two fewer words a sample, which is worth having in a
  // loop that is already 37% memory traffic.
  template<bool kKeepNotchAndHp>
  inline void ProcessInto(int32_t in, int16_t cutoff_u15, int16_t damp_u1_14) {
    int32_t damped_q16_14 = bp * damp_u1_14 + damping_term_remainder_u14;
    damping_term_remainder_u14 = damped_q16_14 & ((1 << 14) - 1);
    const int32_t this_notch = Clip16(in - (damped_q16_14 >> 14));
    int32_t lp_moved_q15_15 = cutoff_u15 * bp + lp_step_remainder_u15;
    lp_step_remainder_u15 = lp_moved_q15_15 & ((1 << 15) - 1);
    lp = Clip16(lp + (lp_moved_q15_15 >> 15));
    const int32_t this_hp = Clip16(this_notch - lp);
    int32_t bp_moved_q15_15 = cutoff_u15 * this_hp + bp_step_remainder_u15;
    bp_step_remainder_u15 = bp_moved_q15_15 & ((1 << 15) - 1);
    bp = Clip16(bp + (bp_moved_q15_15 >> 15));
    if (kKeepNotchAndHp) { notch = this_notch; hp = this_hp; }
  }
  inline void Process(int32_t in, int16_t cutoff_u15, int16_t damp_u1_14) {
    ProcessInto<true>(in, cutoff_u15, damp_u1_14);
  }

  static inline int16_t CutoffFromFreq(int16_t freq_u15) {
    uint32_t index = freq_u15 << (32 - 15);
    int16_t cutoff_u15 = Interpolate824(lut_svf_cutoff_u15, index);
    return cutoff_u15;
  }
};

}  // namespace yarns

#endif  // YARNS_SVF_H_
