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

namespace yarns {

using namespace std;

const uint16_t kPinSS = GPIO_Pin_12;

void Dac::Init() {
  // Initialize SS pin
  GPIO_InitTypeDef gpio_init;
  gpio_init.GPIO_Pin = kPinSS;
  gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
  gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOB, &gpio_init);
  GPIOB->BSRR = kPinSS; // Set CS high initially
  
  // Initialize MOSI and SCK pins
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
  
  // Enable DMA clock
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
  
  // Initialize channel buffers and set default mode
  for (uint8_t i = 0; i < kNumChannels; ++i) {
    channel_buffers_[i].write_index = 0;
    channel_buffers_[i].active_index = 0;
    channel_buffers_[i].buffer_ready = true;
    mode_[i] = MODE_MANUAL;
    value_[i] = 0;
  }
  
  // Configure timer for sample rate (assuming TIM2)
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
  TIM_TimeBaseInitTypeDef tim_init;
  tim_init.TIM_Period = F_CPU / 40000 - 1;
  tim_init.TIM_Prescaler = 0;
  tim_init.TIM_ClockDivision = TIM_CKD_DIV1;
  tim_init.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM2, &tim_init);
  
  // Configure timer DMA requests
  TIM_DMACmd(TIM2, TIM_DMA_Update, ENABLE);
  TIM_Cmd(TIM2, ENABLE); // Keep timer running for all channels
  
  // Initialize DMA configuration (but don't enable yet)
  for (uint8_t i = 0; i < kNumChannels; ++i) {
    ConfigureDma(i);
  }
}

void Dac::ConfigureDma(uint8_t channel) {
  // We'll use one DMA channel for each DAC channel
  DMA_Channel_TypeDef* dma_channel = nullptr;
  
  switch (channel) {
    case 0: dma_channel = DMA1_Channel1; break;
    case 1: dma_channel = DMA1_Channel2; break;
    case 2: dma_channel = DMA1_Channel3; break;
    case 3: dma_channel = DMA1_Channel4; break;
    default: return;
  }
  
  // Configure DMA for CS-toggling SPI transfers
  // Each transfer needs to:
  // 1. Assert CS
  // 2. Send data
  // 3. Deassert CS
  
  DMA_InitTypeDef dma_init;
  dma_init.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
  dma_init.DMA_MemoryBaseAddr = (uint32_t)channel_buffers_[channel].buffer[0];
  dma_init.DMA_DIR = DMA_DIR_PeripheralDST;
  dma_init.DMA_BufferSize = kDmaBufSize;
  dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
  dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
  dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
  dma_init.DMA_Mode = DMA_Mode_Circular;
  dma_init.DMA_Priority = DMA_Priority_High;
  dma_init.DMA_M2M = DMA_M2M_Disable;
  
  DMA_Init(dma_channel, &dma_init);
  
  // Enable DMA transfer complete interrupt
  DMA_ITConfig(dma_channel, DMA_IT_TC, ENABLE);
  
  // Configure NVIC for DMA interrupt
  NVIC_InitTypeDef nvic_init;
  nvic_init.NVIC_IRQChannel = DMA1_Channel1_IRQn + channel; // Channels 1-4 have sequential IRQs
  nvic_init.NVIC_IRQChannelPreemptionPriority = 0;
  nvic_init.NVIC_IRQChannelSubPriority = 0;
  nvic_init.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&nvic_init);
}

bool Dac::SetMode(uint8_t channel, Mode mode) {
  if (channel >= kNumChannels) {
    return false;
  }
  
  // If already in the requested mode, NOOP
  if (mode_[channel] == mode) {
    return false;
  }
  
  // Change mode
  if (mode == MODE_DMA) {
    // Switching to high-frequency DMA mode
    mode_[channel] = MODE_DMA;
    StartDma(channel);
  } else {
    // Switching to low-frequency manual mode
    StopDma(channel);
    mode_[channel] = MODE_MANUAL;
  }
  
  return true;
}

