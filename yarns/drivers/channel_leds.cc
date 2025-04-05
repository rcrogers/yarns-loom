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

// Because we increment by 16, there are only 256/16 = 16 levels of brightness,
// but the PWM cycle is fast enough to achieve 1000/16 = 62.5 Hz refresh rate.
const uint8_t kLedPwmIncrement = 16;

void ChannelLeds::Init() {
  GPIO_InitTypeDef gpio_init;
  gpio_init.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_8;
  gpio_init.GPIO_Speed = GPIO_Speed_10MHz;
  gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOA, &gpio_init);
  
  gpio_init.GPIO_Pin = GPIO_Pin_14;
  gpio_init.GPIO_Speed = GPIO_Speed_10MHz;
  gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOB, &gpio_init);
  
  pwm_counter_ = 0;
  std::fill(&brightness_[0], &brightness_[kNumLeds], 0);
  std::fill(&on_[0], &on_[kNumLeds], false);
}

void ChannelLeds::Write() {
  pwm_counter_ += kLedPwmIncrement;

  bool changed[kNumLeds] = {0};
  for (size_t i = 0; i < kNumLeds; ++i) {
    bool on_before = on_[i];
    on_[i] = brightness_[i] > pwm_counter_;
    changed[i] = on_[i] != on_before;
  }

  uint32_t gpioa_bsrr =
    changed[0] ? (on_[0] ? GPIO_Pin_12 : GPIO_Pin_12 << 16) : 0 |
    changed[1] ? (on_[1] ? GPIO_Pin_11 : GPIO_Pin_11 << 16) : 0 |
    changed[2] ? (on_[2] ? GPIO_Pin_8 : GPIO_Pin_8 << 16) : 0;
  if (gpioa_bsrr) GPIOA->BSRR = gpioa_bsrr;

  uint32_t gpiob_bsrr =
    changed[3] ? (on_[3] ? GPIO_Pin_14 : GPIO_Pin_14 << 16) : 0;
  if (gpiob_bsrr) GPIOB->BSRR = gpiob_bsrr;
}

}  // namespace yarns
