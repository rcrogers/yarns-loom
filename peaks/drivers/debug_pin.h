// Copyright 2012 Olivier Gillet.
//
// Author: Olivier Gillet (pichenettes@mutable-instruments.net)
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
// Driver for the debug (timing) pin.

#ifndef PEAKS_DRIVERS_DEBUG_PIN_H_
#define PEAKS_DRIVERS_DEBUG_PIN_H_

#include <stm32f10x_conf.h>
#include "stmlib/stmlib.h"

namespace peaks {

class DebugPin {
 public:
  DebugPin() { }
  ~DebugPin() { }
  
  static void Init() {
    GPIO_InitTypeDef gpio_init;
    gpio_init.GPIO_Pin = GPIO_Pin_10;
    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
    gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &gpio_init);
    GPIOA->BSRR = GPIO_Pin_10;
  }
  
  static void High() {
    GPIOA->BSRR = GPIO_Pin_10;
  }

  static void Low() {
    GPIOA->BRR = GPIO_Pin_10;
  }
  
 private:
   DISALLOW_COPY_AND_ASSIGN(DebugPin);
};

}  // namespace peaks

#endif  // PEAKS_DRIVERS_DEBUG_PIN_H_
