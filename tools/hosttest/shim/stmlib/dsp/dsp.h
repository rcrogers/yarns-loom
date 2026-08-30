// Copyright 2012 Emilie Gillet.
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
// DSP utility routines.

#ifndef STMLIB_UTILS_DSP_DSP_H_
#define STMLIB_UTILS_DSP_DSP_H_

#include "stmlib/stmlib.h"

#include <cmath>
#include <math.h>

namespace stmlib {

#define MAKE_INTEGRAL_FRACTIONAL(x) \
  int32_t x ## _integral = static_cast<int32_t>(x); \
  float x ## _fractional = x - static_cast<float>(x ## _integral);

inline float Interpolate(const float* table, float index, float size) {
  index *= size;
  MAKE_INTEGRAL_FRACTIONAL(index)
  float a = table[index_integral];
  float b = table[index_integral + 1];
  return a + (b - a) * index_fractional;
}


inline float InterpolateHermite(const float* table, float index, float size) {
  index *= size;
  MAKE_INTEGRAL_FRACTIONAL(index)
  const float xm1 = table[index_integral - 1];
  const float x0 = table[index_integral + 0];
  const float x1 = table[index_integral + 1];
  const float x2 = table[index_integral + 2];
  const float c = (x1 - xm1) * 0.5f;
  const float v = x0 - x1;
  const float w = c + v;
  const float a = w + v + (x2 - x0) * 0.5f;
  const float b_neg = w + a;
  const float f = index_fractional;
  return (((a * f) - b_neg) * f + c) * f + x0;
}

inline float InterpolateWrap(const float* table, float index, float size) {
  index -= static_cast<float>(static_cast<int32_t>(index));
  index *= size;
  MAKE_INTEGRAL_FRACTIONAL(index)
  float a = table[index_integral];
  float b = table[index_integral + 1];
  return a + (b - a) * index_fractional;
}

inline float SmoothStep(float value) {
  return value * value * (3.0f - 2.0f * value);
}

#define ONE_POLE(out, in, coefficient) out += (coefficient) * ((in) - out);
#define SLOPE(out, in, positive, negative) { \
  float error = (in) - out; \
  out += (error > 0 ? positive : negative) * error; \
}
#define SLEW(out, in, delta) { \
  float error = (in) - out; \
  float d = (delta); \
  if (error > d) { \
    error = d; \
  } else if (error < -d) { \
    error = -d; \
  } \
  out += error; \
}

inline float Crossfade(float a, float b, float fade) {
  return a + (b - a) * fade;
}

inline float SoftLimit(float x) {
  return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float SoftClip(float x) {
  if (x < -3.0f) {
    return -1.0f;
  } else if (x > 3.0f) {
    return 1.0f;
  } else {
    return SoftLimit(x);
  }
}

#ifdef TEST
  inline int32_t ClipS(int32_t x, uint8_t bits) {
    int32_t max = (1 << (bits - 1)) - 1;
    int32_t min = -(1 << (bits - 1));
    if (x < min) return min;
    if (x > max) return max;
    return x;
  }
  inline int32_t Clip16(int32_t x) {
    if (x < -32768) {
      return -32768;
    } else if (x > 32767) {
      return 32767;
    } else {
      return x;
    }
  }
  inline uint32_t ClipU(int32_t x, uint8_t bits) {
    int32_t max = (1 << bits) - 1;
    if (x < 0) {
      return 0;
    } else if (x > max) {
      return max;
    } else {
      return x;
    }
  }
  inline uint16_t ClipU16(int32_t x) { return ClipU(x, 16); }
  inline uint32_t ClipUShifted(int32_t x, uint8_t bits, uint8_t right_shift) {
    return ClipU(x >> right_shift, bits);
  }
#else
  inline int32_t ClipS(int32_t x, uint8_t bits) {
    int32_t result;
    __asm ("ssat %0, %1, %2" : "=r" (result) :  "I" (bits), "r" (x) );
    return result;
  }
  inline int32_t Clip16(int32_t x) { return ClipS(x, 16); }

  inline uint32_t ClipU(int32_t x, uint8_t bits) {
    uint32_t result;
    __asm ("usat %0, %1, %2" : "=r" (result) :  "I" (bits), "r" (x) );
    return result;
  }
  inline uint32_t ClipU16(int32_t x) { return ClipU(x, 16); }

  // Arithmetic-shift-right then unsigned-saturate, folded into a single USAT
  // (the shift is an operand of the instruction). bits/right_shift must be
  // compile-time constants.
  inline uint32_t ClipUShifted(int32_t x, uint8_t bits, uint8_t right_shift) {
    uint32_t result;
    __asm ("usat %0, %1, %2, asr %3"
           : "=r" (result) : "I" (bits), "r" (x), "I" (right_shift) );
    return result;
  }
#endif

inline int32_t SatAdd(int32_t a, int32_t b, uint8_t bits) {
  return ClipS((a >> 1) + (b >> 1), bits - 1) << 1;
}

inline int32_t SatSub(int32_t a, int32_t b, uint8_t bits) {
  return SatAdd(a, -b, bits);
}

inline int32_t MulS32(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> 32);
}

