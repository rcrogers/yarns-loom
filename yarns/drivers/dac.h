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
const uint16_t kDmaBufSize = 64; // Match your block size

class Dac {
 public:
  Dac() { }
  ~Dac() { }
  
  enum Mode {
    MODE_MANUAL, // Low-frequency manual output
    MODE_DMA     // High-frequency DMA-driven output
  };
  
  void Init();
  
  // Set channel output mode (high-frequency DMA or low-frequency manual)
  // Returns true if mode was changed, false if already in specified mode
  bool SetMode(uint8_t channel, Mode mode);

  // For low-frequency mode: set single value for DAC channel
  inline void set_channel(uint8_t channel, uint16_t value) {
    if (channel >= kNumChannels || mode_[channel] != MODE_MANUAL) {
      return;
    }
    
    if (value_[channel] != value) {
      value_[channel] = value;
      WriteDacChannel(channel, value);
    }
  }
  
  // For low-frequency mode: write values to all channels
  inline void Write(const uint16_t* values) {
    for (uint8_t i = 0; i < kNumChannels; ++i) {
      set_channel(i, values[i]);
    }
  }
  
  // For high-frequency mode: check if buffer is ready to be filled
  bool IsBufferReady(uint8_t channel);
  
  // For high-frequency mode: fill the next DMA buffer with samples
  // Returns true if buffer was filled, false if buffer wasn't ready
  bool FillBuffer(uint8_t channel, const uint16_t* samples, uint16_t count);
  
  // Called in DMA transfer complete ISR
  void HandleDmaInterrupt(uint8_t channel);
  
 private:
  // Internal buffer state for each channel
  struct ChannelBuffer {
    uint16_t buffer[2][kDmaBufSize];  // Double buffer
    volatile uint8_t write_index;     // Which buffer to write to (0 or 1)
    volatile uint8_t active_index;    // Which buffer DMA is reading from
    volatile bool buffer_ready;       // Is the inactive buffer ready to be filled
  };
  
  Mode mode_[kNumChannels];
  ChannelBuffer channel_buffers_[kNumChannels];
  uint16_t value_[kNumChannels];      // Current value for manual mode
  
  // Start DMA transfers for a channel
  void StartDma(uint8_t channel);
  
  // Stop DMA transfers for a channel
  void StopDma(uint8_t channel);
  
  // Configure DMA for specific channel
  void ConfigureDma(uint8_t channel);
  
  // Helper to write to specific DAC channel using SPI with CS toggle
  inline void WriteDacChannel(uint8_t channel, uint16_t value) {
    // Assert CS
    GPIOB->BRR = GPIO_Pin_12;
    
    // Format the command: 0x1000 | (channel << 9) | (value >> 8)
    uint16_t dac_channel = kNumChannels - 1 - channel;
    uint16_t word = 0x1000 | (dac_channel << 9) | (value & 0x0FFF);
    
    // Send the data
    SPI_I2S_SendData(SPI2, word);
    
    // Wait for transmission to complete
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET);
    
    // Deassert CS
    GPIOB->BSRR = GPIO_Pin_12;
  }
  
  DISALLOW_COPY_AND_ASSIGN(Dac);
};

}  // namespace yarns

#endif  // YARNS_DRIVERS_DAC_H_