void Dac::StartDma(uint8_t channel) {
  if (channel >= kNumChannels) return;
  
  DMA_Channel_TypeDef* dma_channel = nullptr;
  switch (channel) {
    case 0: dma_channel = DMA1_Channel1; break;
    case 1: dma_channel = DMA1_Channel2; break;
    case 2: dma_channel = DMA1_Channel3; break;
    case 3: dma_channel = DMA1_Channel4; break;
    default: return;
  }
  
  // Ensure at least one buffer is filled before starting
  if (channel_buffers_[channel].buffer_ready) {
    // Fill with current value if no data has been provided
    uint16_t initial_value = value_[channel];
    uint16_t* buffer = channel_buffers_[channel].buffer[0];
    
    for (uint16_t i = 0; i < kDmaBufSize; ++i) {
      uint16_t dac_channel = kNumChannels - 1 - channel;
      buffer[i] = 0x1000 | (dac_channel << 9) | (initial_value & 0x0FFF);
    }
    
    channel_buffers_[channel].buffer_ready = false;
  }
  
  // Enable DMA channel
  DMA_Cmd(dma_channel, ENABLE);
}

void Dac::StopDma(uint8_t channel) {
  if (channel >= kNumChannels) return;
  
  DMA_Channel_TypeDef* dma_channel = nullptr;
  switch (channel) {
    case 0: dma_channel = DMA1_Channel1; break;
    case 1: dma_channel = DMA1_Channel2; break;
    case 2: dma_channel = DMA1_Channel3; break;
    case 3: dma_channel = DMA1_Channel4; break;
    default: return;
  }
  
  // Disable DMA channel
  DMA_Cmd(dma_channel, DISABLE);
  
  // Reset buffer state
  channel_buffers_[channel].write_index = 0;
  channel_buffers_[channel].active_index = 0;
  channel_buffers_[channel].buffer_ready = true;
}

bool Dac::IsBufferReady(uint8_t channel) {
  if (channel >= kNumChannels || mode_[channel] != MODE_DMA) {
    return false;
  }
  return channel_buffers_[channel].buffer_ready;
}

bool Dac::FillBuffer(uint8_t channel, const uint16_t* samples, uint16_t count) {
  if (channel >= kNumChannels || 
      mode_[channel] != MODE_DMA || 
      !channel_buffers_[channel].buffer_ready) {
    return false;
  }
  
  // Determine which buffer to fill (the one not being used by DMA)
  uint8_t buffer_index = channel_buffers_[channel].write_index;
  uint16_t* buffer = channel_buffers_[channel].buffer[buffer_index];
  
  // Copy data to buffer, transforming to DAC format
  for (uint16_t i = 0; i < count && i < kDmaBufSize; ++i) {
    // Format samples for DAC - add channel/command bits
    uint16_t dac_value = samples[i] & 0x0FFF; // 12-bit DAC value
    uint16_t dac_channel = kNumChannels - 1 - channel;
    buffer[i] = 0x1000 | (dac_channel << 9) | dac_value;
  }
  
  // Fill remainder with last value if count < kDmaBufSize
  if (count < kDmaBufSize) {
    uint16_t last_value = (count > 0) ? samples[count-1] & 0x0FFF : 0;
    uint16_t dac_channel = kNumChannels - 1 - channel;
    uint16_t formatted = 0x1000 | (dac_channel << 9) | last_value;
    
    for (uint16_t i = count; i < kDmaBufSize; ++i) {
      buffer[i] = formatted;
    }
  }
  
  // Mark buffer as filled and not ready for more data
  channel_buffers_[channel].buffer_ready = false;
  
  // Toggle write index for next time
  channel_buffers_[channel].write_index = (buffer_index + 1) % 2;
  
  return true;
}

void Dac::HandleDmaInterrupt(uint8_t channel) {
  if (channel >= kNumChannels || mode_[channel] != MODE_DMA) return;
  
  // For SPI DACs that need CS toggling, we need a custom DMA handling logic
  // This would ideally involve a second DMA channel to toggle GPIO
  // But for simplicity, we'll handle it in the interrupt
  
  // Toggle active buffer index
  channel_buffers_[channel].active_index = 
      (channel_buffers_[channel].active_index + 1) % 2;
  
  // Mark non-active buffer as ready to be filled
  channel_buffers_[channel].buffer_ready = true;
}

}  // namespace yarns