inline uint32_t MulU32(uint32_t a, uint32_t b) {
    return (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32);
}

#define SHIFT_BY_SIGNED(x, shift) ((shift >= 0) ? (x << shift) : (x >> -shift))

inline uint32_t FractionU32(uint32_t num, uint32_t denom_gte_num) {
  if (denom_gte_num == 0) return UINT32_MAX;
  if (num == 0) return 0;

  uint8_t num_upshift = __builtin_clzl(num);
  uint32_t num_32 = num << num_upshift;

  int8_t denom_shift = __builtin_clzl(denom_gte_num) - 16;
  uint32_t denom_16 = SHIFT_BY_SIGNED(denom_gte_num, denom_shift);

  uint32_t result_16 = num_32 / denom_16;
  CONSTRAIN(result_16, 0, UINT16_MAX);
  int result_shift = 32 + denom_shift - num_upshift;
  return SHIFT_BY_SIGNED(result_16, result_shift);
}

#ifdef TEST
  inline float Sqrt(float x) {
    return sqrtf(x);
  }
#else
  inline float Sqrt(float x) {
    float result;
    __asm ("vsqrt.f32 %0, %1" : "=w" (result) : "w" (x) );
    return result;
  }
#endif

inline int16_t SoftConvert(float x) {
  return Clip16(static_cast<int32_t>(SoftLimit(x * 0.5f) * 32768.0f));
}

#define Q15_SHIFT 15

// Macro to unpack, multiply two Q15 values, and store the result
#define Q15_MULT_PAIR(a_pair, b_pair, result_ptr) do {              \
  int16_t a0 = (int16_t)((a_pair) & 0xFFFF);                        \
  int16_t a1 = (int16_t)(((a_pair) >> 16) & 0xFFFF);                \
  int16_t b0 = (int16_t)((b_pair) & 0xFFFF);                        \
  int16_t b1 = (int16_t)(((b_pair) >> 16) & 0xFFFF);                \
  int32_t result0 = ((int32_t)a0 * b0) >> Q15_SHIFT;                \
  int32_t result1 = ((int32_t)a1 * b1) >> Q15_SHIFT;                \
  *(int32_t*)(result_ptr) = (result1 << 16) | (result0 & 0xFFFF);   \
  result_ptr += 2;                                                  \
} while (0)

template<int LENGTH>
inline void q15_mult(const int16_t* a, const int16_t* b, int16_t* result) {
  STATIC_ASSERT(LENGTH % 4 == 0, length);
  int count = LENGTH / 4;
  while (count--) {
    int32_t a_pair1 = *(int32_t*)a;
    int32_t a_pair2 = *(int32_t*)(a + 2);
    int32_t b_pair1 = *(int32_t*)b;
    int32_t b_pair2 = *(int32_t*)(b + 2);
    Q15_MULT_PAIR(a_pair1, b_pair1, result);
    Q15_MULT_PAIR(a_pair2, b_pair2, result);
    a += 4;
    b += 4;
  }
}

// Saturating addition
#define Q15_ADD_PAIR(a_pair, b_pair, result_ptr, CLIP) do {         \
  int16_t a0 = (int16_t)((a_pair) & 0xFFFF);                        \
  int16_t a1 = (int16_t)(((a_pair) >> 16) & 0xFFFF);                \
  int16_t b0 = (int16_t)((b_pair) & 0xFFFF);                        \
  int16_t b1 = (int16_t)(((b_pair) >> 16) & 0xFFFF);                \
  if (CLIP) {                                                       \
    int32_t result0 = Clip16((int32_t)a0 + b0);                     \
    int32_t result1 = Clip16((int32_t)a1 + b1);                     \
    *(int32_t*)(result_ptr) = (result1 << 16) | (result0 & 0xFFFF); \
  } else {                                                          \
    int32_t result0 = (int32_t)a0 + b0;                             \
    int32_t result1 = (int32_t)a1 + b1;                             \
    *(int32_t*)(result_ptr) = (result1 << 16) | (result0 & 0xFFFF); \
  }                                                                 \
  result_ptr += 2;                                                  \
} while (0)

// Seems slower
// *result_ptr++ = (int16_t)(result0 & 0xFFFF);                      
// *result_ptr++ = (int16_t)(result1 & 0xFFFF);      

