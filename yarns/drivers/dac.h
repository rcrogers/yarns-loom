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

const uint32_t kFrameHz = 50000;
const uint32_t kDacWordsHz = kFrameHz * kDacWordsPerFrame;

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

  // Bits: 8 command | 16 data | 8 padding
  inline uint32_t FormatCommandWords(uint8_t channel, uint16_t value) const {
    uint16_t dac_channel = kNumCVOutputs - 1 - channel;
    uint16_t high = 0x1000 | (dac_channel << 9) | (value >> 8);
    uint16_t low = value << 8;
    return (high << 16) | low;
  }

  void BufferSamples(uint8_t block, uint8_t channel, int16_t* samples);
  void BufferStaticSample(uint8_t block, uint8_t channel, int16_t sample);

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
