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

const uint16_t kPinSS = GPIO_Pin_12;

DMA_Channel_TypeDef* kDmaChannels[kNumCVOutputs] = {
  DMA1_Channel1, DMA1_Channel2, DMA1_Channel3, DMA1_Channel4
};

void Dac::Init() {
  // Initialize SS pin
  GPIO_InitTypeDef gpio_init;
  gpio_init.GPIO_Pin = kPinSS;
  gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
  gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOB, &gpio_init);
  DeassertCS(); // Set CS high initially
  
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
  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    mode_[i] = MODE_MANUAL;
    value_[i] = 0;
    channel_buffers_[i].first_half_free = true;
    channel_buffers_[i].second_half_free = true;
    
    // Pre-initialize all buffers with zeros
    for (uint16_t j = 0; j < kAudioBlockSize; ++j) {
      channel_buffers_[i].buffer[j] = FormatDacWord(i, 0);
    }
    
    // Configure DMA for each channel
    InitDma(i);
  }
  
  // Configure NVIC priorities for DMA interrupts
  NVIC_InitTypeDef nvic_init;
  
  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    nvic_init.NVIC_IRQChannel = DMA1_Channel1_IRQn + i;  // Channel IRQs are sequential
    nvic_init.NVIC_IRQChannelPreemptionPriority = 0;
    nvic_init.NVIC_IRQChannelSubPriority = i;  // Different subpriority for each channel
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);
  }
}

void Dac::InitDma(uint8_t channel) {
  if (channel >= kNumCVOutputs) return;
  
  // Configure DMA for SPI transfers
  DMA_InitTypeDef dma_init;
  dma_init.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
  dma_init.DMA_MemoryBaseAddr = (uint32_t)channel_buffers_[channel].buffer;
  dma_init.DMA_DIR = DMA_DIR_PeripheralDST;
  dma_init.DMA_BufferSize = kAudioBlockSize;
  dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
  dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
  dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
  dma_init.DMA_Mode = DMA_Mode_Circular;  // Circular mode for continuous playback
  dma_init.DMA_Priority = DMA_Priority_High;
  dma_init.DMA_M2M = DMA_M2M_Disable;
  
  DMA_Channel_TypeDef* dma_channel = kDmaChannels[channel];
  DMA_Init(dma_channel, &dma_init);
  
  // Enable both half-transfer and complete-transfer interrupts
  DMA_ITConfig(dma_channel, DMA_IT_TC | DMA_IT_HT, ENABLE);
}

bool Dac::SetMode(uint8_t channel, Mode mode) {
  if (channel >= kNumCVOutputs) {
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
  if (channel >= kNumCVOutputs) return;
  
  // Fill buffer with current value before starting
  uint16_t formatted_value = FormatDacWord(channel, value_[channel]);
  for (uint16_t i = 0; i < kAudioBlockSize; ++i) {
    channel_buffers_[channel].buffer[i] = formatted_value;
  }
  
  // Reset buffer state flags
  channel_buffers_[channel].first_half_free = false;
  channel_buffers_[channel].second_half_free = false;
  
  // Enable SPI DMA request
  SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
  
  // Enable DMA channel to start transfer
  DMA_Cmd(kDmaChannels[channel], ENABLE);
}

void Dac::StopDma(uint8_t channel) {
  if (channel >= kNumCVOutputs) return;
  
  // Disable DMA channel
  DMA_Cmd(kDmaChannels[channel], DISABLE);
  
  // Disable SPI DMA request if no channels are in DMA mode
  bool any_dma_active = false;
  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    if (i != channel && mode_[i] == MODE_DMA) {
      any_dma_active = true;
      break;
    }
  }
  
  if (!any_dma_active) {
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, DISABLE);
  }
}

bool Dac::NeedsSamples(uint8_t channel) {
  if (channel >= kNumCVOutputs || mode_[channel] != MODE_DMA) {
    return false;
  }
  
  // Return true if either half of the buffer needs filling
  return channel_buffers_[channel].first_half_free || 
         channel_buffers_[channel].second_half_free;
}

