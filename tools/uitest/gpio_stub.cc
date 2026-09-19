// The display's WIRE, decoded.
//
// yarns/drivers/display.cc bit-bangs a 74HC595 chain on GPIOB: a serial data
// pin, a shift clock, a storage-register latch, and one enable pin per
// character. This watches those four and reconstructs what the panel would
// show -- the 16-bit segment word standing at each character position.
//
// So the harness reads the display the way an eye does, not by reaching into
// the driver's members. The enable pins are timer PWM outputs, so the lit edge
// is the driver handing a character a non-zero duty, and the word standing at
// the latch then is what that character lights with.
#define TEST 1
#include <stm32f10x_conf.h>
#include "yarns/drivers/display.h"
#include "tools/uitest/gpio_stub.h"

namespace {

// display.cc's own pin map. Named here rather than shared, because the point
// of this file is to be the OTHER side of that interface.
const uint16_t kPinClk = GPIO_Pin_7;
const uint16_t kPinEnable = GPIO_Pin_8;
const uint16_t kPinData = GPIO_Pin_9;
uint16_t g_pins = 0;
// The 595's two registers: what has been clocked in, and what the latch holds.
uint16_t g_shift_register = 0;
uint8_t g_shift_count = 0;
uint16_t g_latched = 0;

void ApplyPins(uint16_t next) {
  const uint16_t rose = static_cast<uint16_t>(next & ~g_pins);
  // Data is shifted on the LOW-to-HIGH transition of the shift clock, LSB
  // first -- display.cc consumes `data` with `data >>= 1`.
  if (rose & kPinClk) {
    if (next & kPinData) g_shift_register |= 1 << g_shift_count;
    if (++g_shift_count == 16) {
      g_shift_count = 0;
    }
  }
  // The storage register takes the shift register on the latch's rise.
  if (rose & kPinEnable) {
    g_latched = g_shift_register;
    g_shift_register = 0;
    g_shift_count = 0;
  }
  g_pins = next;
}

}  // namespace

namespace yarns {
uint16_t g_display_segments[kDisplayWidth];
uint32_t g_display_lit_count[kDisplayWidth];
}

void TimerCompareWrite(uint8_t character, uint16_t duty) {
  if (character >= yarns::kDisplayWidth || !duty) return;
  yarns::g_display_segments[character] = g_latched;
  ++yarns::g_display_lit_count[character];
}

namespace {
TIM_TypeDef g_tim3 = { { 255 }, { 1 } };
TIM_TypeDef g_tim4 = { { 0 }, { 255 } };
}
TIM_TypeDef* const TIM3 = &g_tim3;
TIM_TypeDef* const TIM4 = &g_tim4;

void TIM_TimeBaseInit(TIM_TypeDef*, TIM_TimeBaseInitTypeDef*) { }
void TIM_OC1Init(TIM_TypeDef*, TIM_OCInitTypeDef*) { }
void TIM_OC2Init(TIM_TypeDef*, TIM_OCInitTypeDef*) { }
void TIM_OC1PreloadConfig(TIM_TypeDef*, uint16_t) { }
void TIM_OC2PreloadConfig(TIM_TypeDef*, uint16_t) { }
void TIM_Cmd(TIM_TypeDef*, FunctionalState) { }
namespace { AFIO_TypeDef g_afio = { 0 }; }
AFIO_TypeDef* const AFIO = &g_afio;

void GpioBitSetReset(uint32_t bits) {
  uint16_t next = g_pins;
  next = static_cast<uint16_t>(next | (bits & 0xFFFF));
  next = static_cast<uint16_t>(next & ~(bits >> 16));
  ApplyPins(next);
}

void GpioBitReset(uint32_t bits) {
  ApplyPins(static_cast<uint16_t>(g_pins & ~(bits & 0xFFFF)));
}

void GPIO_Init(GPIO_TypeDef*, GPIO_InitTypeDef*) { }

uint16_t GpioPinState() { return g_pins; }

namespace {
GPIO_TypeDef g_portb;
}
GPIO_TypeDef* const GPIOB = &g_portb;
