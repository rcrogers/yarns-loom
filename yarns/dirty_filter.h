// Copyright 2026 Chris Rogers.
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
// 1-pole LPF with PRNG-dithered shift coefficients ("dirty" because the
// per-sample alpha is intentionally noisy; it averages to a target cutoff).
//
// Design: each pitch bin in lut_chiff_lpf_shifts holds N=4 LPF shift values
// (4 bits each) packed into a uint16. Per sample, a PRNG draw selects one of
// the 4 via masked bits; the mean(1/2^shift) over time approximates the
// pitch-tracked target cutoff with sub-octave precision. Used by chiff noise
// modulation in Envelope to bandlimit the noise to a pitch-tracked region.
//
// Constants below MUST match yarns/resources/lookup_tables.py:chiff_lpf_shifts().

#ifndef YARNS_DIRTY_FILTER_H_
#define YARNS_DIRTY_FILTER_H_

#include "stmlib/stmlib.h"

namespace yarns {

namespace dirty_filter {

// --- Packed-shift LUT entry layout -------------------------------------------

const uint8_t kSlotCount     = 4;   // N: shifts per pitch bin
const uint8_t kSlotBits      = 4;   // bits per packed shift slot
const uint8_t kSlotIdxBits   = 2;   // log2(SlotCount)

// Fast-path requires slot_bits == 1 << slot_idx_bits so a single masked
// PRNG already yields the bit-position into the packed shifts (no multiply
// needed when extracting).
STATIC_ASSERT(
  (1 << kSlotIdxBits) == kSlotBits,
  dirty_filter_slot_layout
);

const uint32_t kShiftBitPosMask =
    ((1u << kSlotIdxBits) - 1u) << kSlotIdxBits;
const uint32_t kShiftValMask = (1u << kSlotBits) - 1u;

// Default packed entry: all slots = 3 → alpha = 1/8, cutoff ~900 Hz at
// SR = 45 kHz. Used by Envelope::Init until a caller installs a pitch-
// tracked entry.
const uint16_t kDefaultShiftsPacked = 0x3333u;

// --- Pitch bin index -----------------------------------------------------------

const uint8_t kBinFracBits   = 3;   // sub-octave bins per integer clz step
const uint8_t kBinBaseClz    = 13;  // bin 0 ≈ 5 Hz pitch at SR = 45 kHz
const uint8_t kBinCount      = 128; // LUT length

// Compute pitch_bin index for lut_chiff_lpf_shifts from a phase_increment.
// Encoding: ((kBinBaseClz - clz(pinc)) << kBinFracBits) | top kBinFracBits
// below MSB, clamped to [0, kBinCount - 1]. Must mirror the formula in
// yarns/resources/lookup_tables.py:chiff_lpf_shifts().
inline uint8_t pitch_bin(uint32_t phase_increment) {
  const uint32_t pinc = phase_increment | 1u;  // avoid clz(0)
  const uint32_t clz_val = __builtin_clz(pinc);
  const uint32_t mantissa = pinc << (clz_val + 1);
  const uint32_t frac = mantissa >> (32 - kBinFracBits);
  int32_t raw =
      (static_cast<int32_t>(kBinBaseClz) - static_cast<int32_t>(clz_val))
          * (1 << kBinFracBits)
      + static_cast<int32_t>(frac);
  if (raw < 0) raw = 0;
  if (raw >= kBinCount) raw = kBinCount - 1;
  return static_cast<uint8_t>(raw);
}

// --- LPF update --------------------------------------------------------------

// Extract one shift from a packed entry, indexed by prng_word's relevant
// bits. The extracted value is in [0, kShiftValMask].
inline uint32_t extract_shift(uint16_t shifts_packed, uint32_t prng_word) {
  return (shifts_packed >> (prng_word & kShiftBitPosMask)) & kShiftValMask;
}

// One-pole LPF update step with externally-provided shift. Caller is
// responsible for picking a shift (typically via extract_shift above).
//   lp += (input - lp) >> shift
inline void update(int32_t& lp, int32_t input, uint32_t shift) {
  lp += (input - lp) >> shift;
}

}  // namespace dirty_filter

}  // namespace yarns

#endif  // YARNS_DIRTY_FILTER_H_