template<int LENGTH, bool CLIP>
inline void q15_add(const int16_t* a, const int16_t* b, int16_t* result) {
  STATIC_ASSERT(LENGTH % 4 == 0, length);
  int count = LENGTH / 4;
  while (count--) {
    int32_t a_pair1 = *(int32_t*)a;
    int32_t a_pair2 = *(int32_t*)(a + 2);
    int32_t b_pair1 = *(int32_t*)b;
    int32_t b_pair2 = *(int32_t*)(b + 2);
    Q15_ADD_PAIR(a_pair1, b_pair1, result, CLIP);
    Q15_ADD_PAIR(a_pair2, b_pair2, result, CLIP);
    a += 4;
    b += 4;
  }
}

#define UNPACK_PAIR_TO_Q15(x) \
  int32_t x ## pair = *(int32_t*)(x); \
  int16_t x ## 0 = (int16_t)((x ## pair) & 0xFFFF); \
  int16_t x ## 1 = (int16_t)(((x ## pair) >> 16) & 0xFFFF);

#define PACK_PAIR_Q15(x) \
  *(int32_t*)(x) = (x ## 1 << 16) | (x ## 0 & 0xFFFF);

inline void q15_2x_multiply_accumulate(const int16_t* a, const int16_t* b, int16_t* acc) {
  UNPACK_PAIR_TO_Q15(a);
  UNPACK_PAIR_TO_Q15(b);
  UNPACK_PAIR_TO_Q15(acc);

  // Wrapping (non-saturating) MAC: acc += (a * b) >> Q15_SHIFT.
  // Equivalent modulo 2^16 to the older upshift-then-downshift form
  // ((acc << 15) + a*b) >> 15 (since acc<<15 has zero low-15 bits, the
  // shift cleanly separates), but emits cleaner code: the product shift
  // folds into ARM's barrel-shifted ADD operand (`add acc, acc, rtmp,
  // asr #15`), saving an explicit upshift+downshift per lane.
  int32_t acc0_new = acc0 + (((int32_t)a0 * b0) >> Q15_SHIFT);
  int32_t acc1_new = acc1 + (((int32_t)a1 * b1) >> Q15_SHIFT);
  *(int32_t*)(acc) = (acc1_new << 16) | (acc0_new & 0xFFFF);
}

template<int LENGTH>
inline void q15_multiply_accumulate(const int16_t* a, const int16_t* b, int16_t* acc) {
  STATIC_ASSERT(LENGTH % 4 == 0, length);
  size_t count = LENGTH / 4;
  while (count--) {
    q15_2x_multiply_accumulate(a, b, acc);
    q15_2x_multiply_accumulate(a + 2, b + 2, acc + 2);
    a += 4;
    b += 4;
    acc += 4;
  }
}

#define U16_SHIFT 16

#define UNPACK_PAIR_TO_U16(x) \
  uint32_t x ## pair = *(uint32_t*)(x); \
  uint16_t x ## 0 = (uint16_t)((x ## pair) & 0xFFFF); \
  uint16_t x ## 1 = (uint16_t)(((x ## pair) >> 16) & 0xFFFF);

#define PACK_PAIR_U16(x) \
  *(uint32_t*)(x) = (x ## 1 << 16) | (x ## 0 & 0xFFFF);

inline void u16_2x_multiply_accumulate(const uint16_t* a, const uint16_t* b, uint16_t* acc) {
  UNPACK_PAIR_TO_U16(a);
  UNPACK_PAIR_TO_U16(b);

  // Need 32-bit headroom for saturating addition
  uint32_t acc_pair = *(uint32_t*)(acc);
  uint32_t acc0 = (uint16_t)((acc_pair) & 0xFFFF) << U16_SHIFT;
  uint32_t acc1 = (uint16_t)(((acc_pair) >> 16) & 0xFFFF) << U16_SHIFT;

  acc0 += (uint32_t)a0 * b0;
  acc1 += (uint32_t)a1 * b1;
  acc0 = acc0 >> U16_SHIFT;
  acc1 = acc1 >> U16_SHIFT;

  PACK_PAIR_U16(acc);
}

template<int LENGTH>
inline void u16_multiply_accumulate(const uint16_t* a, const uint16_t* b, uint16_t* acc) {
  STATIC_ASSERT(LENGTH % 4 == 0, length);
  size_t count = LENGTH / 4;
  while (count--) {
    u16_2x_multiply_accumulate(a, b, acc);
    u16_2x_multiply_accumulate(a + 2, b + 2, acc + 2);
    a += 4;
    b += 4;
    acc += 4;
  }
}

}  // namespace stmlib

#endif  // STMLIB_UTILS_DSP_DSP_H_