// The vendor SDK header, off target.
//
// yarns/drivers/dac.h includes it and uses nothing from it -- only dac.cc does
// -- so the real header compiles for the host and for the QEMU machine with
// nothing here at all. That is why there is no second copy of dac.h in this
// shim: its constants have one definition.
//
// yarns/drivers/display.cc is the one driver whose LOGIC is worth running off
// target, and it bit-bangs GPIOB directly. The port below is enough to compile
// and drive it. BSRR and BRR are write-only registers on the real part, so
// they are write-only here too: assigning to one calls a hook, and
// tools/uitest/gpio_stub.cc is what implements the hook and decodes the wire.
// A build that never assigns to them never needs that TU.
#ifndef STM32F10X_CONF_H_
#define STM32F10X_CONF_H_

#include <stdint.h>

#define GPIO_Pin_0  ((uint16_t)0x0001)
#define GPIO_Pin_1  ((uint16_t)0x0002)
#define GPIO_Pin_2  ((uint16_t)0x0004)
#define GPIO_Pin_3  ((uint16_t)0x0008)
#define GPIO_Pin_4  ((uint16_t)0x0010)
#define GPIO_Pin_5  ((uint16_t)0x0020)
#define GPIO_Pin_6  ((uint16_t)0x0040)
#define GPIO_Pin_7  ((uint16_t)0x0080)
#define GPIO_Pin_8  ((uint16_t)0x0100)
#define GPIO_Pin_9  ((uint16_t)0x0200)
#define GPIO_Pin_10 ((uint16_t)0x0400)
#define GPIO_Pin_11 ((uint16_t)0x0800)
#define GPIO_Pin_12 ((uint16_t)0x1000)
#define GPIO_Pin_13 ((uint16_t)0x2000)
#define GPIO_Pin_14 ((uint16_t)0x4000)
#define GPIO_Pin_15 ((uint16_t)0x8000)

typedef enum { GPIO_Speed_10MHz = 1, GPIO_Speed_2MHz, GPIO_Speed_50MHz } GPIOSpeed_TypeDef;
typedef enum {
  GPIO_Mode_AIN = 0x0, GPIO_Mode_IN_FLOATING = 0x04, GPIO_Mode_IPD = 0x28,
  GPIO_Mode_IPU = 0x48, GPIO_Mode_Out_OD = 0x14, GPIO_Mode_Out_PP = 0x10,
  GPIO_Mode_AF_OD = 0x1C, GPIO_Mode_AF_PP = 0x18
} GPIOMode_TypeDef;

typedef struct {
  uint16_t GPIO_Pin;
  GPIOSpeed_TypeDef GPIO_Speed;
  GPIOMode_TypeDef GPIO_Mode;
} GPIO_InitTypeDef;

// BSRR: low half sets a pin, high half resets it. BRR: resets.
void GpioBitSetReset(uint32_t bits);
void GpioBitReset(uint32_t bits);

struct GpioBsrrRegister { void operator=(uint32_t bits) { GpioBitSetReset(bits); } };
struct GpioBrrRegister { void operator=(uint32_t bits) { GpioBitReset(bits); } };

struct GPIO_TypeDef {
  GpioBsrrRegister BSRR;
  GpioBrrRegister BRR;
};

extern GPIO_TypeDef* const GPIOB;

void GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init);

// Read back what the pins are doing, for a harness that wants the wire.
uint16_t GpioPinState();

#endif  // STM32F10X_CONF_H_
