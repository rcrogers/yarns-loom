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
  template<SvfOutput kOutput>
  inline int32_t Process(int32_t in, int16_t cutoff_u15, int16_t damp_u1_14) {
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

// The high word of a 32x32 product: one SMULL or UMULL. Written out because
// GCC otherwise keeps the whole 64-bit product, sign word and all, and spills it.
#ifdef TEST
inline int32_t MulHighS(int32_t a, int32_t b) {
  return static_cast<int32_t>(static_cast<int64_t>(a) * b >> 32);
}
inline uint32_t MulHighU(uint32_t a, uint32_t b) {
  return static_cast<uint32_t>(static_cast<uint64_t>(a) * b >> 32);
}
// The high word of a * b + *low, leaving the low word in *low.
inline uint32_t MulAccumulateHighU(uint32_t a, uint32_t b, uint32_t* low) {
  const uint64_t sum = static_cast<uint64_t>(a) * b + *low;
  *low = static_cast<uint32_t>(sum);
  return static_cast<uint32_t>(sum >> 32);
}
#else
inline int32_t MulHighS(int32_t a, int32_t b) {
  int32_t low, high;
  __asm ("smull %0, %1, %2, %3" : "=&r" (low), "=&r" (high) : "r" (a), "r" (b));
  return high;
}
inline uint32_t MulHighU(uint32_t a, uint32_t b) {
  uint32_t low, high;
  __asm ("umull %0, %1, %2, %3" : "=&r" (low), "=&r" (high) : "r" (a), "r" (b));
  return high;
}
inline uint32_t MulAccumulateHighU(uint32_t a, uint32_t b, uint32_t* low) {
  uint32_t high = 0;
  __asm ("umlal %0, %1, %2, %3" : "+r" (*low), "+r" (high) : "r" (a), "r" (b));
  return high;
}
#endif

// A resonator's SVF state with 14 fractional bits: the integrators' remainders
// become part of it. With whole counts, a ring decayed to a few of them steps
// between them, its damping arrives as single-count kicks, and its pitch wanders
// with its amplitude -- a squiggle wherever the curve after it is driven hard
// enough to make a few counts audible. Loaded from an SVF and stored back once
// a block, so every other shape sees the SVF's own format.
//
// q15_14 rather than q15_15: a difference of two states, or a state less a
// damping term, then still fits int32.
struct ResonatorState {
  static const int32_t kFractionalBits = 14;
  int32_t bp_q15_14, lp_q15_14;
  // The damping term's fraction of a q15_14 count, as the low word of the
  // product that forms it.
  uint32_t damping_term_remainder_u32;

  inline void Load(const SVF& svf) {
    bp_q15_14 = svf.bp * (1 << 14) + (svf.bp_step_remainder_u15 >> 1);
    lp_q15_14 = svf.lp * (1 << 14) + (svf.lp_step_remainder_u15 >> 1);
    damping_term_remainder_u32 =
        static_cast<uint32_t>(svf.damping_term_remainder_u14) << (32 - 14);
  }
  inline void Store(SVF* svf) const {
    svf->bp = bp_q15_14 >> 14;
    svf->lp = lp_q15_14 >> 14;
    svf->bp_step_remainder_u15 = (bp_q15_14 & ((1 << 14) - 1)) << 1;
    svf->lp_step_remainder_u15 = (lp_q15_14 & ((1 << 14) - 1)) << 1;
    svf->damping_term_remainder_u14 =
        static_cast<int32_t>(damping_term_remainder_u32 >> (32 - 14));
  }

  // Each coefficient is taken pre-shifted so the product wanted is the HIGH
  // word of a 32x32 multiply: one SMULL, and one UMLAL for the damping term
  // with its remainder in the low word.
  //
  // The damping term is taken on bp's MAGNITUDE, its remainder carried: a
  // resonator must reach silence, because its excitation is gated rather than
  // its output. A remainder carrying the SIGNED term cancels against itself
  // over an oscillation and never crosses one count, so the ring stops short of
  // zero and holds there. The magnitude only ever rises, so it always crosses,
  // and bp's sign put back on it always opposes bp.
  //
  // cutoff is u0.31 so it stays positive as a signed operand: the high word is
  // cutoff * x >> 16, doubled back in the add. damp is u1.31 against 2 * |bp|,
  // so its high word is |bp| * damp >> 14.
  template<SvfOutput kOutput>
  inline int32_t Process(
      int32_t in_q15_14, int32_t cutoff_q0_31, int16_t damp_u1_14) {
    const uint32_t damp_u1_31 = static_cast<uint32_t>(damp_u1_14) << 17;
    const uint32_t twice_bp_magnitude = static_cast<uint32_t>(
        bp_q15_14 < 0 ? -bp_q15_14 : bp_q15_14) << 1;
    const int32_t damping_term_q15_14 = static_cast<int32_t>(MulAccumulateHighU(
        twice_bp_magnitude, damp_u1_31, &damping_term_remainder_u32));
    const int32_t notch_q15_14 = ClipS(in_q15_14
        - (bp_q15_14 < 0 ? -damping_term_q15_14 : damping_term_q15_14), 30);
    lp_q15_14 = ClipS(lp_q15_14 + 2 * MulHighS(cutoff_q0_31, bp_q15_14), 30);
    const int32_t hp_q15_14 = ClipS(notch_q15_14 - lp_q15_14, 30);
    bp_q15_14 = ClipS(bp_q15_14 + 2 * MulHighS(cutoff_q0_31, hp_q15_14), 30);
    if (kOutput == SVF_NOTCH) return notch_q15_14;
    if (kOutput == SVF_HP) return hp_q15_14;
    return kOutput == SVF_LP ? lp_q15_14 : bp_q15_14;
  }
};

}  // namespace yarns

#endif  // YARNS_SVF_H_
