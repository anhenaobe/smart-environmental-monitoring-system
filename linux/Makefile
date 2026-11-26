# Native on Linux PC or Duo S, or supply an ABI-compatible cross compiler as CC.
CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
LDFLAGS ?=
LDLIBS ?=

# Pruebas unitarias del modulo metrics (CMocka, PU-08 / RNF-03).
TEST_CFLAGS = -std=c11 -g -Wall -Wextra -Werror -Isrc
TEST_LDLIBS = -lcmocka
TEST_SRC = src/metrics.c tests/test_metrics.c
TEST_TARGET = tests/test_metrics

.PHONY: all clean check test valgrind

all: uart_probe

uart_probe: uart_probe.c ../firmware/src/env_protocol.c ../firmware/include/env_protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I../firmware/include uart_probe.c ../firmware/src/env_protocol.c $(LDFLAGS) $(LDLIBS) -o $@

check: uart_probe
	python3 ../tests/test_uart_virtual.py

$(TEST_TARGET): $(TEST_SRC) src/metrics.h
	$(CC) $(TEST_CFLAGS) $(TEST_SRC) -o $@ $(TEST_LDLIBS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

valgrind: $(TEST_TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_TARGET)

clean:
	rm -f uart_probe $(TEST_TARGET)
