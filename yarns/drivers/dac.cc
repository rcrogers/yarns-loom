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

#include <algorithm>

namespace yarns {

using namespace std;

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
  
  // Initialize values
  for (uint8_t i = 0; i < kNumChannels; ++i) {
    value_[i] = 0;
    update_[i] = false;
    is_high_freq_[i] = false;
    circular_dma_active_[i] = false;
    audio_buffer_[i].state = 0;
  }
  
  // Initialize double buffer system
  active_channel_ = 0;
  active_buffer_ = 0;
  next_buffer_ = 1;
  transfer_in_progress_ = false;
  
  for (uint8_t i = 0; i < kNumBuffers; ++i) {
    buffers_[i].ready = false;
  }
  
  InitDMA();
}

void Dac::InitDMA() {
  // Enable DMA1 clock
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
  
  // Configure DMA channel for SPI2 TX (Channel 5)
  DMA_InitTypeDef dma_init;
  DMA_StructInit(&dma_init);
  dma_init.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
  dma_init.DMA_MemoryBaseAddr = (uint32_t)buffers_[0].data;  // Initial buffer
  dma_init.DMA_DIR = DMA_DIR_PeripheralDST;
  dma_init.DMA_BufferSize = kDMABufferSize;
  dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
  dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
  dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
  dma_init.DMA_Mode = DMA_Mode_Normal;  // Will be changed to circular for audio
  dma_init.DMA_Priority = DMA_Priority_High;
  dma_init.DMA_M2M = DMA_M2M_Disable;
  
  DMA_Init(DMA1_Channel5, &dma_init);
  
  // Enable DMA transfer complete interrupt
  DMA_ITConfig(DMA1_Channel5, DMA_IT_TC, ENABLE);
  DMA_ITConfig(DMA1_Channel5, DMA_IT_HT, ENABLE);  // Half transfer complete
  
  // Enable DMA1 Channel5 interrupt
  NVIC_InitTypeDef nvic_init;
  nvic_init.NVIC_IRQChannel = DMA1_Channel5_IRQn;
  nvic_init.NVIC_IRQChannelPreemptionPriority = 0;
  nvic_init.NVIC_IRQChannelSubPriority = 0;
  nvic_init.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&nvic_init);
  
  // Enable SPI2 DMA TX request
  SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
}

// New method to send a single value to DAC directly
void Dac::SendSingleValue(uint8_t channel, uint16_t value) {
  if (circular_dma_active_[channel]) {
    // Stop circular DMA if it's running for this channel
    StopCircularDMA(channel);
  }
  
  // Format data for DAC
  uint16_t dac_channel = kNumChannels - 1 - channel;
  buffers_[next_buffer_].data[0] = 0x1000 | (dac_channel << 9) | (value >> 8);
  buffers_[next_buffer_].data[1] = value << 8;
  buffers_[next_buffer_].ready = true;
  
  StartTransferIfNeeded();
}

// New methods for circular DMA
void Dac::SetHighFrequencyMode(uint8_t channel, bool high_freq) {
  is_high_freq_[channel] = high_freq;
  
  if (high_freq && !circular_dma_active_[channel]) {
    // Initial buffer fill
    FillBuffer(channel);
    StartCircularDMA(channel);
  } else if (!high_freq && circular_dma_active_[channel]) {
    StopCircularDMA(channel);
  }
}

void Dac::FillBuffer(uint8_t channel) {
  uint16_t dac_channel = kNumChannels - 1 - channel;
  uint16_t* buffer = audio_buffer_[channel].data;
  
  // Determine which half to fill based on buffer state
  uint16_t* dest = buffer;
  if (audio_buffer_[channel].state & kDmaCompleteFlag) {
    // First half needs filling
    dest = buffer;
    audio_buffer_[channel].state &= ~kDmaCompleteFlag;
  } else if (audio_buffer_[channel].state & kDmaHalfCompleteFlag) {
    // Second half needs filling
    dest = buffer + kAudioBufferSize;
    audio_buffer_[channel].state &= ~kDmaHalfCompleteFlag;
  }
  
  // Fill the buffer with data from CVOutput's GetDACSample()
  for (uint16_t i = 0; i < kAudioBufferSize; i += 2) {
    uint16_t sample = multi.mutable_cv_output(channel)->GetDACSample();
    
    // Format data for DAC (command word + data word)
    dest[i] = 0x1000 | (dac_channel << 9) | (sample >> 8);
    dest[i+1] = sample << 8;
  }
}

