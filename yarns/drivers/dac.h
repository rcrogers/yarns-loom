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

const uint8_t kNumChannels = 4;
const uint8_t kAudioBlockSize = 64; // Number of samples per channel that will be filled at once
const uint8_t kDacValuesPerSample = 2; // 2x 16-bit values per sample
const uint8_t kNumBuffers = 2; // Double buffer

class Dac {
 public:
  Dac() { }
  ~Dac() { }
  
  void Init();

  bool CanFill() const { return can_fill_; }
  
  void OnDmaReadComplete() { can_fill_ = true; }
  
  // Call after filling all channels
  inline void OnFillComplete() { 
    can_fill_ = false;
    fillable_buffer_ ^= 1; // Toggle buffer
  }

  // Pack 2 16-bit DMA/SPI words into a 32-bit value
  inline uint32_t FormatDacValues(uint8_t channel, uint16_t sample) {
    uint16_t dac_channel = kNumChannels - 1 - channel;
    uint16_t high = 0x1000 | (dac_channel << 9) | (sample >> 8);
    uint16_t low = sample << 8;
    return (static_cast<uint32_t>(high) << 16) | low;
  }

  inline void BufferSamples(uint8_t channel, uint16_t* samples) {
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      uint32_t dac_values = FormatDacValues(channel, samples[i]);
      buffer_[fillable_buffer_][i][channel][0] = dac_values >> 16;
      buffer_[fillable_buffer_][i][channel][1] = dac_values & 0xFFFF;
    }
  }

  inline void BufferStaticSample(uint8_t channel, uint16_t sample) {
    uint32_t dac_values = FormatDacValues(channel, sample);
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      buffer_[fillable_buffer_][i][channel][0] = dac_values >> 16;
      buffer_[fillable_buffer_][i][channel][1] = dac_values & 0xFFFF;
    }
  }
 
 private:
  uint16_t buffer_[kNumBuffers][kAudioBlockSize][kNumChannels][kDacValuesPerSample];
  volatile uint8_t fillable_buffer_;
  volatile bool can_fill_;
  
  DISALLOW_COPY_AND_ASSIGN(Dac);
};

}  // namespace yarns

#endif  // YARNS_DRIVERS_DAC_H_
