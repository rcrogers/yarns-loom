// Copyright 2013 Emilie Gillet.
// Copyright 2020 Chris Rogers.
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
// Driver for 4x14-segments display.

#include "yarns/drivers/display.h"

#include <stm32f10x_conf.h>
#include <string.h>

#include "yarns/resources.h"

namespace yarns {

const uint16_t kPinClk = GPIO_Pin_7;
const uint16_t kPinEnable = GPIO_Pin_8;
const uint16_t kPinData = GPIO_Pin_9;

const uint16_t kScrollingDelay = 180;
const uint16_t kScrollingPreDelay = 600;
const uint16_t kBlinkMask = 128;

// PWM >6 bits causes visible flickering due to over-long PWM cycle at 8kHz
const uint8_t kDisplayBrightnessPWMBits = 6;
const uint8_t kDisplayBrightnessPWMMax = 1 << kDisplayBrightnessPWMBits;

const uint16_t kCharacterEnablePins[] = {
  GPIO_Pin_6,
  GPIO_Pin_5
};

void Display::Init() {
  GPIO_InitTypeDef gpio_init;
  gpio_init.GPIO_Pin = kPinClk;
  gpio_init.GPIO_Pin |= kPinEnable;
  gpio_init.GPIO_Pin |= kPinData;
  gpio_init.GPIO_Pin |= kCharacterEnablePins[0];
  gpio_init.GPIO_Pin |= kCharacterEnablePins[1];
  
  gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
  gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOB, &gpio_init);

  GPIOB->BSRR = kPinEnable;
  active_position_ = 0;
  brightness_pwm_cycle_ = 0;
  memset(short_buffer_, ' ', kDisplayWidth);
  memset(long_buffer_, ' ', kScrollBufferSize);
  use_mask_ = false;
  fading_counter_ = 0;
  fading_increment_ = 0;
  
  blinking_ = false;
  brightness_ = UINT16_MAX;
}

void Display::Scroll() {
  if (long_buffer_size_ > kDisplayWidth) {
    scrolling_ = true;
    scrolling_step_ = 0;
    scrolling_timer_ = kScrollingDelay;
    scrolling_pre_delay_timer_ = kScrollingPreDelay;
  }
}

void Display::set_brightness(uint16_t fraction) {
  // Applying a brightness fraction naively to PWM results in a visual bias
  // toward over-brightness -- expo conversion biases it back toward darkness
  uint8_t phase = UINT8_MAX - (fraction >> 8);
  brightness_ = UINT16_MAX - lut_env_expo[(phase >> 1) + (phase >> 2)];
}

void Display::RefreshSlow() {
#ifdef APPLICATION
  if (scrolling_) {
    if (scrolling_pre_delay_timer_) {
      --scrolling_pre_delay_timer_;
    } else {
      --scrolling_timer_;
      if (scrolling_timer_ == 0) {
        ++scrolling_step_;
        if (scrolling_step_ > long_buffer_size_ - kDisplayWidth + 1) {
          scrolling_ = false;
        }
        scrolling_timer_ = kScrollingDelay;
      }
    }
  }

  displayed_buffer_ = (scrolling_ && !scrolling_pre_delay_timer_)
      ? long_buffer_ + scrolling_step_
      : short_buffer_;

  if (fading_increment_) {
    fading_counter_ += fading_increment_;
  }
  
  if (fading_increment_) {
    actual_brightness_ = (fading_counter_ >> 1) + (fading_counter_ >> 2) + (1 << 14);
  } else {
    actual_brightness_ = brightness_;
  }
  blink_counter_ = (blink_counter_ + 1) % (kBlinkMask << 1);
  std::fill(&redraw_[0], &redraw_[kDisplayWidth], true); // Force redraw

#else

  displayed_buffer_ = short_buffer_;
  actual_brightness_ = UINT16_MAX;

#endif  // APPLICATION
  // Pre-scale for PWM comparator
  actual_brightness_ = actual_brightness_ >> (16 - kDisplayBrightnessPWMBits);
}

void Display::RefreshFast() {
  if (brightness_pwm_cycle_ == 0) {
    // On rising edge, switch to next display position and draw it
    GPIOB->BRR = kCharacterEnablePins[active_position_];
    active_position_ = (active_position_ + 1) % kDisplayWidth;
    redraw_[active_position_] = true;
  } else if (brightness_pwm_cycle_ - 1 == actual_brightness_) {
    // On falling edge, undraw current display position
    redraw_[active_position_] = true;
  }
  if (redraw_[active_position_]) {
    redraw_[active_position_] = false;
    if (brightness_pwm_cycle_ <= actual_brightness_
        && (!blinking_ || blink_counter_ < kBlinkMask)) {
      if (use_mask_) {
        Shift14SegmentsWord(mask_[active_position_]);
      } else {
        Shift14SegmentsWord(chr_characters[
          static_cast<uint8_t>(displayed_buffer_[active_position_])]);
      }
      GPIOB->BSRR = kCharacterEnablePins[active_position_];
    } else {
      GPIOB->BRR = kCharacterEnablePins[active_position_];
    }
  }
  brightness_pwm_cycle_ = (brightness_pwm_cycle_ + 1) % kDisplayBrightnessPWMMax;
}

void Display::Print(const char* short_buffer, const char* long_buffer) {
  strncpy(short_buffer_, short_buffer, kDisplayWidth);

#ifdef APPLICATION
  strncpy(long_buffer_, long_buffer, kScrollBufferSize);
  long_buffer_size_ = strlen(long_buffer_);
#endif  // APPLICATION

  scrolling_ = false;
  use_mask_ = false;
}

# define SHIFT_BIT \
  GPIOB->BRR = kPinClk; \
  if (data & 1) { \
    GPIOB->BSRR = kPinData; \
  } else { \
    GPIOB->BRR = kPinData; \
  } \
  data >>= 1; \
  GPIOB->BSRR = kPinClk;

void Display::Shift14SegmentsWord(uint16_t data) {
  GPIOB->BRR = kPinEnable;
  for (int i = 0; i < 2; ++i) {
#ifdef APPLICATION
    SHIFT_BIT
    SHIFT_BIT
    SHIFT_BIT
    SHIFT_BIT
    SHIFT_BIT
    SHIFT_BIT
    SHIFT_BIT
    SHIFT_BIT
#else
    for (int j = 0; j < 8; ++j) {
      SHIFT_BIT
    }
#endif  // APPLICATION
  }
  GPIOB->BSRR = kPinEnable;
}

}  // namespace yarns
