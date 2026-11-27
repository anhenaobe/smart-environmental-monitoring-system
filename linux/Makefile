# Native on Linux PC or Duo S, or supply an ABI-compatible cross compiler as CC.
CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
LDFLAGS ?=
LDLIBS ?=

# Pruebas unitarias con CMocka: metrics (PU-08 / RNF-03), parser UART (PU-04..PU-06)
# configuracion (PU-07, PU-14, PU-15), alarmas (PU-13) e historial (PU-09, PU-11).
TEST_CFLAGS = -std=c11 -g -Wall -Wextra -Werror -Isrc
TEST_LDLIBS = -lcmocka
TEST_SRC = src/metrics.c tests/test_metrics.c
TEST_TARGET = tests/test_metrics
PARSER_SRC = src/parser.c tests/test_parser.c
PARSER_TARGET = tests/test_parser
CONFIG_SRC = src/config.c tests/test_config.c
CONFIG_TARGET = tests/test_config
ALARM_SRC = src/alarm.c tests/test_alarm.c
ALARM_TARGET = tests/test_alarm
LOGGER_SRC = src/logger.c tests/test_logger.c
LOGGER_TARGET = tests/test_logger
UART_SRC = src/uart.c tests/test_uart.c
UART_TARGET = tests/test_uart
RESET_SRC = src/reset.c tests/test_reset.c
RESET_TARGET = tests/test_reset
SERVICIO_SRC = src/servicio.c src/config.c src/alarm.c src/logger.c src/parser.c src/reset.c tests/test_servicio.c
SERVICIO_TARGET = tests/test_servicio

.PHONY: all host envmond-bin install integracion integracion-valgrind integracion-larga clean check test valgrind test_parser test_config test_alarm test_logger test_uart test_reset test_servicio

# Servicio envmond (Fase B). El mismo Makefile compila en el host x86
# ("make host") y en la Duo S, o con CC=<compilador cruzado RISC-V>.
ENVMOND_SRC = src/envmond.c src/servicio.c src/uart.c src/config.c src/alarm.c \
              src/logger.c src/parser.c src/reset.c
ENVMOND_HDR = src/servicio.h src/uart.h src/config.h src/alarm.h src/logger.h \
              src/parser.h src/reset.h
PREFIX ?= /usr
DESTDIR ?=
SYSTEMD_DIR ?= /lib/systemd/system

all: uart_probe envmond

