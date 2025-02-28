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
template<uint8_t SLOPE_DOWNSHIFT>
class Interpolator {
 public:
  typedef union {
    int32_t i;
    struct { // endian-specific!
      int16_t lo;
      int16_t hi;
    };
  } fixed_point;

  void Init() {
    y_.i = 0;
    m_ = 0;
  }
  void SetTarget(int16_t y) { y_target_ = y; } // 15-bit
  void ComputeSlope() {
    m_ = static_cast<int32_t>((y_target_ - y_.hi) << 16) >> SLOPE_DOWNSHIFT;
  }
  void Tick() {
    y_.i += m_;
  }
  inline int16_t value() const { return y_.hi; }
  inline int16_t target() const { return y_target_; }
  inline int32_t slope() const { return m_; }

private:
  fixed_point y_;
  int16_t y_target_;

  // Updated by ComputeSlope
  int32_t m_;
};

}  // namespace yarns

#endif // YARNS_INTERPOLATOR_H_
