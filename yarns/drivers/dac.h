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
const uint16_t kPinSS = GPIO_Pin_12;

// New constants for circular DMA
const uint16_t kAudioBufferSize = 64;  // Size per channel, must be power of 2
const uint8_t kDmaHalfCompleteFlag = 0x01;
const uint8_t kDmaCompleteFlag = 0x02;

class Dac {
 public:
  Dac() { }
  ~Dac() { }
  
  void Init();
  void InitDMA();
  
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
  
  inline void Cycle() {
    active_channel_ = (active_channel_ + 1) % kNumChannels;
  }
  
  inline void WriteIfDirty() {
    if (update_[active_channel_]) {
      // For non-high-frequency channels, we still use the direct write
      if (!is_high_freq_[active_channel_]) {
        SendSingleValue(active_channel_, value_[active_channel_]);
      }
      update_[active_channel_] = false;
    }
  }
  
  // New methods for circular DMA
  void FillBuffer(uint8_t channel);
  void StartCircularDMA(uint8_t channel);
  void StopCircularDMA(uint8_t channel);
  
  inline uint8_t channel() { return active_channel_; }
  
  // Called from ISR to handle buffer states
  void HandleDMAComplete();
  void HandleDMAHalfComplete();
  
  // Set channel to high frequency mode (uses circular DMA)
  void SetHighFrequencyMode(uint8_t channel, bool high_freq);
  
 private:
  // For non-circular direct writes
  static const uint16_t kDMABufferSize = 2;  // Two 16-bit words per transfer
  static const uint8_t kNumBuffers = 2;
  
  struct DMABuffer {
    uint16_t data[kDMABufferSize];
    bool ready;  // Indicates buffer is prepared and ready to send
  };
  
  DMABuffer buffers_[kNumBuffers];
  volatile uint8_t active_buffer_;  // Currently transmitting buffer
  volatile uint8_t next_buffer_;    // Buffer being prepared
  
  // For circular DMA
  struct CircularDMABuffer {
    // Each entry contains formatted data for DAC: command word and data word
    uint16_t data[kAudioBufferSize * 2]; 
    volatile uint8_t state;  // Flags to indicate buffer state
  };
  
  CircularDMABuffer audio_buffer_[kNumChannels];
  volatile bool circular_dma_active_[kNumChannels];
  
  bool update_[kNumChannels];
  uint16_t value_[kNumChannels];
  bool is_high_freq_[kNumChannels];
  uint8_t active_channel_;
  volatile bool transfer_in_progress_;
  
  // Helper to send a single value to the DAC
  void SendSingleValue(uint8_t channel, uint16_t value);
  
  inline void PrepareNextBuffer() {
    // Format data for DAC in the next buffer
    uint16_t dac_channel = kNumChannels - 1 - active_channel_;
    buffers_[next_buffer_].data[0] = 0x1000 | (dac_channel << 9) | (value_[active_channel_] >> 8);
    buffers_[next_buffer_].data[1] = value_[active_channel_] << 8;
    buffers_[next_buffer_].ready = true;
  }
  
  inline void StartTransferIfNeeded() {
    if (!transfer_in_progress_ && buffers_[next_buffer_].ready) {
      // Start new transfer
      transfer_in_progress_ = true;
      active_buffer_ = next_buffer_;
      next_buffer_ = (next_buffer_ + 1) % kNumBuffers;
      buffers_[active_buffer_].ready = false;
      
      // Configure DMA for new transfer
      DMA1_Channel5->CMAR = (uint32_t)buffers_[active_buffer_].data;
      DMA1_Channel5->CNDTR = kDMABufferSize;
      
      // Toggle SS pin and start transfer
      GPIOB->BSRR = kPinSS; // Set SS pin high
      GPIOB->BRR = kPinSS;  // Set SS pin low
      DMA_Cmd(DMA1_Channel5, ENABLE);
    }
  }
  
  DISALLOW_COPY_AND_ASSIGN(Dac);
};

}  // namespace yarns

#endif  // YARNS_DRIVERS_DAC_H_