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

#include "yarns/drivers/dac.h"

#include "yarns/drivers/system.h"

#include <algorithm>

namespace yarns {

using namespace std;

const uint16_t kPinSS = GPIO_Pin_12;
volatile uint32_t dma_ss_high = kPinSS;
volatile uint32_t dma_ss_low  = kPinSS;

void Dac::Init() {
  // Initialize SS pin.
  GPIO_InitTypeDef gpio_init;
  gpio_init.GPIO_Pin = kPinSS;
  gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
  gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOB, &gpio_init);
  
  // Initialize MOSI and SCK pins.
  gpio_init.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_15;
  gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
  gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_Init(GPIOB, &gpio_init);
  
  // Initialize SPI
  SPI_InitTypeDef spi_init;
  spi_init.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
  spi_init.SPI_Mode = SPI_Mode_Master;
  spi_init.SPI_DataSize = SPI_DataSize_16b;
  spi_init.SPI_CPOL = SPI_CPOL_High;
  spi_init.SPI_CPHA = SPI_CPHA_1Edge;
  spi_init.SPI_NSS = SPI_NSS_Soft;
  spi_init.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
  spi_init.SPI_FirstBit = SPI_FirstBit_MSB;
  spi_init.SPI_CRCPolynomial = 7;
  SPI_Init(SPI2, &spi_init);
  SPI_Cmd(SPI2, ENABLE);
  
  // Initialize timers and DMA
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

  // 449 for 160kHz
  uint32_t ss_period = F_CPU / (kSampleRate * kNumCVOutputs) - 1;
  // uint32_t ss_high_period = ss_period * 18 / 20; // 90%
  // uint32_t ss_low_period  = ss_period * 19 / 20; // 95%
  uint32_t ss_high_period = ss_period - 2;
  uint32_t ss_low_period  = ss_period - 1;
  // uint32_t ss_high_period = ss_period * 6/ 8; // 75%
  // uint32_t ss_low_period  = ss_period * 7 / 8; // 87.5%

  // TIM1 (160kHz) for SYNC
  TIM_TimeBaseInitTypeDef tim1_init = {0};
  tim1_init.TIM_Prescaler = 0;
  tim1_init.TIM_Period = ss_period;
  tim1_init.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM1, &tim1_init);
  
  TIM_OCInitTypeDef oc_init = {0};
  oc_init.TIM_OCMode = TIM_OCMode_Timing;
  oc_init.TIM_OutputState = TIM_OutputState_Disable;
  
  oc_init.TIM_Pulse = ss_high_period;
  TIM_OC1Init(TIM1, &oc_init);
  
  oc_init.TIM_Pulse = ss_low_period;
  TIM_OC2Init(TIM1, &oc_init);
  
  // TODO timer sync disabled for now
  TIM_SelectMasterSlaveMode(TIM1, TIM_MasterSlaveMode_Enable);
  TIM_SelectOutputTrigger(TIM1, TIM_TRGOSource_Update);

  // TIM2 (320kHz) for DAC data, slaved to TIM1
  TIM_TimeBaseInitTypeDef data_timer = {0};
  data_timer.TIM_Prescaler = 0;
  const uint32_t half_sync_period = (ss_period + 1) / 2; // 320kHz
  const uint32_t dac_period = half_sync_period * 17/16 - 1; // Reduce freq to ~300kHz
  data_timer.TIM_Period = dac_period; 
  data_timer.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM2, &data_timer);
  
  // TODO timer sync disabled for now
  TIM_SelectInputTrigger(TIM2, TIM_TS_ITR0); // TIM1 → TIM2 sync
  TIM_SelectSlaveMode(TIM2, TIM_SlaveMode_Reset);
  
  // // Compare channel for DMA trigger
  // // TODO why use this instead of TIM_DMA_Update ?
  // TIM_OC1Init(TIM2, &oc_init);
  // TIM_SetCompare1(TIM2, 0);
  // TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Disable);
  // TIM_ARRPreloadConfig(TIM2, ENABLE);
  
  DMA_InitTypeDef ss_dma = {0};
  ss_dma.DMA_DIR = DMA_DIR_PeripheralDST;
  ss_dma.DMA_BufferSize = 1;
  ss_dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  ss_dma.DMA_MemoryInc = DMA_MemoryInc_Disable;
  ss_dma.DMA_M2M = DMA_M2M_Disable;
  ss_dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
  ss_dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
  ss_dma.DMA_Mode = DMA_Mode_Circular;
  ss_dma.DMA_Priority = DMA_Priority_High;

  // DMA for SYNC High (TIM1_CH1)
  DMA_InitTypeDef high_ss_dma = ss_dma;
  high_ss_dma.DMA_PeripheralBaseAddr = (uint32_t)&GPIOB->BSRR;
  high_ss_dma.DMA_MemoryBaseAddr = (uint32_t)&dma_ss_high;
  DMA_Init(DMA1_Channel2, &high_ss_dma);

  // DMA for SYNC Low (TIM1_CH2)
  DMA_InitTypeDef low_ss_dma = ss_dma;
  low_ss_dma.DMA_PeripheralBaseAddr = (uint32_t)&GPIOB->BRR;
  low_ss_dma.DMA_MemoryBaseAddr = (uint32_t)&dma_ss_low;
  DMA_Init(DMA1_Channel3, &low_ss_dma);

  // DMA for SPI (TIM2_CH1)
  DMA_InitTypeDef spi_dma = {0};
  spi_dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
  spi_dma.DMA_MemoryBaseAddr = (uint32_t)&buffer_[0];
  spi_dma.DMA_DIR = DMA_DIR_PeripheralDST;
  spi_dma.DMA_BufferSize = sizeof(buffer_) / sizeof(uint16_t);
  spi_dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  spi_dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
  spi_dma.DMA_M2M = DMA_M2M_Disable;
  spi_dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
  spi_dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
  spi_dma.DMA_Mode = DMA_Mode_Circular;
  spi_dma.DMA_Priority = DMA_Priority_VeryHigh;
  DMA_Init(DMA1_Channel5, &spi_dma);

  SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);

  TIM_DMACmd(TIM1, TIM_DMA_CC1 | TIM_DMA_CC2, ENABLE);
  TIM_DMACmd(TIM2, TIM_DMA_Update, ENABLE);

  DMA_ITConfig(DMA1_Channel5, DMA_IT_TC | DMA_IT_HT, ENABLE);
  NVIC_InitTypeDef nvic_init;
  nvic_init.NVIC_IRQChannel = DMA1_Channel5_IRQn;
  nvic_init.NVIC_IRQChannelPreemptionPriority = 1;
  nvic_init.NVIC_IRQChannelSubPriority = 0;
  nvic_init.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&nvic_init);

  DMA_Cmd(DMA1_Channel2, ENABLE);
  DMA_Cmd(DMA1_Channel3, ENABLE);
  DMA_Cmd(DMA1_Channel5, ENABLE);

  TIM_Cmd(TIM1, ENABLE);
  TIM_Cmd(TIM2, ENABLE);

  can_fill_ = true;
  fillable_buffer_half_ = 1; // DMA will be consuming the first half
}

/* extern */
Dac dac;

}  // namespace yarns