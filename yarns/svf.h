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

  void Init() {
    bp = lp = notch = hp = 0;
  }

  // cutoff: Q0.15, damp: Q1.14 (Chamberlin needs damp range 0..2.0)
  inline void Process(int32_t in, int16_t cutoff, int16_t damp) {
    int32_t damped_bp = bp * damp >> 14;
    notch = in - damped_bp;
    notch = Clip16(notch);
    lp += cutoff * bp >> 15;
    lp = Clip16(lp);
    hp = notch - lp;
    hp = Clip16(hp);
    bp += cutoff * hp >> 15;
    bp = Clip16(bp);
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
