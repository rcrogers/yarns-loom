#ifndef YARNS_TOOLS_UITEST_GPIO_STUB_H_
#define YARNS_TOOLS_UITEST_GPIO_STUB_H_
#include "yarns/drivers/display.h"
namespace yarns {
// The segment word standing at each character position, and how many times
// that position has been lit. Written by the wire decoder, not the driver.
extern uint16_t g_display_segments[kDisplayWidth];
extern uint32_t g_display_lit_count[kDisplayWidth];
}
#endif
