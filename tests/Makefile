CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -pedantic
.PHONY: all check clean
all: test_protocol test_firmware_control test_dht11 test_dht11_parser_compat

test_protocol: test_protocol.c ../firmware/src/env_protocol.c ../firmware/include/env_protocol.h
	$(CC) $(CFLAGS) -I../firmware/include test_protocol.c ../firmware/src/env_protocol.c -o $@

test_firmware_control: test_firmware_control.c ../firmware/src/env_control.c ../firmware/include/env_control.h
	$(CC) $(CFLAGS) -I../firmware/include test_firmware_control.c ../firmware/src/env_control.c -o $@

test_dht11: test_dht11.c ../firmware/src/env_dht11.c ../firmware/include/env_dht11.h ../firmware/src/env_protocol.c ../firmware/include/env_protocol.h
	$(CC) $(CFLAGS) -I../firmware/include test_dht11.c ../firmware/src/env_dht11.c ../firmware/src/env_protocol.c -o $@

test_dht11_parser_compat: test_dht11_parser_compat.c ../firmware/src/env_dht11.c ../firmware/include/env_dht11.h ../firmware/src/env_protocol.c ../firmware/include/env_protocol.h ../linux/src/parser.c ../linux/src/parser.h
	$(CC) $(CFLAGS) -I../firmware/include -I../linux/src $< ../firmware/src/env_dht11.c ../firmware/src/env_protocol.c ../linux/src/parser.c -o $@

check: all
	./test_protocol
	./test_firmware_control
	./test_dht11
	./test_dht11_parser_compat

clean:
	rm -f test_protocol test_firmware_control test_dht11 test_dht11_parser_compat
