CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -pedantic
all: test_protocol

test_protocol: test_protocol.c ../firmware/src/env_protocol.c ../firmware/include/env_protocol.h
	$(CC) $(CFLAGS) -I../firmware/include test_protocol.c ../firmware/src/env_protocol.c -o $@

check: test_protocol
	./test_protocol
clean:
	rm -f test_protocol
.PHONY: all check clean
