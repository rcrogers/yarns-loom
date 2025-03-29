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

#include "yarns/multi.h"

#include <algorithm>

namespace yarns {

using namespace std;

const uint16_t kPinSS = GPIO_Pin_12;
volatile uint32_t dma_ss_high = kPinSS;
volatile uint32_t dma_ss_low  = kPinSS;

void Dac::Init() {
  can_fill_ = true;
  fillable_buffer_half_ = 1; // DMA will initially be consuming the first half
  std::fill(&buffer_[0], &buffer_[kDacBufferSize], 0);
  __DMB();

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
  __DSB();
  
  // Initialize timers and DMA
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

  // RCC_ClocksTypeDef rcc_clocks;
  // RCC_GetClocksFreq(&rcc_clocks);
  // uint32_t freq_pclk1 = rcc_clocks.PCLK1_Frequency;
  // uint32_t apb1_prescaler = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;

  // 449 for 160kHz
  uint32_t ss_period = 72000000 / (kSampleRate * kNumCVOutputs) - 1;

  // uint32_t ss_high_period = ss_period * 18 / 20; // 90%
  // uint32_t ss_low_period  = ss_period * 19 / 20; // 95%

  // Loudest garbage yet
  // uint32_t ss_high_period = ss_period - 3;
  // uint32_t ss_low_period  = ss_period - 2;
  
  uint32_t ss_high_period = ss_period * 6 / 8; // 75%
  uint32_t ss_low_period  = ss_period * 7 / 8; // 87.5%

  // TIM1 (160kHz) for SYNC
  TIM_TimeBaseInitTypeDef tim1_init = {0};
  tim1_init.TIM_Prescaler = 0;
  tim1_init.TIM_Period = ss_period;
  tim1_init.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM1, &tim1_init);
  TIM_UpdateRequestConfig(TIM1, TIM_UpdateSource_Global);
  TIM_ARRPreloadConfig(TIM1, DISABLE); // Ensure immediate reload

  // Debug
  // TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);
  // NVIC_InitTypeDef tim1_up_it;
  // tim1_up_it.NVIC_IRQChannel = TIM1_UP_IRQn;
  // tim1_up_it.NVIC_IRQChannelPreemptionPriority = 0;
  // tim1_up_it.NVIC_IRQChannelSubPriority = 0;
  // tim1_up_it.NVIC_IRQChannelCmd = ENABLE;
  // NVIC_Init(&tim1_up_it);
  // TIM_ITConfig(TIM2, TIM_IT_CC1, ENABLE);
  // NVIC_InitTypeDef tim2_cc1_it = {
  //   .NVIC_IRQChannel = TIM2_IRQn,             // TIM2 global interrupt
  //   .NVIC_IRQChannelPreemptionPriority = 1,    // Lower priority than DMA
  //   .NVIC_IRQChannelSubPriority = 0,
  //   .NVIC_IRQChannelCmd = ENABLE
  // };
  // NVIC_Init(&tim2_cc1_it);
  
  TIM_OCInitTypeDef oc_init = {0};
  oc_init.TIM_OCMode = TIM_OCMode_Timing;
  oc_init.TIM_OutputState = TIM_OutputState_Disable;
  
  oc_init.TIM_Pulse = ss_high_period;
  TIM_OC1Init(TIM1, &oc_init);
  
  oc_init.TIM_Pulse = ss_low_period;
  TIM_OC2Init(TIM1, &oc_init);

