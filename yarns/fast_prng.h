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
// Multiply-free PRNG for inner loops where stmlib's LCG-based Random costs
// too much. Marsaglia xorshift32, period 2^32 - 1, three shift+XOR per draw.
//
// To use in a new site:
//   1. Add a uint32_t state variable, seeded nonzero (zero is a fixed
//      point of xorshift). Persist across calls.
//   2. Per draw, call FastPrngDraw(state) for a raw 32-bit word, or
//      FastPrngDitheredSample(state, K, p) for a signed noise sample with
//      continuous amplitude control via dithered shift.

#ifndef YARNS_FAST_PRNG_H_
#define YARNS_FAST_PRNG_H_

#include "stmlib/stmlib.h"

namespace yarns {

inline uint32_t FastPrngDraw(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// Returns 1 with probability threshold / 2^kBits, else 0. Compares the low
// kBits of `random_bits` against `threshold`. The caller supplies the bits
// (typically a slice of a FastPrngDraw word), so multiple independent
// dithers can share a single draw — pass disjoint bit fields of the same
// word for each axis.
//
// threshold valid range: [0, (1 << kBits)].
template<uint8_t kBits>
inline uint8_t FastPrngDitherBit(uint32_t random_bits, uint8_t threshold) {
  return (random_bits & ((1u << kBits) - 1)) < threshold ? 1 : 0;
}

// Signed noise sample, ~white. Peak amplitude is 2^(31 - downshift_u8),
// halved when the one-bit dither fires (shift becomes downshift_u8 + 1).
// Dither probability is dither_threshold / 2^kDitherBits; sweeping the
// threshold moves RMS smoothly between the two rungs without any multiply.
// A single draw fuels both the dither comparison (low kDitherBits) and
// the noise (high bits, post-shift); they don't observably correlate.
//
// dither_threshold valid range: [0, (1 << kDitherBits)].
template<uint8_t kDitherBits>
inline int32_t FastPrngDitheredSample(
    uint32_t& state, uint8_t downshift_u8, uint8_t dither_threshold) {
  uint32_t word = FastPrngDraw(state);
  uint8_t shift = downshift_u8 +
    FastPrngDitherBit<kDitherBits>(word, dither_threshold);
  return static_cast<int32_t>(word) >> shift;
}

}  // namespace yarns

#endif  // YARNS_FAST_PRNG_H_
