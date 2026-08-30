// Copyright 2026 Chris Rogers.
//
// Inspired by: https://codeplea.com/optimal-bit-packing
//
// Dense encoding for fixed-size arrays where each element has fewer distinct
// values than a power of 2.  For example, 30 elements with 130 possible
// values each would normally take 30 bytes (1 byte per element, wasting the
// unused 126 values per byte).  Dense encoding packs them into
// ceil(30 * log2(130) / 8) = 27 bytes by treating the array as a single
// large number in base 130.
//
// Usage:
//   typedef DenseArray<num_elements, num_values_per_element> MyDenseArray;
//   uint8_t buf[MyDenseArray::kNumBytes];    // storage
//   MyDenseArray::Encode(buf, value);        // call per element, first to last
//   value = MyDenseArray::Decode(buf);       // call per element, last to first

#ifndef YARNS_DENSE_ARRAY_H_
#define YARNS_DENSE_ARRAY_H_

#include "stmlib/stmlib.h"

namespace yarns {

namespace dense_internal {

// Compile-time byte count for dense encoding.  Simulates filling bytes:
// partial tracks how much of the current byte the elements so far have used,
// in fixed point, and each element multiplies it by the radix.
//
// The fraction is carried up rather than truncated.  Truncating drifts partial
// low, so a byte eventually goes unemitted and the array comes out too small
// to hold what Encode will write -- DenseArray<11, 3> asked for two bytes
// where it needs three.  Fifteen fraction bits is the least that is exact for
// every radix a DenseArray can name, and leaves the widest intermediate at
// 2^30, inside uint32_t.
const uint32_t kFractionBits = 15;
const uint32_t kOne = 1u << kFractionBits;
const uint32_t kByteFull = 256u << kFractionBits;

// One byte per whole 256 that partial has grown past, however many that is.
template<uint32_t partial, bool full = (partial >= kByteFull)>
struct Carry {
  static const uint32_t bytes = 0;
  static const uint32_t rest = partial;
};
template<uint32_t partial>
struct Carry<partial, true> {
  typedef Carry<(partial + 255) / 256> Next;
  static const uint32_t bytes = 1 + Next::bytes;
  static const uint32_t rest = Next::rest;
};

template<uint32_t remaining, uint32_t radix, uint32_t partial>
struct BytesNeeded {
  typedef Carry<partial * radix> Step;
  static const uint32_t value =
      Step::bytes + BytesNeeded<remaining - 1, radix, Step::rest>::value;
};
template<uint32_t radix, uint32_t partial>
struct BytesNeeded<0, radix, partial> {
  static const uint32_t value = (partial > kOne) ? 1 : 0;
};

}  // namespace dense_internal

// num_elements: array length (e.g. 30 sequencer steps)
// num_values:   distinct values per element (e.g. 130 = 128 notes + rest + tie)
template<uint8_t num_elements, uint8_t num_values>
struct DenseArray {
  static const uint8_t kNumBytes =
      dense_internal::BytesNeeded<num_elements, num_values,
                                 dense_internal::kOne>::value;

  // Encode one element.  Call for each element from first to last.
  // buf must be zeroed before the first call.
  static void Encode(uint8_t* buf, uint8_t value) {
    uint16_t carry = value;
    for (int8_t i = kNumBytes - 1; i >= 0; i--) {
      carry += static_cast<uint16_t>(buf[i]) * num_values;
      buf[i] = carry & 0xFF;
      carry >>= 8;
    }
  }

  // Decode one element.  Call for each element from last to first
  // (last encoded = least significant digit).
  static uint8_t Decode(uint8_t* buf) {
    uint16_t remainder = 0;
    for (uint8_t i = 0; i < kNumBytes; i++) {
      remainder = (remainder << 8) | buf[i];
      buf[i] = remainder / num_values;
      remainder = remainder % num_values;
    }
    return remainder;
  }
};

// The first of these is the shape the truncating carry got wrong, asking for
// two bytes where Encode needs three; the second is the size in use, so that
// any change to the arithmetic has to be deliberate. Exhaustively checked over
// every radix and length the template can name: never short, and one byte over
// in four of some sixty-five thousand combinations.
STATIC_ASSERT((DenseArray<11, 3>::kNumBytes == 3), dense_array_carry_truncates);
STATIC_ASSERT(
  (DenseArray<30, 130>::kNumBytes == 27), dense_array_step_pitch_size
);

}  // namespace yarns

#endif  // YARNS_DENSE_ARRAY_H_
