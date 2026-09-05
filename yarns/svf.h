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
  int32_t damping_term_remainder, lp_step_remainder, bp_step_remainder;

  void Init() {
    bp = lp = notch = hp = 0;
    damping_term_remainder = lp_step_remainder = bp_step_remainder = 0;
  }

  // cutoff: Q0.15, damp: Q1.14 (Chamberlin needs damp range 0..2.0)
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
  inline void Process(int32_t in, int16_t cutoff, int16_t damp) {
    int32_t damped = bp * damp + damping_term_remainder;
    damping_term_remainder = damped & ((1 << 14) - 1);
    notch = Clip16(in - (damped >> 14));
    int32_t lp_moved = cutoff * bp + lp_step_remainder;
    lp_step_remainder = lp_moved & ((1 << 15) - 1);
    lp = Clip16(lp + (lp_moved >> 15));
    hp = Clip16(notch - lp);
    int32_t bp_moved = cutoff * hp + bp_step_remainder;
    bp_step_remainder = bp_moved & ((1 << 15) - 1);
    bp = Clip16(bp + (bp_moved >> 15));
  }

  // Conversion methods. Callers pass Q0.15 domain values, get back
  // parameters ready for Process.
  static inline int16_t DampFromResonance(int16_t resonance_q_0_15) {
    uint32_t index = resonance_q_0_15 << (32 - 15);
    uint16_t damp_u_1_15 = Interpolate824(lut_svf_damp, index);
    int16_t damp_q_1_14 = damp_u_1_15 >> 1;
    return damp_q_1_14;
  }
  static inline int16_t CutoffFromFreq(int16_t freq_q_0_15) {
    uint32_t index = freq_q_0_15 << (32 - 15);
    int16_t cutoff_q_0_15 = Interpolate824(lut_svf_cutoff, index);
    return cutoff_q_0_15;
  }
};

}  // namespace yarns

#endif  // YARNS_SVF_H_
