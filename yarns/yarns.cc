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

#include <stm32f10x_conf.h>

#include "stmlib/utils/dsp.h"
#include "stmlib/utils/ring_buffer.h"
#include "stmlib/system/system_clock.h"
#include "stmlib/system/uid.h"

#include "yarns/drivers/dac.h"
#include "yarns/drivers/gate_output.h"
#include "yarns/drivers/midi_io.h"
#include "yarns/drivers/system.h"
#include "yarns/midi_handler.h"
#include "yarns/multi.h"
#include "yarns/settings.h"
#include "yarns/storage_manager.h"
#include "yarns/ui.h"

using namespace yarns;
using namespace stmlib;

const char* const kVersion = "Loom 2_8_0";

GateOutput gate_output;
MidiIO midi_io;
System sys;

extern "C" {
  
void HardFault_Handler(void) { while (1); }
void MemManage_Handler(void) { while (1); }
void BusFault_Handler(void) { while (1); }
void UsageFault_Handler(void) { while (1); }
void NMI_Handler(void) { }
void SVC_Handler(void) { }
void DebugMon_Handler(void) { }
void PendSV_Handler(void) { }

}

extern "C" {

volatile uint16_t cv[4];
bool gate[4];
bool is_high_freq[4];
uint16_t factory_testing_counter;

uint32_t err_count = 0;
uint32_t txe_count = 0;
uint32_t rxne_count = 0;
uint32_t ovr_count = 0;

void SysTick_Handler() {
  // MIDI I/O, and CV/Gate refresh at 8kHz.
  // UI polling and LED refresh at 1kHz.
  static uint8_t counter;
  if ((++counter & 7) == 0) {
    ui.Poll();
    system_clock.Tick();
  }
  ui.PollFast(); // Display refresh at 8kHz
  
  // Try to read some MIDI input if available.
  if (midi_io.readable()) {
    midi_handler.PushByte(midi_io.ImmediateRead());
  }
  
  // Try to push some MIDI data out.
  if (midi_handler.mutable_high_priority_output_buffer()->readable()) {
    if (midi_io.writable()) {
      midi_io.Overwrite(
          midi_handler.mutable_high_priority_output_buffer()->ImmediateRead());
    }
  }

  if (midi_handler.mutable_output_buffer()->readable()) {
    if (midi_io.writable()) {
      midi_io.Overwrite(midi_handler.mutable_output_buffer()->ImmediateRead());
    }
  }

  bool refresh = (counter & 1) == 0; // Sample rate = 4 kHz
  if (refresh) {
    // Observe that the gate output is written with a systick * 2 (0.25 ms) delay
    // compared to the CV output. This ensures that the CV output will have been
    // refreshed to the right value when the trigger/gate is sent.
    gate_output.Write(gate);
  }
  multi.UpdateResetPulse();
  if (refresh) {
    multi.RefreshInternalClock();
    multi.Refresh();
    multi.GetCvGate(cv, gate);

    is_high_freq[0] = multi.cv_output(0).is_high_freq();
    is_high_freq[1] = multi.cv_output(1).is_high_freq();
    is_high_freq[2] = multi.cv_output(2).is_high_freq();
    is_high_freq[3] = multi.cv_output(3).is_high_freq();
    
    // In calibration mode, overrides the DAC outputs with the raw calibration
    // table values.
    if (ui.calibrating()) {
      const CVOutput& voice = multi.cv_output(ui.calibration_voice());
      cv[ui.calibration_voice()] = voice.calibration_dac_code(
          ui.calibration_note());
    } else if (midi_handler.calibrating()) {
      const CVOutput& voice = multi.cv_output(midi_handler.calibration_voice());
      cv[midi_handler.calibration_voice()] = voice.calibration_dac_code(
          midi_handler.calibration_note());
    }
    
    // In UI testing mode, overrides the GATE values with timers
    if (ui.factory_testing()) {
      gate[0] = (factory_testing_counter % 800) < 400;
      gate[1] = (factory_testing_counter % 400) < 200;
      gate[2] = (factory_testing_counter % 266) < 133;
      gate[3] = (factory_testing_counter % 200) < 100;
      ++factory_testing_counter;
    }
  }
}

void DMA1_Channel6_IRQHandler(void) {
  // static uint32_t debug_counter = 0;

  if(DMA_GetITStatus(DMA1_IT_HT6) == SET) {
    DMA_ClearITPendingBit(DMA1_IT_HT6);
    dac.OnBlockConsumed(true);
    // if (++debug_counter == 0) {
    //   multi.PrintDebugByte(0xC0);
    // }
  }

  if(DMA_GetITStatus(DMA1_IT_TC6) == SET) {
    DMA_ClearITPendingBit(DMA1_IT_TC6);
    dac.OnBlockConsumed(false);
    // if (debug_counter++ % 1000 == 0) {
    //   multi.PrintDebugByte(0xC1);
    // }

    // This adds occasional glitch if DAC is working at all
    // DMA_Cmd(DMA1_Channel2, DISABLE);
    // DMA_Cmd(DMA1_Channel3, DISABLE);
    // DMA_Cmd(DMA1_Channel2, ENABLE);
    // DMA_Cmd(DMA1_Channel3, ENABLE);
  }
}

}

