// Copyright 2012 Emilie Gillet.
// Copyright 2020 Chris Rogers.
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

#ifndef YARNS_ENVELOPE_H_
#define YARNS_ENVELOPE_H_

#include "yarns/resources.h"

namespace yarns {

using namespace stmlib;

enum EnvelopeStage {
  ENV_STAGE_ATTACK,
  ENV_STAGE_DECAY,
  ENV_STAGE_SUSTAIN,
  ENV_STAGE_RELEASE,
  ENV_STAGE_DEAD,
  ENV_NUM_STAGES,
};

struct ADSR {
  uint16_t peak_u16, sustain_u16; // Platonic, unscaled targets
  uint32_t attack_u32, decay_u32, release_u32; // Phase increments
};

const uint8_t kLutExpoSlopeShiftSizeBits = 4;
STATIC_ASSERT(
  1 << kLutExpoSlopeShiftSizeBits == LUT_EXPO_SLOPE_SHIFT_SIZE,
  expo_slope_shift_size
);

// Chiff LPF cutoff dither — these constants MUST match the formulas in
// yarns/resources/lookup_tables.py:chiff_lpf_shifts(). Keep in sync.
const uint8_t kChiffLpfShiftSlotCount     = 4;   // N: shifts per pitch bin
const uint8_t kChiffLpfShiftSlotBits      = 4;   // bits per packed shift slot
const uint8_t kChiffLpfShiftSlotIdxBits   = 2;   // log2(SlotCount)
const uint8_t kChiffLpfBinFracBits        = 3;   // 8 sub-octave bins per clz step
const uint8_t kChiffLpfBinBaseClz         = 13;  // bin 0 ≈ 5 Hz pitch at SR=45 kHz
const uint8_t kChiffLpfBinCount           = 128; // total bins (= LUT length)
const uint16_t kChiffLpfDefaultShiftsPacked = 0x3333u;  // all shifts=3, α≈1/8
// Inner-loop fast path requires slot_bits == 1 << slot_idx_bits so a
// single masked PRNG already yields the bit-position into the packed
// shifts (no multiply needed).
STATIC_ASSERT(
  (1 << kChiffLpfShiftSlotIdxBits) == kChiffLpfShiftSlotBits,
  chiff_lpf_slot_layout
);

class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init(int16_t zero_value_s16);
  // Refill the system-wide chiff PRNG buffer; must be called once per
  // audio block (before any envelope renders) so all envelopes share the
  // same random words this block.
  static void FillSharedPrngBuffer();
  void NoteOff();
  void NoteOn(
    ADSR& adsr,
    // Bounds stored as s32 but semantically s16
    int32_t min_target_s16, int32_t max_target_s16,
    uint8_t chiff_amount
  );
  void Trigger(EnvelopeStage stage);
  void RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31);
  void RenderStageDispatch(
    int16_t* sample_buffer, size_t samples_left,
    int32_t bias_q31, int32_t bias_slope_q31
  );
  template<bool MOVING, bool POSITIVE_SLOPE>
  void RenderStage(
    int16_t* sample_buffer, size_t samples_left,
    int32_t bias_q31, int32_t bias_slope_q31
  );

  void Rescale(float scaling_factor);

  inline int16_t tremolo(uint16_t strength_u16) const {
    int32_t relative_value_q15 = (value_q30_ - stage_target_q30_[ENV_STAGE_RELEASE]) >> (30 - 15);
    return relative_value_q15 * -strength_u16 >> 16;
  }

  inline int16_t value() const { return value_q30_ >> (30 - 15); }
  inline EnvelopeStage stage() const { return stage_; }

  // Per-block setter: packs 4 × 4-bit LPF shifts from
  // lut_chiff_lpf_shifts[pitch_bin]. Chiff post-pass picks one shift
  // uniformly per sample via 2 PRNG bits, averaging to a pitch-tracked
  // cutoff. Caller computes pitch_bin from phase_increment.
  inline void set_chiff_lpf_shifts(uint16_t packed) {
    chiff_lpf_shifts_packed_ = packed;
  }
  // Helper: compute pitch_bin index for lut_chiff_lpf_shifts from a
  // phase_increment. Encoding: (BaseClz - clz(pinc)) << FracBits |
  // top FracBits below MSB, clamped to [0, BinCount-1]. Must mirror the
  // formula in lookup_tables.py:chiff_lpf_shifts().
  static inline uint8_t chiff_pitch_bin(uint32_t phase_increment) {
    const uint32_t pinc = phase_increment | 1u;     // avoid clz(0)
    const uint32_t clz_val = __builtin_clz(pinc);
    const uint32_t mantissa = pinc << (clz_val + 1);
    const uint32_t frac = mantissa >> (32 - kChiffLpfBinFracBits);
    int32_t raw =
        (static_cast<int32_t>(kChiffLpfBinBaseClz) - static_cast<int32_t>(clz_val))
            * (1 << kChiffLpfBinFracBits)
        + static_cast<int32_t>(frac);
    if (raw < 0) raw = 0;
    if (raw >= kChiffLpfBinCount) raw = kChiffLpfBinCount - 1;
    return static_cast<uint8_t>(raw);
  }

  static inline uint8_t signed_clz(int32_t x) {
    const uint32_t x_for_clz = static_cast<uint32_t>(abs(x >= 0 ? x : x + 1));
    return __builtin_clzl(x_for_clz) - 1;
  }

 private:
  ADSR* adsr_;

  // Q30 in int32_t; the top integer bit is saturation headroom for
  // `value += slope` overshoot and for SatSub deltas (range [-2, 2)).
  int32_t stage_target_q30_[ENV_NUM_STAGES];
  int32_t target_q30_, value_q30_;
  int32_t expo_slope_lut_q30_[LUT_EXPO_SLOPE_SHIFT_SIZE];

  // Q31 (full s32; no overshoot, slope is pre-scaled by block size).
  int32_t bias_q31_;

  // Current stage.
  EnvelopeStage stage_;

  uint32_t phase_u32_, phase_increment_u32_;

  // Chiff: bandlimited noise additively mixed into the envelope output as
  // a post-process pass. Per-sample ±noise of decaying magnitude is
  // smoothed by a 1-pole LPF, then added to the sample (saturated).
  // The downstream gain*carrier multiply turns this into AM noise around
  // the carrier — broadband-decaying-to-pitched character (the carrier is
  // the resonator; this is the excitation). Replaces the older
  // value-replacement chiff, which was too click-like on long attacks.
  //
  // Amplitude is derived from chiff_probability_u31_, which still ramps
  // linearly to 0 over a duration equal to the attack stage. The LPF
  // state continues filtering past attack so the noise tail decays
  // smoothly rather than cutting off. Post-pass runs unconditionally
  // each block for uniform worst-case cost. All envelopes share one
  // PRNG buffer per block (filled in FillSharedPrngBuffer); the per-
  // instance XOR mask decorrelates sample timing across envelopes.
  uint32_t chiff_probability_u31_;        // current ramping noise amplitude scale, max ~2^31-1
  uint32_t chiff_prob_decrement_u32_;     // per-sample decrement
  uint32_t chiff_prng_xor_u32_;           // per-instance PRNG decorrelation mask, set in Init()
  int32_t  chiff_lp_q15_;                 // 1-pole LPF state in Q15 (same scale as sample buffer)
  uint16_t chiff_lpf_shifts_packed_;      // 4 × 4-bit LPF shifts (from lut_chiff_lpf_shifts)

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