void Dac::StartCircularDMA(uint8_t channel) {
  // Disable current DMA if running
  DMA_Cmd(DMA1_Channel5, DISABLE);
  // Wait a bit to ensure DMA is fully disabled
  // (there's no direct status check function, so we'll just add a small delay)
  for (volatile uint16_t i = 0; i < 10; ++i) { }
  
  // Reset DMA configuration
  DMA_DeInit(DMA1_Channel5);
  
  // Configure DMA for circular mode
  DMA_InitTypeDef dma_init;
  DMA_StructInit(&dma_init);
  dma_init.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
  dma_init.DMA_MemoryBaseAddr = (uint32_t)audio_buffer_[channel].data;
  dma_init.DMA_DIR = DMA_DIR_PeripheralDST;
  dma_init.DMA_BufferSize = kAudioBufferSize * 2;  // Double because each sample is 2 words
  dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
  dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
  dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
  dma_init.DMA_Mode = DMA_Mode_Circular;
  dma_init.DMA_Priority = DMA_Priority_High;
  dma_init.DMA_M2M = DMA_M2M_Disable;
  
  DMA_Init(DMA1_Channel5, &dma_init);
  
  // Enable interrupts
  DMA_ITConfig(DMA1_Channel5, DMA_IT_TC, ENABLE);
  DMA_ITConfig(DMA1_Channel5, DMA_IT_HT, ENABLE);
  
  // Toggle SS pin and start transfer
  GPIOB->BSRR = kPinSS; // Set SS pin high
  GPIOB->BRR = kPinSS;  // Set SS pin low
  
  // Start DMA
  DMA_Cmd(DMA1_Channel5, ENABLE);
  circular_dma_active_[channel] = true;
  transfer_in_progress_ = true;
}

void Dac::StopCircularDMA(uint8_t channel) {
  // Disable DMA
  DMA_Cmd(DMA1_Channel5, DISABLE);
  // Short delay to ensure DMA is stopped
  for (volatile uint16_t i = 0; i < 10; ++i) { }
  
  // Reset DMA configuration
  DMA_DeInit(DMA1_Channel5);
  
  circular_dma_active_[channel] = false;
  transfer_in_progress_ = false;
  
  // Return to normal mode configuration
  DMA_InitTypeDef dma_init;
  DMA_StructInit(&dma_init);
  dma_init.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
  dma_init.DMA_MemoryBaseAddr = (uint32_t)buffers_[0].data;
  dma_init.DMA_DIR = DMA_DIR_PeripheralDST;
  dma_init.DMA_BufferSize = kDMABufferSize;
  dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
  dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
  dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
  dma_init.DMA_Mode = DMA_Mode_Normal;
  dma_init.DMA_Priority = DMA_Priority_High;
  dma_init.DMA_M2M = DMA_M2M_Disable;
  
  DMA_Init(DMA1_Channel5, &dma_init);
  
  // Re-enable interrupts
  DMA_ITConfig(DMA1_Channel5, DMA_IT_TC, ENABLE);
}

void Dac::HandleDMAComplete() {
  if (circular_dma_active_[active_channel_]) {
    // For circular DMA, mark buffer half as needing refill
    audio_buffer_[active_channel_].state |= kDmaCompleteFlag;
    
    // Fill the half that just completed
    FillBuffer(active_channel_);
  } else {
    // Normal mode behavior
    DMA_ClearFlag(DMA1_FLAG_TC5);
    DMA_Cmd(DMA1_Channel5, DISABLE);
    
    transfer_in_progress_ = false;
    
    // If next buffer is ready, start its transfer immediately
    StartTransferIfNeeded();
  }
}

void Dac::HandleDMAHalfComplete() {
  if (circular_dma_active_[active_channel_]) {
    // Mark first half as needing refill
    audio_buffer_[active_channel_].state |= kDmaHalfCompleteFlag;
    
    // Fill the half that just completed
    FillBuffer(active_channel_);
  }
}

}  // namespace yarns
