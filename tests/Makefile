CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -pedantic
.PHONY: all check clean
all: test_protocol test_firmware_control

test_protocol: test_protocol.c ../firmware/src/env_protocol.c ../firmware/include/env_protocol.h
	$(CC) $(CFLAGS) -I../firmware/include test_protocol.c ../firmware/src/env_protocol.c -o $@

test_firmware_control: test_firmware_control.c ../firmware/src/env_control.c ../firmware/include/env_control.h
	$(CC) $(CFLAGS) -I../firmware/include test_firmware_control.c ../firmware/src/env_control.c -o $@

check: all
	./test_protocol
	./test_firmware_control

clean:
	rm -f test_protocol test_firmware_control