bool Dac::FillBuffer(uint8_t channel, const uint16_t* samples, uint16_t count) {
  multi.PrintDebugByte(0xA0 + channel);
  if (channel >= kNumCVOutputs || mode_[channel] != MODE_DMA) {
    return false;
  }
  multi.PrintDebugByte(0xB0 + channel);
  
  const uint16_t half_buffer_size = kAudioBlockSize / 2;
  
  // Determine which half of the buffer to fill
  bool use_first_half = channel_buffers_[channel].first_half_free;
  bool use_second_half = channel_buffers_[channel].second_half_free;
  
  // If both halves need filling, prioritize the first half
  if (use_first_half && use_second_half) {
    use_second_half = false;
  }
  
  if (!use_first_half && !use_second_half) {
    // No buffer half is ready for filling
    return false;
  }

  multi.PrintDebugByte(0xC0 + channel);
  
  // Calculate start position in the buffer
  uint16_t start_pos = use_first_half ? 0 : half_buffer_size;
  
  // Copy data to buffer, transforming to DAC format
  for (uint16_t i = 0; i < count && i < half_buffer_size; ++i) {
    channel_buffers_[channel].buffer[start_pos + i] = FormatDacWord(channel, samples[i]);
  }
  
  // Fill remainder with last value if count < half_buffer_size
  if (count < half_buffer_size) {
    uint16_t formatted = count > 0 ? FormatDacWord(channel, samples[count-1]) : 
                                    FormatDacWord(channel, 0);
    for (uint16_t i = count; i < half_buffer_size; ++i) {
      channel_buffers_[channel].buffer[start_pos + i] = formatted;
    }
  }
  
  // Mark buffer half as no longer free
  if (use_first_half) {
    channel_buffers_[channel].first_half_free = false;
  } else {
    channel_buffers_[channel].second_half_free = false;
  }
  
  return true;
}

void Dac::HandleDMAIrq(uint8_t channel) {
  if (channel >= kNumCVOutputs) return;

  uint32_t flag;
  switch (channel) {
    case 0: flag = DMA1_IT_TC1; break;
    case 1: flag = DMA1_IT_TC2; break;
    case 2: flag = DMA1_IT_TC3; break;
    case 3: flag = DMA1_IT_TC4; break;
    default: return;
  }
  
  const uint32_t half_transfer_bit = flag & DMA_IT_HT;
  if (DMA_GetITStatus(half_transfer_bit)) {
    DMA_ClearITPendingBit(half_transfer_bit);    
    channel_buffers_[channel].first_half_free = true;
  }
  
  const uint32_t transfer_complete_bit = flag & DMA_IT_TC;
  if (DMA_GetITStatus(transfer_complete_bit)) {
    DMA_ClearITPendingBit(transfer_complete_bit);    
    channel_buffers_[channel].second_half_free = true;
  }
}

void Dac::AssertCS() {
  GPIOB->BRR = kPinSS;
}

void Dac::DeassertCS() {
  GPIOB->BSRR = kPinSS;
}

void Dac::WriteIfManual(uint8_t channel, uint16_t value) {
  if (channel >= kNumCVOutputs || mode_[channel] != MODE_MANUAL) {
    return;
  }
  
  if (value_[channel] != value) {
    value_[channel] = value;
    DirectWrite(channel, value);
  }
}

void Dac::WriteAllIfManual(const uint16_t* values) {
  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    WriteIfManual(i, values[i]);
  }
}

void Dac::DirectWrite(uint8_t channel, uint16_t value) {
  if (channel >= kNumCVOutputs) return;
  
  // Format the data word with channel and command
  uint16_t word = FormatDacWord(channel, value);
  
  // Assert chip select
  AssertCS();
  
  // Send the data
  SPI_I2S_SendData(SPI2, word);
  
  // Wait for transmission to complete with timeout protection
  uint32_t timeout = kSpiTimeout;
  while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET) {
    if (--timeout == 0) {
      // Timeout - abort transfer
      DeassertCS();
      return;
    }
  }
  
  // Deassert chip select
  DeassertCS();
}

// extern
Dac dac;

}  // namespace yarns