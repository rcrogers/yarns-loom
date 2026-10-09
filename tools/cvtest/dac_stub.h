#ifndef YARNS_TOOLS_CVTEST_DAC_STUB_H_
#define YARNS_TOOLS_CVTEST_DAC_STUB_H_
#include "yarns/drivers/dac.h"
namespace yarns {
extern int16_t g_dac_block[kNumCVOutputs][kAudioBlockSize];
extern bool g_dac_noop[kNumCVOutputs];
}
#endif
