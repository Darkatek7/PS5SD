# ps5sd-fan.elf, PS5SD's fan modes (main.c). scripts/build-app.sh builds it:
#   make -C ps5/fan PS5_PAYLOAD_SDK=<sdk> ELF=<out>/ps5sd-fan.elf
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF ?= ps5sd-fan.elf

$(ELF): main.c fan_policy.h
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -o $@ main.c -lkernel_sys -lpthread
