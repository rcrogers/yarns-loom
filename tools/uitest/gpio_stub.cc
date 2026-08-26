// The display's WIRE, decoded.
//
// yarns/drivers/display.cc bit-bangs a 74HC595 chain on GPIOB: a serial data
// pin, a shift clock, a storage-register latch, and one enable pin per
// character. This watches those four and reconstructs what the panel would
// show -- the 16-bit segment word standing at each character position.
//
// So the harness reads the display the way an eye does, not by reaching into
// the driver's members. What it cannot see is brightness: RefreshFast's PWM
// blanks a position by driving its enable low, and this records the word at
// each enable RISE, which is the lit edge.
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
const uint16_t kCharacterEnablePins[yarns::kDisplayWidth] = {
  GPIO_Pin_6, GPIO_Pin_5
};

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
  // A character lights when its enable rises, and the word standing at the
  // latch is what it lights with: display.cc shifts, then enables.
  for (uint8_t i = 0; i < yarns::kDisplayWidth; ++i) {
    if (rose & kCharacterEnablePins[i]) {
      yarns::g_display_segments[i] = g_latched;
      ++yarns::g_display_lit_count[i];
    }
  }
  g_pins = next;
}

}  // namespace

namespace yarns {
uint16_t g_display_segments[kDisplayWidth];
uint32_t g_display_lit_count[kDisplayWidth];
}

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
