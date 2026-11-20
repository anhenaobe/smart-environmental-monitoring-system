CC ?= cc
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
all: uart_probe
uart_probe: uart_probe.c ../firmware/src/env_protocol.c ../firmware/include/env_protocol.h
	$(CC) $(CFLAGS) -I../firmware/include uart_probe.c ../firmware/src/env_protocol.c -o $@
clean:
	rm -f uart_probe
.PHONY: all clean
