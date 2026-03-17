// Copyright 2013 Emilie Gillet.
// Copyright 2025 Chris Rogers.
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

static volatile uint32_t dma_ss_high[kDacWordsPerSample] __attribute__((aligned(4))) = {kPinSS, 0};
static volatile uint32_t dma_ss_low [kDacWordsPerSample] __attribute__((aligned(4))) = {kPinSS, 0};

void Dac::Init() {
  can_fill_ = false;
  fillable_block_ = 1; // DMA will initially be consuming the first half
  for (size_t i = 0; i < kBufferSize; i += kDacWordsPerSample) {
    spi_tx_buffer_[i] = kNoopHighWord;
    spi_tx_buffer_[i + 1] = kNoopLowWord;
  }

  // Initialize SS pin.
  GPIO_InitTypeDef gpio_init = {0};
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
  SPI_InitTypeDef spi_init = {0};
  spi_init.SPI_Direction = SPI_Direction_1Line_Tx;
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

  TIM_TimeBaseInitTypeDef timer_init = {0};
  timer_init.TIM_Period = timer_period() - 1;
  timer_init.TIM_Prescaler = 0;
  timer_init.TIM_ClockDivision = TIM_CKD_DIV1;
  timer_init.TIM_CounterMode = TIM_CounterMode_Up;
  timer_init.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(TIM1, &timer_init);

  TIM_OCInitTypeDef oc_init = {0};
  oc_init.TIM_OCMode = TIM_OCMode_Timing;
  oc_init.TIM_OutputState = TIM_OutputState_Disable;
  oc_init.TIM_OutputNState = TIM_OutputNState_Disable;
  oc_init.TIM_OCIdleState = TIM_OCIdleState_Reset;
  oc_init.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
  oc_init.TIM_OCPolarity = TIM_OCPolarity_High;
  oc_init.TIM_OCNPolarity = TIM_OCNPolarity_High;
  
  // SYNC high (conditional)
  oc_init.TIM_Pulse = timer_period() * 1 / 1000 - 1;
  TIM_OC1Init(TIM1, &oc_init);
  
  // SYNC low (conditional)
  oc_init.TIM_Pulse = timer_period() * 2 / 1000 - 1;
  TIM_OC2Init(TIM1, &oc_init);

  // SPI2 TX
  oc_init.TIM_Pulse = timer_period() * 3 / 1000 - 1;
  TIM_OC3Init(TIM1, &oc_init);


  DMA_InitTypeDef ss_dma = {0};
  ss_dma.DMA_DIR = DMA_DIR_PeripheralDST;
  ss_dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  ss_dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
  ss_dma.DMA_M2M = DMA_M2M_Disable;
  ss_dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
  ss_dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
  ss_dma.DMA_Mode = DMA_Mode_Circular;
  ss_dma.DMA_Priority = DMA_Priority_VeryHigh;
  ss_dma.DMA_BufferSize = kDacWordsPerSample;

  // DMA for SYNC High (TIM1_CH1)
  DMA_InitTypeDef high_ss_dma = ss_dma;
  high_ss_dma.DMA_PeripheralBaseAddr = (uint32_t)&GPIOB->BSRR;
  high_ss_dma.DMA_MemoryBaseAddr = (uint32_t)&dma_ss_high[0];
  DMA_Init(DMA1_Channel2, &high_ss_dma);

  // DMA for SYNC Low (TIM1_CH2)
  DMA_InitTypeDef low_ss_dma = ss_dma;
  low_ss_dma.DMA_PeripheralBaseAddr = (uint32_t)&GPIOB->BRR;
  low_ss_dma.DMA_MemoryBaseAddr = (uint32_t)&dma_ss_low[0];
  DMA_Init(DMA1_Channel3, &low_ss_dma);

  // DMA for SPI2 TX (TIM1_CH3)
  DMA_InitTypeDef spi_dma = {0};
  spi_dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
  spi_dma.DMA_MemoryBaseAddr = (uint32_t)&spi_tx_buffer_[0];
  spi_dma.DMA_DIR = DMA_DIR_PeripheralDST;
  spi_dma.DMA_BufferSize = kBufferSize;
  spi_dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  spi_dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
  spi_dma.DMA_M2M = DMA_M2M_Disable;
  spi_dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
  spi_dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
  spi_dma.DMA_Mode = DMA_Mode_Circular;
  spi_dma.DMA_Priority = DMA_Priority_VeryHigh;
  DMA_Init(DMA1_Channel6, &spi_dma);

  DMA_ITConfig(DMA1_Channel6, DMA_IT_TC | DMA_IT_HT, ENABLE);

  NVIC_EnableIRQ(DMA1_Channel6_IRQn);

  DMA_Cmd(DMA1_Channel6, ENABLE);
  DMA_Cmd(DMA1_Channel2, ENABLE);
  DMA_Cmd(DMA1_Channel3, ENABLE);

  TIM_DMACmd(
    TIM1,
    TIM_DMA_CC3 |
    TIM_DMA_CC1 |
    TIM_DMA_CC2,
    ENABLE
  );

  TIM_Cmd(TIM1, ENABLE);
}

