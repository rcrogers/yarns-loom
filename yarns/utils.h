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

}  // namespace yarns

#endif  // YARNS_UTILS_H_
