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
// partial tracks the fractional product within the current byte; when
// partial * radix >= 256, a byte is emitted.
template<uint32_t remaining, uint32_t radix, uint32_t partial>
struct BytesNeeded {
  static const bool emit = (partial * radix >= 256);
  static const uint32_t next_partial = emit
      ? (partial * radix / 256) : (partial * radix);
  static const uint32_t value = (emit ? 1 : 0)
      + BytesNeeded<remaining - 1, radix, next_partial>::value;
};
template<uint32_t radix, uint32_t partial>
struct BytesNeeded<0, radix, partial> {
  static const uint32_t value = (partial > 1) ? 1 : 0;
};

}  // namespace dense_internal

// num_elements: array length (e.g. 30 sequencer steps)
// num_values:   distinct values per element (e.g. 130 = 128 notes + rest + tie)
template<uint8_t num_elements, uint8_t num_values>
struct DenseArray {
  static const uint8_t kNumBytes =
      dense_internal::BytesNeeded<num_elements, num_values, 1>::value;

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

}  // namespace yarns

#endif  // YARNS_DENSE_ARRAY_H_
