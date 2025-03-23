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
const uint16_t kAudioBlockSize = 1 << kAudioBlockSizeBits;

const uint16_t kDacCommandWrite = 0x1000;
const uint32_t kSpiTimeout = 1000;

class Dac {
 public:
  Dac() { }
  ~Dac() { }
  
  enum Mode {
    MODE_MANUAL, // Low-frequency manual output
    MODE_DMA     // High-frequency DMA-driven output
  };
  
  void Init();
  
  // Returns true if mode was changed, false if already in specified mode
  bool SetMode(uint8_t channel, Mode mode);

  // For low-frequency mode: set single value for DAC channel
  void WriteIfManual(uint8_t channel, uint16_t value);
  
  // For low-frequency mode: write values to all channels
  void WriteAllIfManual(const uint16_t* values);
  
  // Fill half of the DMA buffer with new samples
  // Returns true if buffer was filled, false if buffer not ready
  bool FillBuffer(uint8_t channel, const uint16_t* samples, uint16_t count);

  // Check if a buffer needs filling (true = needs samples)
  bool NeedsSamples(uint8_t channel);
  
  // Called in DMA transfer half/complete ISR
  void HandleDMAIrq(uint8_t channel);
  
  void AssertCS();
  void DeassertCS();
  
 private:
  
  // Buffer structure using circular buffer with half-transfer interrupts
  struct ChannelBuffer {
    uint16_t buffer[kAudioBlockSize];     // Circular buffer for DMA
    volatile bool first_half_free;        // Is first half ready for filling
    volatile bool second_half_free;       // Is second half ready for filling
  };
  
  Mode mode_[kNumCVOutputs];
  ChannelBuffer channel_buffers_[kNumCVOutputs];
  uint16_t value_[kNumCVOutputs];      // Current value for manual mode
  
  // Initialize DMA for a channel in circular mode
  void InitDma(uint8_t channel);
  
  // Start DMA transfers for a channel
  void StartDma(uint8_t channel);
  
  // Stop DMA transfers for a channel
  void StopDma(uint8_t channel);
  
  // Format samples for DAC - add channel/command bits to 12-bit value
  inline uint16_t FormatDacWord(uint8_t channel, uint16_t value) {
    // Command: write, channel address, 12-bit value
    return kDacCommandWrite | ((kNumCVOutputs - 1 - channel) << 9) | (value & 0x0FFF);
  }
  
  // Helper to write to specific DAC channel using SPI with CS toggle
  void DirectWrite(uint8_t channel, uint16_t value);
  
  DISALLOW_COPY_AND_ASSIGN(Dac);
};

extern Dac dac;

}  // namespace yarns

#endif  // YARNS_DRIVERS_DAC_H_