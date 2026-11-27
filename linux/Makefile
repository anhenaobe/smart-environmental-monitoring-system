# Native on Linux PC or Duo S, or supply an ABI-compatible cross compiler as CC.
CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
LDFLAGS ?=
LDLIBS ?=

# Pruebas unitarias con CMocka: metrics (PU-08 / RNF-03), parser UART (PU-04..PU-06)
# y configuracion (PU-07, PU-14, PU-15).
TEST_CFLAGS = -std=c11 -g -Wall -Wextra -Werror -Isrc
TEST_LDLIBS = -lcmocka
TEST_SRC = src/metrics.c tests/test_metrics.c
TEST_TARGET = tests/test_metrics
PARSER_SRC = src/parser.c tests/test_parser.c
PARSER_TARGET = tests/test_parser
CONFIG_SRC = src/config.c tests/test_config.c
CONFIG_TARGET = tests/test_config

.PHONY: all clean check test valgrind test_parser test_config

all: uart_probe

uart_probe: uart_probe.c ../firmware/src/env_protocol.c ../firmware/include/env_protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I../firmware/include uart_probe.c ../firmware/src/env_protocol.c $(LDFLAGS) $(LDLIBS) -o $@

check: uart_probe
	python3 ../tests/test_uart_virtual.py

$(TEST_TARGET): $(TEST_SRC) src/metrics.h
	$(CC) $(TEST_CFLAGS) $(TEST_SRC) -o $@ $(TEST_LDLIBS)

$(PARSER_TARGET): $(PARSER_SRC) src/parser.h
	$(CC) $(TEST_CFLAGS) $(PARSER_SRC) -o $@ $(TEST_LDLIBS)

$(CONFIG_TARGET): $(CONFIG_SRC) src/config.h
	$(CC) $(TEST_CFLAGS) $(CONFIG_SRC) -o $@ $(TEST_LDLIBS) -lcrypt

test_parser: $(PARSER_TARGET)
	./$(PARSER_TARGET)

test_config: $(CONFIG_TARGET)
	./$(CONFIG_TARGET)

test: $(TEST_TARGET) test_parser test_config
	./$(TEST_TARGET)

valgrind: $(TEST_TARGET) $(PARSER_TARGET) $(CONFIG_TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(PARSER_TARGET)
	valgrind --leak-check=full --error-exitcode=1 --trace-children=yes ./$(CONFIG_TARGET)

clean:
	rm -f uart_probe $(TEST_TARGET) $(PARSER_TARGET) $(CONFIG_TARGET)
