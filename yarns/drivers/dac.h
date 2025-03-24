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
const uint8_t kAudioBlockSizeBits = 6; // 64 samples
const uint8_t kAudioBlockSize = 1 << kAudioBlockSizeBits;

const uint16_t kDacCommandWrite = 0x1000;
const uint32_t kSpiTimeout = 1000;

class Dac {
 public:
  Dac() { }
  ~Dac() { }
  
  void Init();
  
  // Set channel to high-frequency (DMA) or low-frequency (manual) mode
  // Returns true if mode was changed
  bool SetHighFrequencyMode(uint8_t channel, bool high_freq);

  // For low-frequency mode: set single value for DAC channel
  void WriteManual(uint8_t channel, uint16_t value);
  
  // For low-frequency mode: write values to all channels
  void WriteAllManual(const uint16_t* values);
  
  // For high-frequency mode: get pointer to buffer half that needs filling
  // Returns nullptr if no buffer half needs filling
  uint16_t* GetBufferHalfToFill(uint8_t channel, bool* is_first_half);
  
  // For high-frequency mode: mark buffer half as filled
  void MarkBufferHalfFilled(uint8_t channel, bool is_first_half);
  
  // Called in DMA transfer half/complete ISR
  void HandleDMAIrq(uint8_t channel, bool is_half_transfer);

  // Format value for DAC - adds channel/command bits to 12-bit value
  inline uint16_t FormatDacWord(uint8_t channel, uint16_t value) const {
    return kDacCommandWrite | ((kNumCVOutputs - 1 - channel) << 9) | (value & 0x0FFF);
  }
  
 private:
  // Buffer structure for DMA circular buffer
  struct ChannelBuffer {
    uint16_t buffer[kAudioBlockSize * 2];
    volatile bool half_needs_filling[2];  // [0] = first half, [1] = second half
  };
  
  bool high_freq_mode_[kNumCVOutputs];
  ChannelBuffer buffers_[kNumCVOutputs];
  uint16_t current_value_[kNumCVOutputs];
  
  // Initialize DMA for a channel
  void InitDma(uint8_t channel);
  
  // Start DMA transfers for a channel
  void StartDma(uint8_t channel);
  
  // Stop DMA transfers for a channel
  void StopDma(uint8_t channel);
  
  // Direct SPI write to DAC
  void DirectWrite(uint8_t channel, uint16_t value);
  
  // CS control for SPI transfers
  inline void AssertCS() { GPIOB->BRR = GPIO_Pin_12; }
  inline void DeassertCS() { GPIOB->BSRR = GPIO_Pin_12; }
  
  DISALLOW_COPY_AND_ASSIGN(Dac);
};

extern Dac dac;

}  // namespace yarns

#endif  // YARNS_DRIVERS_DAC_H_