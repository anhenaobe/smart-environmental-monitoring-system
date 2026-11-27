/*
 * Pruebas de desarrollo del modulo uart de envmond: formato de las tramas
 * SYS/ACK (vectores de docs/protocolo-uart.md) y E/S no bloqueante sobre un
 * pseudoterminal, sin hardware.
 */
#define _GNU_SOURCE

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cmocka.h>
#include "uart.h"

static void formato_vectores_del_protocolo(void **state) {
    (void)state;
    char b[64];
    assert_int_equal(uart_formatear("SYS,0", b, sizeof(b)), 11);
    assert_string_equal(b, "$SYS,0*45\r\n");
    uart_formatear("SYS,1", b, sizeof(b)); assert_string_equal(b, "$SYS,1*44\r\n");
    uart_formatear("SYS,3", b, sizeof(b)); assert_string_equal(b, "$SYS,3*46\r\n");
    uart_formatear("ACK,RST", b, sizeof(b)); assert_string_equal(b, "$ACK,RST*30\r\n");
}

static void formato_rechaza_cargas_invalidas(void **state) {
    (void)state;
    char b[64];
    assert_int_equal(uart_formatear("", b, sizeof(b)), 0);
    assert_int_equal(uart_formatear("SYS*1", b, sizeof(b)), 0);
    assert_int_equal(uart_formatear("$SYS,1", b, sizeof(b)), 0);
    assert_int_equal(uart_formatear("SYS,1\n", b, sizeof(b)), 0);
    assert_int_equal(uart_formatear("SYS,1", b, 11), 0);  /* no cabe el '\0' */
}

/* Pseudoterminal: el "STM32" usa el maestro y envmond abre el esclavo. */
static int abrir_pty(char *esclavo, size_t n) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    assert_true(m >= 0);
    assert_int_equal(grantpt(m), 0);
    assert_int_equal(unlockpt(m), 0);
    assert_int_equal(ptsname_r(m, esclavo, n), 0);
    struct termios t;
    assert_int_equal(tcgetattr(m, &t), 0);
    cfmakeraw(&t);
    assert_int_equal(tcsetattr(m, TCSANOW, &t), 0);
    return m;
}

static size_t leer_con_espera(int fd, char *buf, size_t n, size_t esperado) {
    size_t total = 0;
    for (int i = 0; i < 50 && total < esperado; i++) {
        struct pollfd p = { .fd = fd, .events = POLLIN };
        if (poll(&p, 1, 20) > 0) {
            ssize_t r = read(fd, buf + total, n - total);
            if (r > 0) total += (size_t)r;
        }
    }
    return total;
}

static void pty_ida_y_vuelta(void **state) {
    (void)state;
    char esclavo[128];
    int m = abrir_pty(esclavo, sizeof(esclavo));
    uart_t u;
    uart_init(&u);
    assert_false(uart_abierto(&u));
    assert_int_equal(uart_abrir(&u, esclavo), 0);
    assert_true(uart_abierto(&u));

    struct termios t;
    assert_int_equal(tcgetattr(u.fd, &t), 0);
    assert_int_equal(cfgetispeed(&t), B115200);
    assert_int_equal(cfgetospeed(&t), B115200);
    assert_int_equal(t.c_cflag & CSIZE, CS8);
    assert_int_equal(t.c_cflag & (PARENB | CSTOPB | CRTSCTS), 0);
    assert_int_equal(t.c_lflag & (ICANON | ECHO), 0);
    assert_true((fcntl(u.fd, F_GETFL) & O_NONBLOCK) != 0);

    /* Sin datos: no bloquea. */
    uint8_t buf[128];
    assert_int_equal(uart_leer(&u, buf, sizeof(buf)), 0);

    /* envmond -> STM32 */
    assert_int_equal(uart_enviar(&u, "SYS,1"), 0);
    assert_int_equal(uart_enviar(&u, "ACK,RST"), 0);
    char rx[128];
    size_t n = leer_con_espera(m, rx, sizeof(rx), 24);
    assert_int_equal(n, 24);
    assert_memory_equal(rx, "$SYS,1*44\r\n$ACK,RST*30\r\n", 24);
    assert_int_equal(u.tramas_tx, 2);
    assert_int_equal(u.bytes_tx, 24);
    assert_false(uart_tx_pendiente(&u));

    /* STM32 -> envmond, sin traducir \r\n */
    const char *env = "$ENV,00000042,+02450,05310,0*44\r\n";
    assert_int_equal(write(m, env, strlen(env)), (ssize_t)strlen(env));
    size_t total = 0;
    for (int i = 0; i < 50 && total < strlen(env); i++) {
        ssize_t r = uart_leer(&u, buf + total, sizeof(buf) - total);
        assert_true(r >= 0);
        total += (size_t)r;
        if (r == 0) usleep(10000);
    }
    assert_int_equal(total, strlen(env));
    assert_memory_equal(buf, env, total);
    assert_int_equal(u.bytes_rx, strlen(env));

    uart_cerrar(&u);
    assert_false(uart_abierto(&u));
    assert_int_equal(uart_enviar(&u, "SYS,1"), -1);  /* cerrado: se descarta */
    assert_int_equal(u.tramas_descartadas_tx, 1);
    close(m);
}

static void abrir_ruta_inexistente_falla(void **state) {
    (void)state;
    uart_t u;
    uart_init(&u);
    assert_int_equal(uart_abrir(&u, "/dev/no-existe-envmond"), -1);
    assert_int_equal(errno, ENOENT);
    assert_false(uart_abierto(&u));
}

int main(void) {
    const struct CMUnitTest pruebas[] = {
        cmocka_unit_test(formato_vectores_del_protocolo),
        cmocka_unit_test(formato_rechaza_cargas_invalidas),
        cmocka_unit_test(pty_ida_y_vuelta),
        cmocka_unit_test(abrir_ruta_inexistente_falla),
    };
    return cmocka_run_group_tests_name("uart", pruebas, NULL, NULL);
}
