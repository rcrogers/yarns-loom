#ifndef YARNS_CONSTANTS_H_
#define YARNS_CONSTANTS_H_

#include "stmlib/stmlib.h"

namespace yarns {

const uint32_t kSysTickHz = 8000;
const uint32_t kRefreshHz = 4000;

// MIDI values are 7-bit, which is also as wide as any setting gets.
const uint8_t kNumMidiValues = 128;

}  // namespace yarns

#endif  // YARNS_CONSTANTS_H_
