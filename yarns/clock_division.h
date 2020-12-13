// Copyright 2013 Emilie Gillet.
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
// Clock division definitions.

#ifndef YARNS_CLOCK_DIVISION_H_
#define YARNS_CLOCK_DIVISION_H_

namespace yarns {
namespace clock_division {

struct ClockDivision {
  const char* const display;
  const uint16_t num_ticks;
};

const uint8_t count = 15;
const ClockDivision list[] = {
  { "x4", 96 * 4 },
  { "x3", 96 * 3 },
  { "x2", 96 * 2 },
  { "/1", 96 },
  { "/2", 48 },
  { "/3", 32 },
  { "/4", 24 },
  { "/6", 16 },
  { "/8", 12 },
  { "12", 8 },
  { "16", 6 },
  { "24", 4 },
  { "32", 3 },
  { "48", 2 },
  { "96", 1 },
};

}  // namespace clock_division
}  // namespace yarns

#endif // YARNS_CLOCK_DIVISIONS_H_
