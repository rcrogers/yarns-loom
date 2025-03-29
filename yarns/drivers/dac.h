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

const uint8_t kNumCVOutputs = 4;

// Number of samples per channel that will be filled at once
const uint8_t kAudioBlockSizeBits = 6;
const uint8_t kAudioBlockSize = 1 << kAudioBlockSizeBits;

const uint8_t kDacValuesPerSample = 2; // 2x 16-bit values per sample
const uint8_t kNumBuffers = 2; // Double buffer

// Frame: one 40kHz tick's worth of DAC data for all channels
const size_t kFrameSize = kNumCVOutputs * kDacValuesPerSample;

const size_t kDacBufferSize = kNumBuffers * kAudioBlockSize * kFrameSize;

class Dac {
 public:
  Dac() { }
  ~Dac() { }
  
  void Init();

  volatile uint8_t* FillableBufferHalf() {
    return can_fill_ ? &fillable_buffer_half_ : NULL;
  }
  
  void OnHalfBufferConsumed(bool first_half) {
    can_fill_ = true;
    fillable_buffer_half_ = first_half ? 0 : 1;
    __DMB();
  }
  
  // Call after filling all channels
  inline void OnHalfBufferFilled() { can_fill_ = false; }

  // Pack 2 16-bit DMA/SPI words into a 32-bit value
  uint32_t FormatDacWords(uint8_t channel, uint16_t sample);
  void BufferSamples(uint8_t buffer_half, uint8_t channel, uint16_t* samples);
  void BufferStaticSample(uint8_t buffer_half, uint8_t channel, uint16_t sample);
 
 private:
  // Multipliers express the time-ordering of the buffer: half-buffer, sample, channel, word
  // Channels must be interleaved so they output at a consistent phase of each 40kHz tick
  volatile uint16_t buffer_[kDacBufferSize] __attribute__((aligned(4)));
  volatile uint8_t fillable_buffer_half_;
  volatile bool can_fill_;
  
  DISALLOW_COPY_AND_ASSIGN(Dac);
};

extern Dac dac;

}  // namespace yarns

#endif  // YARNS_DRIVERS_DAC_H_