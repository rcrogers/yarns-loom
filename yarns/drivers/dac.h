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

class Dac {
 public:
  Dac() { }
  ~Dac() { }
  
  void Init();
  
  inline void set_channel(uint8_t channel, uint16_t value) {
    if (value_[channel] != value) {
      value_[channel] = value;
      update_[channel] = true;
    }
  }
  
  inline void Write(const uint16_t* values) {
    set_channel(0, values[0]);
    set_channel(1, values[1]);
    set_channel(2, values[2]);
    set_channel(3, values[3]);
  }
  
 private:
  bool update_[kNumChannels];
  uint16_t value_[kNumChannels];
  uint16_t dac_buffer_[8]; // 4 channels × 2 words
  
  DISALLOW_COPY_AND_ASSIGN(Dac);
};

}  // namespace yarns

#endif  // YARNS_DRIVERS_DAC_H_