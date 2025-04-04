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

const uint32_t kNumChannels = 4;
const uint32_t kDacWordsPerSample = 2;
const uint32_t kDacWordsPerFrame = kNumChannels * kDacWordsPerSample;
const uint32_t kDacWordsPerBlock = kAudioBlockSize * kDacWordsPerFrame;

const uint32_t kFrameHz = 40000;
const uint32_t kDacWordsHz = kFrameHz * kDacWordsPerFrame;

const uint16_t kPinSS = GPIO_Pin_12;
static volatile uint32_t dma_ss_high[kDacWordsPerSample] __attribute__((aligned(4))) = {0, kPinSS};
static volatile uint32_t dma_ss_low [kDacWordsPerSample] __attribute__((aligned(4))) = {0, kPinSS};

class Dac {
 public:
  Dac() { }
  ~Dac() { }
  
  void Init();
  void RestartSyncDMA();
  
  inline void PrepareWrite(uint8_t channel, uint16_t value) {
    if (value_[channel] != value) {
      value_[channel] = value;
      update_[channel] = true;
    }
  }
  
  inline void PrepareWrites(const uint16_t* values) {
    PrepareWrite(0, values[0]);
    PrepareWrite(1, values[1]);
    PrepareWrite(2, values[2]);
    PrepareWrite(3, values[3]);
  }
  
  inline void WriteIfDirty(uint8_t channel) {
    // if (update_[channel]) {
      Write(channel, value_[channel]);
      update_[channel] = false;
    // }
  }

  // Bits: 8 command | 16 data | 8 padding
  inline uint32_t FormatCommandWords(uint8_t channel, uint16_t value) {
    uint16_t dac_channel = kNumChannels - 1 - channel;
    uint16_t high = 0x1000 | (dac_channel << 9) | (value >> 8);
    uint16_t low = value << 8;
    return (high << 16) | low;
  }
  
  inline void Write(uint8_t channel, uint16_t value) {
    uint32_t words = FormatCommandWords(channel, value);
    SPI_I2S_SendData(SPI2, words >> 16);
    SPI_I2S_SendData(SPI2, words & 0xFFFF);
  }

  uint32_t timer_base_freq(uint8_t apb) const;
  uint32_t timer_period() const;
 
 private:
  bool update_[kNumChannels];
  uint16_t value_[kNumChannels];
  
  DISALLOW_COPY_AND_ASSIGN(Dac);
};

extern Dac dac;

}  // namespace yarns

#endif  // YARNS_DRIVERS_DAC_H_
