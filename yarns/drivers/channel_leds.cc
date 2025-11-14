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
// Driver for the 4 channels LEDs.

#include "yarns/drivers/channel_leds.h"

#include <stm32f10x_conf.h>

namespace yarns {

// 8000/2^(6+1) = 62.5 Hz refresh rate
// Add 1 because we do a mirrored duty cycle to prevent transition artifacts
const uint8_t kBcmBits = 6;

void ChannelLeds::Init() {
  GPIO_InitTypeDef gpio_init = {0};
  gpio_init.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_8;
  gpio_init.GPIO_Speed = GPIO_Speed_10MHz;
  gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOA, &gpio_init);
  
  gpio_init.GPIO_Pin = GPIO_Pin_14;
  gpio_init.GPIO_Speed = GPIO_Speed_10MHz;
  gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOB, &gpio_init);
  
  bcm_bit_pos_ = -1;  // Start at last bit to roll over to 0 first
  bcm_bit_pos_increment_ = 1;
  bcm_bit_countdown_ = 0;
  std::fill(&brightness_[0], &brightness_[kNumLeds], 0);
}

void ChannelLeds::Write() {
  if (bcm_bit_countdown_ > 0) {
    --bcm_bit_countdown_;
    return;
  }

  bcm_bit_pos_ += bcm_bit_pos_increment_;
  if (bcm_bit_pos_ >= kBcmBits) {
    bcm_bit_pos_ = kBcmBits - 1;
    bcm_bit_pos_increment_ = -1;
  } else if (bcm_bit_pos_ < 0) {
    bcm_bit_pos_ = 0;
    bcm_bit_pos_increment_ = 1;
  }
  bcm_bit_countdown_ = (1 << (kBcmBits - 1 - bcm_bit_pos_)) - 1;

  const uint8_t mask = 1 << (7 - bcm_bit_pos_);
  GPIOA->BSRR =
    (brightness_[0] & mask ? GPIO_Pin_12  : GPIO_Pin_12 << 16) |
    (brightness_[1] & mask ? GPIO_Pin_11  : GPIO_Pin_11 << 16) |
    (brightness_[2] & mask ? GPIO_Pin_8   : GPIO_Pin_8  << 16);
  GPIOB->BSRR =
    (brightness_[3] & mask ? GPIO_Pin_14  : GPIO_Pin_14 << 16);
}

/* extern */
ChannelLeds channel_leds;

}  // namespace yarns
