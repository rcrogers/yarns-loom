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
// System level initialization.

#include "yarns/drivers/system.h"

#include <stm32f10x_conf.h>

#include "yarns/drivers/dac.h"

namespace yarns {

void System::Init() {
  SystemInit();
  
  NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x1000);

  RCC_APB2PeriphClockCmd(
    RCC_APB2Periph_GPIOA |
    RCC_APB2Periph_GPIOB |
    RCC_APB2Periph_GPIOC |
    RCC_APB2Periph_TIM1 |
    RCC_APB2Periph_USART1,
    ENABLE
  );
  RCC_APB1PeriphClockCmd(
    RCC_APB1Periph_SPI2,
    ENABLE
  );
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);  // 2.2 priority split.
    
  // Reduce SysTick priority to below DAC interrupt
  NVIC_InitTypeDef timer_interrupt;
  timer_interrupt.NVIC_IRQChannel = SysTick_IRQn;
  timer_interrupt.NVIC_IRQChannelPreemptionPriority = 1;
  timer_interrupt.NVIC_IRQChannelSubPriority = 1;
  timer_interrupt.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&timer_interrupt);
}

void System::StartTimers() {
  SysTick_Config(F_CPU / 8000);
}

}  // namespace yarns