  // TIM2 for DAC data, slaved to TIM1.  Runs just under 2x the rate of TIM1 so that it will trigger exactly twice per TIM1 cycle
  TIM_TimeBaseInitTypeDef data_timer = {0};
  data_timer.TIM_Prescaler = 0;
  const uint32_t half_sync_period = (ss_period + 1) / 2; // 320kHz
  const uint32_t dac_period = half_sync_period * 20/16; // Reduce freq to ~300kHz, to be absolutely sure this doesn't trigger 3x before the next TIM1 update.
  data_timer.TIM_Period = dac_period / 2 - 1; // TODO double freq because APB1 is slow
  data_timer.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM2, &data_timer);
  
  // Reset TIM2 on update event from TIM1
  TIM_SelectMasterSlaveMode(TIM1, TIM_MasterSlaveMode_Enable);
  TIM_SelectOutputTrigger(TIM1, TIM_TRGOSource_Update);
  TIM_SelectSlaveMode(TIM2, TIM_SlaveMode_Reset);
  TIM_SelectInputTrigger(TIM2, TIM_TS_ITR0);
  TIM2->SMCR |= TIM_SMCR_MSM;
  
  // Compare channel for DMA trigger
  // We use this instead of update because DMA request mappings are fixed!
  // TIM_OC1Init(TIM2, &oc_init);
  // TIM_SetCompare1(TIM2, 0);
  // TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Disable);
  // TIM_ARRPreloadConfig(TIM2, ENABLE); // TODO needed?
  TIM_OCInitTypeDef data_timer_init = {0};
  data_timer_init.TIM_OCMode = TIM_OCMode_Timing;
  data_timer_init.TIM_OutputState = TIM_OutputState_Disable;
  data_timer_init.TIM_Pulse = 0; // Trigger immediately after reset
  TIM_OC1Init(TIM2, &data_timer_init);
  TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Disable);
  TIM_ARRPreloadConfig(TIM2, DISABLE);
  
  DMA_InitTypeDef ss_dma = {0};
  ss_dma.DMA_DIR = DMA_DIR_PeripheralDST;
  ss_dma.DMA_BufferSize = 1;
  ss_dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  ss_dma.DMA_MemoryInc = DMA_MemoryInc_Disable;
  ss_dma.DMA_M2M = DMA_M2M_Disable;
  ss_dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
  ss_dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
  ss_dma.DMA_Mode = DMA_Mode_Circular;
  ss_dma.DMA_Priority = DMA_Priority_VeryHigh;

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

  // DMA channel 5 for SPI (TIM2_CH1)
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
  spi_dma.DMA_Priority = DMA_Priority_High;
  DMA_Init(DMA1_Channel5, &spi_dma);

  // SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);

  TIM_DMACmd(TIM1, TIM_DMA_CC1 | TIM_DMA_CC2, ENABLE);
  TIM_DMACmd(TIM2, TIM_DMA_CC1, ENABLE);

  DMA_ITConfig(DMA1_Channel5, DMA_IT_TC | DMA_IT_HT, ENABLE);
  NVIC_InitTypeDef nvic_init;
  nvic_init.NVIC_IRQChannel = DMA1_Channel5_IRQn;
  nvic_init.NVIC_IRQChannelPreemptionPriority = 0;
  nvic_init.NVIC_IRQChannelSubPriority = 0;
  nvic_init.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&nvic_init);

  DMA_Cmd(DMA1_Channel2, ENABLE);
  DMA_Cmd(DMA1_Channel3, ENABLE);
  DMA_Cmd(DMA1_Channel5, ENABLE);

  TIM2->CNT = 0;          // Explicitly reset counter
  TIM2->EGR = TIM_EGR_UG; // Force update to reload registers
  TIM2->SR = 0;           // Clear all flags manually

  TIM_Cmd(TIM1, ENABLE);
  TIM_Cmd(TIM2, ENABLE);
  for (volatile int i = 0; i < 10000; i++); // Small delay
}

// Pack 2 16-bit DMA/SPI words into a 32-bit value
uint32_t Dac::FormatDacWords(uint8_t channel, uint16_t sample) {
  uint16_t dac_channel = kNumCVOutputs - 1 - channel;
  uint16_t high = 0x1000 | (dac_channel << 9) | (sample >> 8);
  uint16_t low = sample << 8;
  return (static_cast<uint32_t>(high) << 16) | low;
}

#define BUFFER_SAMPLES(channel, dac_words_exp) \
  volatile uint16_t* ptr = &buffer_[0]; \
  /* Offset for buffer half */ \
  ptr += buffer_half * kAudioBlockSize * kFrameSize; \
  /* Offset for channel */ \
  ptr += channel * kDacValuesPerSample; \
  for (size_t i = 0; i < kAudioBlockSize; ++i) { \
    uint32_t words = (dac_words_exp); \
    ptr[0] = (words >> 16) & 0xFFFF; \
    ptr[1] = words & 0xFFFF; \
    ptr += kFrameSize; \
  }

void Dac::BufferSamples(uint8_t buffer_half, uint8_t channel, uint16_t* samples) {
  // BUFFER_SAMPLES(channel, FormatDacWords(channel, samples[i]))
  // BUFFER_SAMPLES(channel, FormatDacWords(channel, 0xCfff))
  BufferStaticSample(buffer_half, channel, 0xCfff);
  multi.PrintDebugByte(0x0C + (channel << 4));
  __DMB();
}

// TODO this actually has about 13x the latency of SysTick write
// consider dynamic injection into the DMA buffer being consumed?
// low-freq channels could use a "no change word"
void Dac::BufferStaticSample(uint8_t buffer_half, uint8_t channel, uint16_t sample) {
  uint32_t static_words = FormatDacWords(channel, sample);
  BUFFER_SAMPLES(channel, static_words)
  __DMB();
}

/* extern */
Dac dac;

}  // namespace yarns