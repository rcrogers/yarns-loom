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
template<typename HighRes, typename LowRes, uint8_t DX>
class Interpolator {
 STATIC_ASSERT(sizeof(HighRes) == 2 * sizeof(LowRes), sizes);
 public:
  typedef union {
    HighRes high_res;
    struct { // endian-specific!
      LowRes subsampling;
      LowRes low_res;
    };
  } fixed_point;

  void Init() {
    value_.high_res = 0;
    slope_ = 0;
  }
  void SetTarget(LowRes t) { target_ = t; } // 15-bit
  void ComputeSlope() {
    slope_ = ((static_cast<HighRes>(target_ ) << sizeof(LowRes)) - value_.high_res) / DX;
  }
  void SetSlope(HighRes slope) { slope_ = slope; }
  void Tick() {
    SaturatingIncrement(value_.high_res, slope_);
  }
  LowRes value() const { return value_.low_res; }
  LowRes target() const { return target_; }

private:
  fixed_point value_;
  LowRes target_;

  // Updated by ComputeSlope
  HighRes slope_;
};

}  // namespace yarns

#endif // YARNS_INTERPOLATOR_H_
