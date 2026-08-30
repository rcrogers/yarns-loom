# Copyright 2013 Emilie Gillet.
#
# Author: Emilie Gillet (emilie.o.gillet@gmail.com)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
# 
# See http://creativecommons.org/licenses/MIT/ for more information.

# System specifications
F_CRYSTAL      = 8000000L
F_CPU          = 72000000L
SYSCLOCK       = SYSCLK_FREQ_72MHz
FAMILY         = f10x
DENSITY        = md
MEMORY_MODE    = flash
# USB            = enabled

BOOTLOADER     = yarns_bootloader

APPLICATION_SMALL = TRUE

# Preferred upload command
UPLOAD_COMMAND  = upload_combo_jtag

# Packages to build
TARGET         = yarns
PACKAGES       = yarns yarns/drivers stmlib/utils stmlib/system
RESOURCES      = yarns/resources

TOOLCHAIN_PATH ?= /usr/local/arm-4.8.3/

# Stack budget bookkeeping.
#
# STACK_FRAME_WARN: per-function frame threshold for -Wframe-larger-than. This
# is a ceiling on a SINGLE function's stack frame, not total stack. Any
# individual frame larger than this is a smell worth explicit sign-off.
# -Wframe-larger-than (not -Wstack-usage) is used because naked-asm functions
# in third-party CMSIS error out under -Wstack-usage + -Werror.
#
# STACK_RESERVATION: total bytes between _sstack and _estack in the linker
# script. Must match stm32f10x_flash_md_application_small.ld; that file has
# the cumulative-stack math behind the 2048 figure. This value is passed into
# C++ as STACK_RESERVATION_BYTES for the static_assert in yarns/stack_budget.h
# that cross-checks the sum of known large stack locals against the reservation.
STACK_FRAME_WARN    = 512
STACK_RESERVATION   = 2048
STACK_CCFLAGS       = -Wframe-larger-than=$(STACK_FRAME_WARN) -fstack-usage \
                      -DSTACK_RESERVATION_BYTES=$(STACK_RESERVATION)

# Total on-chip SRAM. Must match the RAM region LENGTH in
# stm32f10x_flash_md_application_small.ld (20K for STM32F10x MD). Used by the
# `syx` rule to report free RAM = RAM_SIZE - (data + bss) - STACK_RESERVATION.
RAM_SIZE            = 20480

include stmlib/makefile.inc

MAKEFLAGS += -j8

# Rules for building the SysEx update file.
SYSEX_FLAGS    = --page_size=512 --device_id=11

HEX2SYSEX = python tools/hex2sysex/hex2sysex.py

$(BUILD_DIR)%.syx: $(BUILD_DIR)%.bin
	$(HEX2SYSEX) $(SYSEX_FLAGS) --syx -o $@ $<

# Preset-storage budget, read back out of a probe that is compiled, never linked.
STORAGE_PROBE_SRC = yarns/tools/storage_budget_probe.cc
STORAGE_PROBE_OBJ = $(BUILD_DIR)storage_budget_probe.o

# No generated dep file out here, and a stale budget is worse than a compile.
.PHONY: always_rebuild
always_rebuild:

$(STORAGE_PROBE_OBJ): $(STORAGE_PROBE_SRC) always_rebuild | $(BUILD_DIR)
	$(CXX) -c $(CCFLAGS) $(CPPFLAGS) $< -o $@

# Each asserts that its scope's kFreeBits is not understated. Compiling them is
# the whole test; they report nothing.
FREE_BITS_CHECK_OBJS = $(BUILD_DIR)free_bits_check_part.o \
                       $(BUILD_DIR)free_bits_check_multi.o

$(BUILD_DIR)free_bits_check_%.o: yarns/tools/free_bits_check_%.cc always_rebuild | $(BUILD_DIR)
	$(CXX) -c $(CCFLAGS) $(CPPFLAGS) $< -o $@

syx: $(BUILD_DIR)$(TARGET).syx $(STORAGE_PROBE_OBJ) $(FREE_BITS_CHECK_OBJS)
	@$(SIZE) $(TARGET_ELF) | awk 'NR==2 { \
	  used = $$2 + $$3; \
	  free = $(RAM_SIZE) - used - $(STACK_RESERVATION); \
	  printf "(%cram_used%c, %d)\n(%cram_free%c, %d)\n", 39, 39, used, 39, 39, free }'
	@$(NM) -t d --print-size --defined-only $(STORAGE_PROBE_OBJ) | \
	  awk '$$4 ~ /^probe_/ { sub(/^probe_[0-9]+_/, "", $$4); \
	    printf "(%cpreset_%s%c, %d)\n", 39, $$4, 39, $$2 - 1 }'