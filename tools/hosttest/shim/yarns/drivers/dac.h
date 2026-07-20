#ifndef YARNS_DRIVERS_DAC_H_
#define YARNS_DRIVERS_DAC_H_
#include "stmlib/stmlib.h"
namespace yarns {
const size_t kAudioBlockSizeBits = 6;
const size_t kAudioBlockSize = 1 << kAudioBlockSizeBits;
const uint32_t kFrameHz = 45000;
}
#endif
