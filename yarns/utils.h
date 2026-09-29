// Copyright 2020 Chris Rogers.
//
// Author: Chris Rogers (teukros@gmail.com)
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
// Arithmetic more than one module needs, and that the toolchain would
// otherwise charge a library for.

#ifndef YARNS_UTILS_H_
#define YARNS_UTILS_H_

#include "stmlib/stmlib.h"

namespace yarns {

// Exact unsigned 64/32 division, valid when the quotient fits 32 bits
// (hi < divisor). Hacker's Delight "divlu".
//
// Every 64-bit divide in yarns comes here. GCC 4.8 emits __aeabi_uldivmod
// for the plain form -- ~1.4 kB of library code that no check in the suite
// can see.
//
// Out of line: every caller is cold -- a note on, a shape change, a control
// tick -- so there is nothing to buy by inlining, and inlined at each of the
// four sites it costs 244 bytes more than the one copy.
uint32_t DivU64ByU32(uint32_t hi, uint32_t lo, uint32_t divisor);

// xorshift32. Zero is a fixed point, so a seed must be nonzero.
//
// Inline, unlike the rest of this header: its callers are per-sample loops, and
// on Cortex-M3 each line is one barrel-shifted eor -- three cycles and no
// literal pool, where a multiplicative generator spends two loads on its
// constants and holds two registers for them.
inline uint32_t NextXorshift32(uint32_t state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// A seed for NextXorshift32, nonzero and distinct from the last. xorshift32 has
// one orbit, so seeds are phases of a single stream and near seeds start near
// each other -- hence a large odd stride. Out of line, and not drawn from
// stmlib::Random: a seeder that consumes the shared stream moves every other
// draw off it, which shifted three shapes that had not been touched.
uint32_t NextXorshift32Seed();

// Restarts that sequence, so a harness can repeat a run. Firmware never calls
// it: the sequence only has to be deterministic, not chosen.
void RestartXorshift32Seeds(uint32_t from);

// Floor of the square root. Out of line for the same reason as the divide:
// every caller is cold, and GCC has no integer sqrt to reach for.
uint32_t IntegerSqrt(uint32_t x);

}  // namespace yarns

#endif  // YARNS_UTILS_H_
