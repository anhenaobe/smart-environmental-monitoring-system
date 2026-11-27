/*
 * Puerto serie de envmond (ver uart.h).
 */
#define _GNU_SOURCE

#include "uart.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

void uart_init(uart_t *u) {
    memset(u, 0, sizeof(*u));
    u->fd = -1;
}

bool uart_abierto(const uart_t *u) { return u->fd >= 0; }

int uart_abrir(uart_t *u, const char *ruta) {
    uart_cerrar(u);
    const size_t n = strlen(ruta);
    if (n >= sizeof(u->ruta)) { errno = ENAMETOOLONG; return -1; }
    memcpy(u->ruta, ruta, n + 1u);

    int fd = open(ruta, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    struct termios t;
    if (tcgetattr(fd, &t) != 0) goto fallo;
    cfmakeraw(&t);                       /* sin eco, sin traducir \r\n */
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~(tcflag_t)(CSTOPB | PARENB | CRTSCTS | CSIZE);
    t.c_cflag |= CS8;                    /* 8N1 */
    t.c_iflag &= ~(tcflag_t)(IXON | IXOFF | IXANY);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (cfsetispeed(&t, B115200) != 0 || cfsetospeed(&t, B115200) != 0) goto fallo;
    if (tcsetattr(fd, TCSANOW, &t) != 0) goto fallo;
    (void)tcflush(fd, TCIOFLUSH);        /* descartar basura previa */
    u->fd = fd;
    u->tx_len = 0;
    return 0;
fallo:;
    const int e = errno;
    close(fd);
    errno = e;
    return -1;
}

void uart_cerrar(uart_t *u) {
    if (u->fd >= 0) close(u->fd);
    u->fd = -1;
    u->tx_len = 0;
}

ssize_t uart_leer(uart_t *u, uint8_t *buf, size_t n) {
    if (u->fd < 0) return -1;
    for (;;) {
        ssize_t r = read(u->fd, buf, n);
        if (r > 0) { u->bytes_rx += (uint32_t)r; return r; }
        if (r == 0) return 0;  /* VMIN = 0: sin datos */
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
}

size_t uart_formatear(const char *carga, char *out, size_t n) {
    uint8_t cs = 0;
    size_t len = 0;
    for (const char *p = carga; *p != '\0'; p++, len++) {
        const unsigned char c = (unsigned char)*p;
        if (c < 0x20u || c > 0x7Eu || c == '$' || c == '*') return 0;
        cs ^= c;
    }
    if (len == 0u) return 0;
    int r = snprintf(out, n, "$%s*%02X\r\n", carga, (unsigned)cs);
    return (r < 0 || (size_t)r >= n) ? 0u : (size_t)r;
}

int uart_vaciar(uart_t *u) {
    if (u->fd < 0) return -1;
    while (u->tx_len > 0u) {
        ssize_t r = write(u->fd, u->tx, u->tx_len);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        u->bytes_tx += (uint32_t)r;
        memmove(u->tx, u->tx + r, u->tx_len - (size_t)r);
        u->tx_len -= (size_t)r;
    }
    return 0;
}

int uart_enviar(uart_t *u, const char *carga) {
    char trama[64];
    const size_t n = uart_formatear(carga, trama, sizeof(trama));
    if (u->fd < 0 || n == 0u || u->tx_len + n > sizeof(u->tx)) {
        u->tramas_descartadas_tx++;
        return -1;
    }
    memcpy(u->tx + u->tx_len, trama, n);
    u->tx_len += n;
    u->tramas_tx++;
    return uart_vaciar(u);
}

bool uart_tx_pendiente(const uart_t *u) { return u->tx_len > 0u; }
