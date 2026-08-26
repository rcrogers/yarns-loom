// The vendor SDK header, off target. yarns/drivers/dac.h includes it but uses
// nothing from it -- only dac.cc does -- so the real header compiles for the
// host and for the QEMU machine once this exists. That is why there is no
// second copy of dac.h here: its constants have one definition.
#ifndef STM32F10X_CONF_H_
#define STM32F10X_CONF_H_
#endif
