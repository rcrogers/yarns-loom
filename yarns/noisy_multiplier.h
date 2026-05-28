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
// PRNG-dithered multiplier-by-1/2^shift. Per pitch bin, an LUT entry holds
// a small set of integer shifts whose mean inverse-power-of-2 equals a
// target fractional alpha. Per sample, a PRNG draw selects one shift; the
// time-average over many samples lands the effective alpha at the target
// with sub-octave precision — a "noisy multiplier" whose mean is the
// desired fractional coefficient.
//
// The shift can drive whatever the caller needs (LPF coefficient, gain
// scaler, decay rate). The actual multiplication/shift operation is the
// caller's concern; this header only exposes the LUT layout and the
// per-sample extract.
//
// Constants below MUST match yarns/resources/lookup_tables.py:chiff_lpf_shifts().

#ifndef YARNS_NOISY_MULTIPLIER_H_
#define YARNS_NOISY_MULTIPLIER_H_

#include "stmlib/stmlib.h"

namespace yarns {

namespace noisy_multiplier {

// --- Packed-shift LUT entry layout -------------------------------------------

const uint8_t kSlotCount     = 4;   // N: shifts per pitch bin
const uint8_t kSlotBits      = 4;   // bits per packed shift slot
const uint8_t kSlotIdxBits   = 2;   // log2(SlotCount)

// Fast-path requires slot_bits == 1 << slot_idx_bits so a single masked
// PRNG already yields the bit-position into the packed shifts (no multiply
// needed when extracting).
STATIC_ASSERT(
  (1 << kSlotIdxBits) == kSlotBits,
  noisy_multiplier_slot_layout
);

const uint32_t kShiftValMask = (1u << kSlotBits) - 1u;

// Default packed entry: all slots = 3 → alpha = 1/8, cutoff ~900 Hz at
// SR = 45 kHz when used as a 1-pole LPF. Used until a caller installs a
// pitch-tracked entry.
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

// Callers are expected to unpack the LUT entry into a uint8_t[kSlotCount]
// array (member or local), then per sample index by:
//   shifts[(prng >> kSlotIdxBits) & ((1 << kSlotIdxBits) - 1)]
// The ldrb's load-use stall is hidden by the immediately-following ALU op
// (typically `sub noise, lp`) so the per-sample cost is effectively 2
// cycles vs 3 for a shift+mask on packed nibbles.

}  // namespace noisy_multiplier

}  // namespace yarns

#endif  // YARNS_NOISY_MULTIPLIER_H_
