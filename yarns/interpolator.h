// Copyright 2021 Chris Rogers.
//
// Author: Chris Rogers
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
// Linear interpolator.

#ifndef YARNS_INTERPOLATOR_H
#define YARNS_INTERPOLATOR_H

#include <cstdio>

namespace yarns {

// https://hbfs.wordpress.com/2009/07/28/faster-than-bresenhams-algorithm/
class Interpolator {
 public:
  typedef union {
    int32_t i;
    struct { // endian-specific!
      int16_t lo;
      int16_t hi;
    };
  } fixed_point;

  void Init(uint8_t dx) {
    x_delta_ = dx;
    y_.i = 0;
    m_ = 0;
  }
  void SetTarget(int16_t y) { // 15-bit
    y_target_ = y;
  }
  void ComputeSlope() {
    m_ = static_cast<int32_t>((y_target_ - y_.hi) << 16) / x_delta_;
  }
  void Tick() {
    if (m_ > 0 && y_.i > INT32_MAX - m_) y_.i = INT32_MAX;
    else if (m_ < 0 && y_.i < INT32_MIN - m_) y_.i = INT32_MIN;
    else y_.i += m_;
  }
  int16_t value() const { return y_.hi; }
  int16_t target() const { return y_target_; }

private:
  uint8_t x_delta_;
  fixed_point y_;
  int16_t y_target_;
  int32_t m_;
};

}  // namespace yarns

#endif // YARNS_INTERPOLATOR_H_