void Init() {
  sys.Init();
  
  setting_defs.Init();
  multi.Init(true);
  ui.Init();

  // Load multi 0 on boot.
  storage_manager.LoadMulti(0);
  storage_manager.LoadCalibration(); // Can disable to reset calibration
  
  system_clock.Init();
  gate_output.Init();
  dac.Init();
  midi_io.Init();
  midi_handler.Init();
  sys.StartTimers();
}

int main(void) {
  Init();
  ui.SplashString(kVersion);
  while (1) {
    // static uint32_t debug_counter = 0;
    // debug_counter++;

    // bool spi2_disabled = !(SPI2->CR1 & SPI_CR1_SPE);

    // FlagStatus txe = SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE);
    // FlagStatus rxne = SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_RXNE);
    // FlagStatus bsy = SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY);
    // FlagStatus ovr = SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_OVR);
    // FlagStatus modf = SPI_I2S_GetFlagStatus(SPI2, SPI_FLAG_MODF);
    // FlagStatus crcerr = SPI_I2S_GetFlagStatus(SPI2, SPI_FLAG_CRCERR);
    
    // uint32_t debug = debug_counter % 100000;
    // if (debug == 10000 * 0) {
    //   multi.PrintDebugByte(0xE0 + spi2_disabled);
    // } else if (debug == 10000 * 1) {
    //   multi.PrintDebugByte(0x10 + txe);
    // } else if (debug == 10000 * 2) {
    //   multi.PrintDebugByte(0x20 + rxne);
    // } else if (debug == 10000 * 3) {
    //   multi.PrintDebugByte(0x30 + bsy);
    // } else if (debug == 10000 * 4) {
    //   multi.PrintDebugByte(0x40 + ovr);
    // } else if (debug == 10000 * 5) {
    //   multi.PrintDebugByte(0x50 + modf);
    // } else if (debug == 10000 * 6) {
    //   multi.PrintDebugByte(0x60 + crcerr);
    // }

    ui.DoEvents();
    midi_handler.ProcessInput();
    multi.LowPriority();
    volatile uint8_t* block_num_ptr = dac.PtrToFillableBlockNum();
    if (block_num_ptr) {
      uint8_t block = *block_num_ptr;
      for (uint8_t channel = 0; channel < kNumCVOutputs; ++channel) {
        multi.mutable_cv_output(channel)->RenderSamples(
          block, channel, cv[channel]
        );
        // multi.PrintDebugByte(0xA0 | channel);
        // multi.PrintDebugByte(RCC_GetClocksFreq());
      }
      dac.OnBlockFilled();
    }
    if (midi_handler.factory_testing_requested()) {
      midi_handler.AcknowledgeFactoryTestingRequest();
      ui.StartFactoryTesting();
    }
  }
}
