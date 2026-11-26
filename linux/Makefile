# Native on Linux PC or Duo S, or supply an ABI-compatible cross compiler as CC.
CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
LDFLAGS ?=
LDLIBS ?=
.PHONY: all clean check
all: uart_probe
uart_probe: uart_probe.c ../firmware/src/env_protocol.c ../firmware/include/env_protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I../firmware/include uart_probe.c ../firmware/src/env_protocol.c $(LDFLAGS) $(LDLIBS) -o $@
check: uart_probe
	python3 ../tests/test_uart_virtual.py
clean:
	rm -f uart_probe
