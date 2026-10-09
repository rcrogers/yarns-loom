// The DAC, off target. yarns/drivers/dac.cc is the SPI and DMA driver and
// nothing else here wants it, so this defines the three symbols
// CVOutput::RenderSamples reaches and records what it wrote.
//
// This is the observation point for the CV output path: the firmware's last
// step before the wire.
#define TEST 1
#include "yarns/drivers/dac.h"
#include <cstring>

namespace yarns {

Dac dac;

// Per channel, the samples of the last block buffered, and whether the block
// was a NOOP fill instead.
int16_t g_dac_block[kNumCVOutputs][kAudioBlockSize];
bool g_dac_noop[kNumCVOutputs];

void Dac::BufferSamples(uint8_t block, uint8_t channel, int16_t* samples) {
  g_dac_noop[channel] = false;
  memcpy(g_dac_block[channel], samples, sizeof(g_dac_block[0]));
}

void Dac::FillDCNoops(uint8_t block, uint8_t channel) {
  g_dac_noop[channel] = true;
}

}  // namespace yarns