// Write a packed high:low command word pair to a buffer position
#define WRITE_WORDS(ptr, words_exp) do { \
  uint32_t w_ = (words_exp); \
  (ptr)[0] = (w_ >> 16) & 0xFFFF; \
  (ptr)[1] = w_ & 0xFFFF; \
} while(0)

// Write interleaved DAC words starting at start_frame
#define BUFFER_SAMPLES(channel, dac_words_exp, start_frame) \
  volatile uint16_t* ptr = &spi_tx_buffer_[0]; \
  /* Offset for buffer half */ \
  ptr += block ? kDacWordsPerBlock : 0; \
  /* Offset for channel and start frame */ \
  ptr += channel << kDacWordsPerSampleBits; \
  ptr += (start_frame) * kDacWordsPerFrame; \
  for (size_t i = (start_frame); i < kAudioBlockSize; ++i) { \
    WRITE_WORDS(ptr, dac_words_exp); \
    ptr += kDacWordsPerFrame; \
  }

void Dac::BufferSamples(uint8_t block, uint8_t channel, int16_t* samples) {
  BUFFER_SAMPLES(channel, FormatCommandWords(channel, samples[i]), 0)
}

void Dac::BufferStaticSample(uint8_t block, uint8_t channel, int16_t sample) {
  uint32_t static_words = FormatCommandWords(channel, sample);
  BUFFER_SAMPLES(channel, static_words, 0)
}

// Low-latency DC update, called from SysTick at 4kHz for each DC channel.
//
// Two writes per call:
//
// 1. Frame 0 of the fillable block — ensures the next block DMA consumes has
//    the current value in its first frame. Safe because DMA is consuming the
//    OTHER block; the main loop never touches frame 0 of DC channels (it only
//    writes NOOPs to frames 1-63 via FillDCNoops).
//
// 2. Injection near the DMA cursor in the being-consumed block — places the
//    value a few frames ahead of where DMA is currently reading, so the DAC
//    latches it within ~22-44us instead of waiting for the next block. Safe
//    because the write completes in a few cycles while DMA advances one word
//    per ~400 cycles. If the target wraps into the fillable block, it's
//    redundant with write #1 and will be erased by the next FillDCNoops pass.
//    Stale injections in the consumed block are erased when the main loop
//    fills that block with NOOPs on its next cycle.
void Dac::UpdateDC(uint8_t channel, uint16_t sample) {
  uint32_t words = FormatCommandWords(channel, sample);

  // (1) Frame 0 of fillable block
  size_t frame0_offset = (fillable_block_ ? kDacWordsPerBlock : 0)
                       + (channel << kDacWordsPerSampleBits);
  WRITE_WORDS(&spi_tx_buffer_[frame0_offset], words);

  // (2) Inject ahead of DMA cursor
  size_t cursor_frame = (kBufferSize - DMA1_Channel6->CNDTR) / kDacWordsPerFrame;
  size_t target_frame = (cursor_frame + kInjectGapFrames) % kTotalFrames;
  size_t inject_offset = target_frame * kDacWordsPerFrame
                       + (channel << kDacWordsPerSampleBits);
  WRITE_WORDS(&spi_tx_buffer_[inject_offset], words);
}

void Dac::FillDCNoops(uint8_t block, uint8_t channel) {
  // Skip frame 0 (owned by SysTick's UpdateDC)
  static const uint32_t noop = (static_cast<uint32_t>(kNoopHighWord) << 16) | kNoopLowWord;
  BUFFER_SAMPLES(channel, noop, 1)
}

uint32_t Dac::timer_base_freq(uint8_t apb) const {
  RCC_ClocksTypeDef rcc_clocks;
  RCC_GetClocksFreq(&rcc_clocks);
  uint32_t hclk = rcc_clocks.HCLK_Frequency;
  uint32_t pclk = apb == 1 ? rcc_clocks.PCLK1_Frequency : rcc_clocks.PCLK2_Frequency;
  return hclk == pclk ? pclk : pclk * 2;
}

uint32_t Dac::timer_period() const {
  uint32_t base_freq = timer_base_freq(2);
  return base_freq / kDacWordsHz;
}

/* extern */
Dac dac;

}  // namespace yarns