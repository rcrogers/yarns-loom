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
#include "yarns/multi.h"

namespace yarns {

using namespace std;

// Define DMA channels - these are sequential for convenience
DMA_Channel_TypeDef* kDmaChannels[kNumCVOutputs] = {
  DMA1_Channel1, DMA1_Channel2, DMA1_Channel3, DMA1_Channel4
};

// DMA IRQ flags - used for identifying half/complete transfer in IRQ handlers
const uint32_t kDmaIrqFlags[kNumCVOutputs] = {
  DMA1_IT_TC1, DMA1_IT_TC2, DMA1_IT_TC3, DMA1_IT_TC4
};

void Dac::Init() {
  // Initialize SPI pins (SS, MOSI, SCK)
  GPIO_InitTypeDef gpio_init;
  
  // SS as push-pull output
  gpio_init.GPIO_Pin = GPIO_Pin_12;  // SS
  gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
  gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOB, &gpio_init);
  DeassertCS(); // Start with CS high
  
  // MOSI and SCK as alternate function
  gpio_init.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_15;  // SCK, MOSI
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
  
  // Initialize channels
  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    high_freq_mode_[i] = false;
    current_value_[i] = 0;
    
    // Initialize buffer state
    buffers_[i].half_needs_filling[0] = false;
    buffers_[i].half_needs_filling[1] = false;
    
    // Pre-fill buffers with zeros
    uint16_t formatted_zero = FormatDacWord(i, 0);
    for (uint16_t j = 0; j < kAudioBlockSize * 2; ++j) {
      buffers_[i].buffer[j] = formatted_zero;
    }
    
    // Configure DMA
    InitDma(i);
  }
  
  // Configure DMA interrupt priorities
  NVIC_InitTypeDef nvic_init;
  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    nvic_init.NVIC_IRQChannel = DMA1_Channel1_IRQn + i;  // IRQs are sequential
    nvic_init.NVIC_IRQChannelPreemptionPriority = 0;
    nvic_init.NVIC_IRQChannelSubPriority = i;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);
  }
}

void Dac::InitDma(uint8_t channel) {
  if (channel >= kNumCVOutputs) return;
  
  DMA_InitTypeDef dma_init;
  dma_init.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
  dma_init.DMA_MemoryBaseAddr = (uint32_t)buffers_[channel].buffer;
  dma_init.DMA_DIR = DMA_DIR_PeripheralDST;
  dma_init.DMA_BufferSize = kAudioBlockSize * 2;
  dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
  dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
  dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
  dma_init.DMA_Mode = DMA_Mode_Circular;
  dma_init.DMA_Priority = DMA_Priority_High;
  dma_init.DMA_M2M = DMA_M2M_Disable;
  
  DMA_Init(kDmaChannels[channel], &dma_init);
  
  // Enable half and complete transfer interrupts
  DMA_ITConfig(kDmaChannels[channel], DMA_IT_TC | DMA_IT_HT, ENABLE);
}

bool Dac::SetHighFrequencyMode(uint8_t channel, bool high_freq) {
  if (channel >= kNumCVOutputs) {
    return false;
  }
  
  // If already in the requested mode, no change needed
  if (high_freq_mode_[channel] == high_freq) {
    return false;
  }
  
  high_freq_mode_[channel] = high_freq;
  
  if (high_freq) {
    // Fill buffer with current value before starting DMA
    uint16_t formatted_value = FormatDacWord(channel, current_value_[channel]);
    for (uint16_t i = 0; i < kAudioBlockSize * 2; ++i) {
      buffers_[channel].buffer[i] = formatted_value;
    }
    
    // Mark both halves as not needing filling
    buffers_[channel].half_needs_filling[0] = false;
    buffers_[channel].half_needs_filling[1] = false;
    
    // Start DMA
    StartDma(channel);
  } else {
    // Stop DMA
    StopDma(channel);
    
    // Write current value immediately
    DirectWrite(channel, current_value_[channel]);
  }
  
  return true;
}

void Dac::StartDma(uint8_t channel) {
  if (channel >= kNumCVOutputs) return;
  
  // Enable SPI DMA request
  SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
  
  // Enable DMA channel
  DMA_Cmd(kDmaChannels[channel], ENABLE);
}

void Dac::StopDma(uint8_t channel) {
  if (channel >= kNumCVOutputs) return;
  
  // Disable DMA channel
  DMA_Cmd(kDmaChannels[channel], DISABLE);
  
  // Check if any other channels are still in high-frequency mode
  bool any_high_freq = false;
  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    if (i != channel && high_freq_mode_[i]) {
      any_high_freq = true;
      break;
    }
  }
  
  // If no channels need DMA, disable SPI DMA request
  if (!any_high_freq) {
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, DISABLE);
  }
}

uint16_t* Dac::GetBufferHalfToFill(uint8_t channel, bool* is_first_half) {
  if (channel >= kNumCVOutputs || !high_freq_mode_[channel]) {
    return NULL;
  }
  
  // Check if first half needs filling
  if (buffers_[channel].half_needs_filling[0]) {
    *is_first_half = true;
    return buffers_[channel].buffer;
  }
  
  // Check if second half needs filling
  if (buffers_[channel].half_needs_filling[1]) {
    *is_first_half = false;
    return buffers_[channel].buffer + kAudioBlockSize;
  }
  
  // No buffer half needs filling
  return NULL;
}

void Dac::MarkBufferHalfFilled(uint8_t channel, bool is_first_half) {
  if (channel >= kNumCVOutputs || !high_freq_mode_[channel]) {
    return;
  }
  
  buffers_[channel].half_needs_filling[is_first_half ? 0 : 1] = false;
}

void Dac::HandleDMAIrq(uint8_t channel, bool is_half_transfer) {
  if (channel >= kNumCVOutputs) return;
  
  // Mark appropriate buffer half as needing filling
  buffers_[channel].half_needs_filling[is_half_transfer ? 0 : 1] = true;
}

void Dac::WriteManual(uint8_t channel, uint16_t value) {
  if (channel >= kNumCVOutputs || high_freq_mode_[channel]) {
    return;
  }
  
  if (current_value_[channel] != value) {
    current_value_[channel] = value;
    DirectWrite(channel, value);
  }
}

void Dac::WriteAllManual(const uint16_t* values) {
  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    WriteManual(i, values[i]);
  }
}

void Dac::DirectWrite(uint8_t channel, uint16_t value) {
  if (channel >= kNumCVOutputs) return;
  
  uint16_t word = FormatDacWord(channel, value);
  
  AssertCS();
  
  SPI_I2S_SendData(SPI2, word);
  
  // Wait for transmission to complete with timeout
  uint32_t timeout = kSpiTimeout;
  while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET) {
    if (--timeout == 0) {
      DeassertCS();
      return;
    }
  }
  
  DeassertCS();
}

// Factory instance
Dac dac;

}  // namespace yarns