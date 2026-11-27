/*
 * envmond: servicio Linux del monitor ambiental (docs/arquitectura.md §5).
 *
 * Lazo de eventos de un solo hilo de servicio con poll(): UART no
 * bloqueante, signalfd para SIGTERM/SIGINT y un plazo hasta el siguiente
 * latido SYS. Toda la logica vive en servicio.c; aqui solo hay E/S.
 * En la Fase C los hilos HTTP de CivetWeb leeran una instantanea protegida
 * por un mutex corto; este hilo sigue siendo el unico dueno del estado.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <time.h>
#include <unistd.h>

#include "servicio.h"
#include "uart.h"

#define ENVMOND_VERSION "0.2.0"
#define REABRIR_UART_MS 5000

static const char *const UART_DEFECTO = "/dev/ttyS2";   /* UART2 candidata (PI-01) */
static const char *const CONFIG_DEFECTO = "/etc/envmon";
static const char *const DATOS_DEFECTO = "/var/lib/envmon";

typedef struct {
    uart_t uart;
} contexto_t;

static int64_t ahora_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void log_stderr(void *ctx, const char *mensaje) {
    (void)ctx;
    fprintf(stderr, "envmond: %s\n", mensaje);
}

static void tx_uart(void *ctx, const char *carga) {
    contexto_t *c = ctx;
    if (uart_abierto(&c->uart)) (void)uart_enviar(&c->uart, carga);
}

static void uso(FILE *f) {
    fprintf(f,
            "uso: envmond [--uart RUTA] [--config-dir DIR] [--data-dir DIR]\n"
            "  --uart RUTA        puerto serie hacia el STM32 (por defecto %s)\n"
            "  --config-dir DIR   configuracion y reset.pending (por defecto %s)\n"
            "  --data-dir DIR     historial CSV (por defecto %s)\n"
            "  --version          muestra la version\n",
            UART_DEFECTO, CONFIG_DEFECTO, DATOS_DEFECTO);
}

static void falla_uart(contexto_t *c, int64_t *reabrir, const char *que) {
    fprintf(stderr, "envmond: uart: %s en %s (%s); reintento en %d s\n", que, c->uart.ruta,
            strerror(errno), REABRIR_UART_MS / 1000);
    uart_cerrar(&c->uart);
    *reabrir = ahora_ms() + REABRIR_UART_MS;
}

int main(int argc, char **argv) {
    const char *ruta_uart = UART_DEFECTO;
    const char *dir_config = CONFIG_DEFECTO;
    const char *dir_datos = DATOS_DEFECTO;
    static const struct option opciones[] = {
        { "uart", required_argument, NULL, 'u' },
        { "config-dir", required_argument, NULL, 'c' },
        { "data-dir", required_argument, NULL, 'd' },
        { "version", no_argument, NULL, 'V' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };
    int op;
    while ((op = getopt_long(argc, argv, "u:c:d:Vh", opciones, NULL)) != -1) {
        switch (op) {
        case 'u': ruta_uart = optarg; break;
        case 'c': dir_config = optarg; break;
        case 'd': dir_datos = optarg; break;
        case 'V': puts("envmond " ENVMOND_VERSION); return 0;
        case 'h': uso(stdout); return 0;
        default: uso(stderr); return 2;
        }
    }
    if (optind != argc) { uso(stderr); return 2; }
    setvbuf(stderr, NULL, _IOLBF, 0);  /* una linea por mensaje en journald */

    /* Las senales se atienden en el lazo; se bloquean antes de crear hilos
     * (Fase C) para que ningun hilo HTTP las reciba. */
    sigset_t senales;
    sigemptyset(&senales);
    sigaddset(&senales, SIGTERM);
    sigaddset(&senales, SIGINT);
    if (sigprocmask(SIG_BLOCK, &senales, NULL) != 0) { perror("envmond: sigprocmask"); return 1; }
    signal(SIGPIPE, SIG_IGN);
    const int sfd = signalfd(-1, &senales, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sfd < 0) { perror("envmond: signalfd"); return 1; }

    static contexto_t c;
    uart_init(&c.uart);
    static servicio_t s;
    const servicio_params_t p = { dir_config, dir_datos, tx_uart, log_stderr, &c, NULL };
    fprintf(stderr, "envmond: version %s, uart %s, config %s, datos %s\n", ENVMOND_VERSION,
            ruta_uart, dir_config, dir_datos);
    if (servicio_init(&s, &p, ahora_ms(), time(NULL)) != 0) {
        fprintf(stderr, "envmond: directorios no validos\n");
        close(sfd);
        return 2;
    }

    int64_t reabrir = 0;
    if (uart_abrir(&c.uart, ruta_uart) != 0) falla_uart(&c, &reabrir, "no se pudo abrir");

    bool salir = false;
    while (!salir) {
        int64_t t = ahora_ms();
        servicio_tick(&s, t, time(NULL));
        if (uart_abierto(&c.uart) && uart_tx_pendiente(&c.uart) && uart_vaciar(&c.uart) != 0)
            falla_uart(&c, &reabrir, "error de escritura");
        if (!uart_abierto(&c.uart) && t >= reabrir) {
            if (uart_abrir(&c.uart, ruta_uart) == 0)
                fprintf(stderr, "envmond: uart: %s abierto\n", ruta_uart);
            else
                falla_uart(&c, &reabrir, "no se pudo abrir");
        }

        int plazo = servicio_plazo_ms(&s, t);
        if (!uart_abierto(&c.uart) && reabrir - t < plazo) plazo = (int)(reabrir - t > 0 ? reabrir - t : 0);
        struct pollfd pfd[2] = { { .fd = sfd, .events = POLLIN }, { .fd = -1 } };
        nfds_t n = 1;
        if (uart_abierto(&c.uart)) {
            pfd[1].fd = c.uart.fd;
            pfd[1].events = (short)(POLLIN | (uart_tx_pendiente(&c.uart) ? POLLOUT : 0));
            n = 2;
        }
        if (poll(pfd, n, plazo) < 0) {
            if (errno == EINTR) continue;
            perror("envmond: poll");
            break;
        }
        if (pfd[0].revents & POLLIN) {
            struct signalfd_siginfo si;
            if (read(sfd, &si, sizeof(si)) == (ssize_t)sizeof(si))
                fprintf(stderr, "envmond: senal %u, deteniendo\n", (unsigned)si.ssi_signo);
            salir = true;
        }
        if (n == 2 && pfd[1].revents != 0) {
            const short ev = pfd[1].revents;
            bool fallo = false;
            if (ev & POLLIN) {
                uint8_t buf[512];
                ssize_t r;
                while ((r = uart_leer(&c.uart, buf, sizeof(buf))) > 0)
                    servicio_bytes(&s, buf, (size_t)r, ahora_ms(), time(NULL));
                if (r < 0) fallo = true;
            }
            if (!fallo && (ev & POLLOUT) && uart_vaciar(&c.uart) != 0) fallo = true;
            if (!fallo && (ev & (POLLERR | POLLNVAL | POLLHUP)) && !(ev & POLLIN)) {
                errno = EIO;
                fallo = true;
            }
            if (fallo) falla_uart(&c, &reabrir, "el puerto fallo");
        }
    }

    servicio_detener(&s);
    uart_cerrar(&c.uart);
    close(sfd);
    return 0;
}
