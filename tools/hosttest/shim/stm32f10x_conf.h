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

// The character enable pins are timer PWM outputs, so a character's share of
// the light is its compare register. Assigning calls a hook, as BSRR does, and
// tools/uitest/gpio_stub.cc is what implements it.
void TimerCompareWrite(uint8_t character, uint16_t duty);

struct TimCompareRegister {
  uint8_t character;
  void operator=(uint16_t duty) { TimerCompareWrite(character, duty); }
};

struct TIM_TypeDef {
  TimCompareRegister CCR1;
  TimCompareRegister CCR2;
};

extern TIM_TypeDef* const TIM3;
extern TIM_TypeDef* const TIM4;

typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;

typedef struct {
  uint16_t TIM_Prescaler, TIM_CounterMode, TIM_Period, TIM_ClockDivision;
  uint8_t TIM_RepetitionCounter;
} TIM_TimeBaseInitTypeDef;

typedef struct {
  uint16_t TIM_OCMode, TIM_OutputState, TIM_OutputNState, TIM_Pulse;
  uint16_t TIM_OCPolarity, TIM_OCNPolarity, TIM_OCIdleState, TIM_OCNIdleState;
} TIM_OCInitTypeDef;

#define TIM_CKD_DIV1 ((uint16_t)0x0000)
#define TIM_CounterMode_Up ((uint16_t)0x0000)
#define TIM_OCMode_PWM1 ((uint16_t)0x0060)
#define TIM_OutputState_Enable ((uint16_t)0x0001)
#define TIM_OCPolarity_High ((uint16_t)0x0000)
#define TIM_OCPreload_Disable ((uint16_t)0x0000)
#define TIM_OCPreload_Enable ((uint16_t)0x0008)
#define AFIO_MAPR_TIM3_REMAP ((uint32_t)0x00000C00)
#define AFIO_MAPR_TIM3_REMAP_PARTIALREMAP ((uint32_t)0x00000800)

struct AFIO_TypeDef { uint32_t MAPR; };
extern AFIO_TypeDef* const AFIO;

// Configuration the wire cannot show: the harness runs the driver's logic, and
// the panel it reconstructs is the same whatever the timers were set to.
void TIM_TimeBaseInit(TIM_TypeDef* timer, TIM_TimeBaseInitTypeDef* init);
void TIM_OC1Init(TIM_TypeDef* timer, TIM_OCInitTypeDef* init);
void TIM_OC2Init(TIM_TypeDef* timer, TIM_OCInitTypeDef* init);
void TIM_OC1PreloadConfig(TIM_TypeDef* timer, uint16_t preload);
void TIM_OC2PreloadConfig(TIM_TypeDef* timer, uint16_t preload);
void TIM_Cmd(TIM_TypeDef* timer, FunctionalState state);

void GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init);

// Read back what the pins are doing, for a harness that wants the wire.
uint16_t GpioPinState();

#endif  // STM32F10X_CONF_H_
