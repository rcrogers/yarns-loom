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
// Driver for 2x14-segments display.

#ifndef YARNS_DRIVERS_DISPLAY_H_
#define YARNS_DRIVERS_DISPLAY_H_

#include <cmath>
#include <algorithm>
#include "stmlib/stmlib.h"

namespace yarns {

const uint8_t kDisplayWidth = 2;
const uint8_t kScrollBufferSize = 32;

// Applying a brightness fraction naively to PWM results in a visual bias toward
// over-brightness -- exponentiating the fraction biases it back toward darkness
const uint8_t kDisplayBrightnessLinearizingExponent = 2;
// Max number of bits that, when exponentiated, will fit in a uint32_t
const uint8_t kDisplayBrightnessLinearizingBaseBits = 32 / kDisplayBrightnessLinearizingExponent;
// Max number of bits used by the result of exponentiation
const uint8_t kDisplayBrightnessLinearizingPowerBits = kDisplayBrightnessLinearizingExponent * kDisplayBrightnessLinearizingBaseBits;

class Display {
 public:
  Display() { }
  ~Display() { }
  
  void Init();
  void RefreshSlow();
  void RefreshFast();
  
  inline void Print(const char* string) {
    Print(string, string);
  }
  void Print(const char* short_string, const char* long_string);

  inline void PrintMasks(const uint16_t* masks) {
    std::copy(&masks[0], &masks[kDisplayWidth], &mask_[0]);
    use_mask_ = true;
  }
  
  char* mutable_buffer() { return short_buffer_; }
  void set_brightness(uint16_t fraction) {
    uint16_t base = fraction >> (16 - kDisplayBrightnessLinearizingBaseBits);
    uint32_t power = base * base;
    brightness_ = power >> (kDisplayBrightnessLinearizingPowerBits - 16);
  }
  void Scroll();
  
  inline bool scrolling() const { return scrolling_; }
  inline void set_blink(bool blinking) { blinking_ = blinking; }
  inline void set_fade(uint16_t increment) { // Applied at 1kHz
    fading_increment_ = increment;
  }
 
 private:
  void Shift14SegmentsWord(uint16_t data);

  char short_buffer_[kDisplayWidth];
  char long_buffer_[kScrollBufferSize];
  char* displayed_buffer_;
  uint16_t mask_[kDisplayWidth];
  bool use_mask_;
  uint8_t long_buffer_size_;
  uint16_t actual_brightness_;

  bool scrolling_;
  bool blinking_;
  
  uint16_t scrolling_pre_delay_timer_;
  uint16_t scrolling_timer_;
  uint16_t fading_counter_;
  uint16_t fading_increment_;
  uint8_t scrolling_step_;
  
  uint16_t active_position_;
  uint16_t brightness_pwm_cycle_;
  uint16_t brightness_;
  bool redraw_[kDisplayWidth];
  uint16_t blink_counter_;
  
  DISALLOW_COPY_AND_ASSIGN(Display);
};

}  // namespace yarns

#endif  // YARNS_DRIVERS_DISPLAY_H_