envmond: $(ENVMOND_SRC) $(ENVMOND_HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $(ENVMOND_SRC) $(LDFLAGS) $(LDLIBS) -lcrypt -o $@

host: envmond

# Instala solo archivos; los pasos de systemd estan en linux/README.md.
install: envmond
	install -D -m 0755 envmond $(DESTDIR)$(PREFIX)/bin/envmond
	install -D -m 0644 systemd/envmond.service $(DESTDIR)$(SYSTEMD_DIR)/envmond.service
	install -D -m 0644 systemd/envmond.sysusers $(DESTDIR)$(PREFIX)/lib/sysusers.d/envmond.conf
	install -D -m 0644 systemd/envmond.tmpfiles $(DESTDIR)$(PREFIX)/lib/tmpfiles.d/envmond.conf

# Integracion en el host con socat (reloj real, ~1 min).
INTEG_BIN = tests/sim_stm32 tests/crear_config
tests/sim_stm32: tests/sim_stm32.c
	$(CC) $(TEST_CFLAGS) tests/sim_stm32.c -o $@

tests/crear_config: tests/crear_config.c src/config.c src/config.h
	$(CC) $(TEST_CFLAGS) tests/crear_config.c src/config.c -o $@ -lcrypt

integracion: envmond $(INTEG_BIN)
	sh tests/integracion.sh corta

integracion-valgrind: envmond $(INTEG_BIN)
	VALGRIND=1 sh tests/integracion.sh corta

# PI-11: 30 min bajo Valgrind (DURACION en segundos).
integracion-larga: envmond $(INTEG_BIN)
	VALGRIND=1 DURACION=$(or $(DURACION),1800) sh tests/integracion.sh larga

uart_probe: uart_probe.c ../firmware/src/env_protocol.c ../firmware/include/env_protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I../firmware/include uart_probe.c ../firmware/src/env_protocol.c $(LDFLAGS) $(LDLIBS) -o $@

check: uart_probe
	python3 ../tests/test_uart_virtual.py

$(TEST_TARGET): $(TEST_SRC) src/metrics.h
	$(CC) $(TEST_CFLAGS) $(TEST_SRC) -o $@ $(TEST_LDLIBS)

$(PARSER_TARGET): $(PARSER_SRC) src/parser.h
	$(CC) $(TEST_CFLAGS) $(PARSER_SRC) -o $@ $(TEST_LDLIBS)

$(CONFIG_TARGET): $(CONFIG_SRC) src/config.h src/alarm.h
	$(CC) $(TEST_CFLAGS) $(CONFIG_SRC) -o $@ $(TEST_LDLIBS) -lcrypt

test_parser: $(PARSER_TARGET)
	./$(PARSER_TARGET)

$(ALARM_TARGET): $(ALARM_SRC) src/alarm.h src/parser.h
	$(CC) $(TEST_CFLAGS) $(ALARM_SRC) -o $@ $(TEST_LDLIBS)

test_config: $(CONFIG_TARGET)
	./$(CONFIG_TARGET)

$(LOGGER_TARGET): $(LOGGER_SRC) src/logger.h src/parser.h
	$(CC) $(TEST_CFLAGS) $(LOGGER_SRC) -o $@ $(TEST_LDLIBS)

test_alarm: $(ALARM_TARGET)
	./$(ALARM_TARGET)

$(UART_TARGET): $(UART_SRC) src/uart.h
	$(CC) $(TEST_CFLAGS) $(UART_SRC) -o $@ $(TEST_LDLIBS)

test_logger: $(LOGGER_TARGET)
	./$(LOGGER_TARGET)

$(RESET_TARGET): $(RESET_SRC) src/reset.h
	$(CC) $(TEST_CFLAGS) $(RESET_SRC) -o $@ $(TEST_LDLIBS)

test_uart: $(UART_TARGET)
	./$(UART_TARGET)

$(SERVICIO_TARGET): $(SERVICIO_SRC) src/servicio.h src/config.h src/alarm.h src/logger.h src/parser.h src/reset.h
	$(CC) $(TEST_CFLAGS) $(SERVICIO_SRC) -o $@ $(TEST_LDLIBS) -lcrypt

test_reset: $(RESET_TARGET)
	./$(RESET_TARGET)

test_servicio: $(SERVICIO_TARGET)
	./$(SERVICIO_TARGET)

test: $(TEST_TARGET) test_parser test_config test_alarm test_logger test_uart test_reset test_servicio
	./$(TEST_TARGET)

valgrind: $(TEST_TARGET) $(PARSER_TARGET) $(CONFIG_TARGET) $(ALARM_TARGET) $(LOGGER_TARGET) $(UART_TARGET) $(RESET_TARGET) $(SERVICIO_TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(PARSER_TARGET)
	valgrind --leak-check=full --error-exitcode=1 --trace-children=yes ./$(CONFIG_TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(ALARM_TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(LOGGER_TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(UART_TARGET)
	valgrind --leak-check=full --error-exitcode=1 --trace-children=yes ./$(RESET_TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(SERVICIO_TARGET)

clean:
	rm -f uart_probe envmond $(INTEG_BIN) $(TEST_TARGET) $(PARSER_TARGET) $(CONFIG_TARGET) $(ALARM_TARGET) $(LOGGER_TARGET) $(UART_TARGET) $(RESET_TARGET) $(SERVICIO_TARGET)
