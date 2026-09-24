// Copyright 2013 Emilie Gillet.
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
// Driver for DAC.

#ifndef YARNS_DRIVERS_DAC_H_
#define YARNS_DRIVERS_DAC_H_

#include "stmlib/stmlib.h"

#include <stm32f10x_conf.h>

namespace yarns {

const size_t kAudioBlockSizeBits = 6;
const size_t kAudioBlockSize = 1 << kAudioBlockSizeBits;

const uint8_t kNumCVOutputs = 4;
const uint8_t kDacWordsPerSampleBits = 1;
const uint8_t kDacWordsPerSample = 1 << kDacWordsPerSampleBits;
const uint8_t kNumBlocks = 2;

const uint32_t kDacWordsPerFrame = kNumCVOutputs * kDacWordsPerSample;
const uint32_t kDacWordsPerBlock = kAudioBlockSize * kDacWordsPerFrame;
const uint32_t kBufferSize = kNumBlocks * kDacWordsPerBlock;

const uint32_t kFrameHz = 45000;
const uint32_t kDacWordsHz = kFrameHz * kDacWordsPerFrame;
const uint32_t kTotalFrames = kAudioBlockSize * kNumBlocks;

// DAC8564 address-mismatch NOOP: setting DB23:DB22 to nonzero (vs A1=A0=GND)
// causes the DAC to ignore the entire frame, holding its current output.
const uint16_t kNoopHighWord = 0xC000;
const uint16_t kNoopLowWord = 0x0000;
// 32-bit little-endian view of the (kNoopHighWord, kNoopLowWord) pair:
// memory layout is [high LSB, high MSB, low LSB, low MSB] = [00, C0, 00, 00].
const uint32_t kNoopPacked =
    static_cast<uint32_t>(kNoopHighWord) |
    (static_cast<uint32_t>(kNoopLowWord) << 16);

// Frames ahead of the DMA cursor to place DC injections.
// 1 frame = 8 DMA words = ~3200 CPU cycles of margin. Wildly conservative.
const size_t kInjectGapFrames = 1;

class Dac {
 public:
  Dac() { }
  ~Dac() { }
  
  void Init();
  
  uint8_t* PtrToFillableBlockNum() {
    uint8_t* res = can_fill_ ? &fillable_block_ : NULL;
    can_fill_ = false;
    return res;
  }

  void OnBlockConsumed(bool first_block_consumed) {
    can_fill_ = true;
    fillable_block_ = first_block_consumed ? 0 : 1;
  }

  // Format a (high16, low16) DAC command pair as a single 32-bit word, laid
  // out for a direct little-endian store into the volatile uint16_t SPI
  // buffer (ptr[0] = high16, ptr[1] = low16).
  //
  // Semantic frame: 8-bit command | 16-bit data | 8-bit padding.
  //   high16 = 0x1000 | (dac_channel << 9) | (value >> 8)
  //   low16  = (value & 0xFF) << 8
  //
  // Packing as a uint32 in LE memory order:
  //   bits  0-7  : value high byte         (high16 LSB)
  //   bits  8-15 : 0x10 | (dac_channel<<1) (high16 MSB, the command byte)
  //   bits 16-23 : 0                       (low16 LSB, the padding byte)
  //   bits 24-31 : value low byte          (low16 MSB)
  //
  // (value >> 8) | (value << 24) is the ROR-by-8 pattern; GCC fuses it
  // into a single ROR instruction. Saves a store vs separate STRH pair.
  inline uint32_t FormatCommandWord(uint8_t channel, uint16_t value) const {
    const uint16_t dac_channel = kNumCVOutputs - 1 - channel;
    const uint32_t cmd_const =
        static_cast<uint32_t>(0x1000) | (dac_channel << 9);
    const uint32_t v = value;
    return cmd_const | (v >> 8) | (v << 24);
  }

  void BufferSamples(uint8_t block, uint8_t channel, int16_t* samples);
  void BufferStaticSample(uint8_t block, uint8_t channel, int16_t sample);

  // Low-latency DC output path. SysTick calls UpdateDC to write this channel
  // in frame 0 of the fillable block and inject near the DMA cursor. The main
  // loop calls FillDCNoops to write NOOPs to frames 1-63 (erasing stale
  // injections).
  void UpdateDC(uint8_t channel, uint16_t sample);
  void FillDCNoops(uint8_t block, uint8_t channel);

  uint32_t timer_base_freq(uint8_t apb) const;
  uint32_t timer_period() const;

  // Multipliers express the time-ordering of the buffer: block, frame, channel, word
  // Channels must be interleaved so they output at a consistent phase of each 40kHz tick
  volatile uint16_t spi_tx_buffer_[kBufferSize] __attribute__((aligned(4)));
  uint8_t fillable_block_;
  bool can_fill_;
 
 private:
  DISALLOW_COPY_AND_ASSIGN(Dac);
};

extern Dac dac;

}  // namespace yarns

#endif  // YARNS_DRIVERS_DAC_H_
