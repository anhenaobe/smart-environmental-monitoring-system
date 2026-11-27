/*
 * Simulador del STM32 para la integracion en host (socat + envmond).
 *
 * uso: sim_stm32 <tty> <orden>...
 *   env T H N     N tramas ENV validas (T °C, H % HR) cada 3 s
 *   falla N       N tramas ENV con estado 1 (sensor sin respuesta) cada 3 s
 *   sta           una trama STA
 *   btn           $BTN,RST (factory reset)
 *   basura        una trama con checksum malo, un tipo desconocido y ruido
 *   espera S      solo escuchar S segundos
 *   aleatorio S   ENV cada 3 s durante S segundos, con valores y errores
 *                 pseudoaleatorios (prueba larga, PI-11)
 * Imprime en stdout cada linea recibida de envmond: "<ms> RX <trama>".
 * No forma parte del producto: es una herramienta de prueba.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int g_fd;
static int64_t g_t0;
static uint32_t g_seq;
static char g_linea[128];
static size_t g_len;

static int64_t ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void escribir(const char *b, size_t n) {
    while (n > 0) {
        ssize_t r = write(g_fd, b, n);
        if (r < 0) {
            if (errno == EINTR || errno == EAGAIN) { usleep(1000); continue; }
            perror("sim: write");
            exit(1);
        }
        b += r;
        n -= (size_t)r;
    }
}

static void trama(const char *carga) {
    unsigned cs = 0;
    for (const char *p = carga; *p; p++) cs ^= (unsigned char)*p;
    char b[96];
    int n = snprintf(b, sizeof(b), "$%s*%02X\r\n", carga, cs);
    escribir(b, (size_t)n);
}

/* "$SYS,1*44" valida -> "<ms> RX SYS,1"; con checksum malo -> "RX MALA ...". */
static void imprimir(const char *l) {
    const char *ast = strrchr(l, '*');
    unsigned cs = 0, leido = 0;
    bool ok = l[0] == '$' && ast != NULL && sscanf(ast + 1, "%2X", &leido) == 1 && strlen(ast) == 3;
    if (ok) {
        for (const char *p = l + 1; p < ast; p++) cs ^= (unsigned char)*p;
        ok = cs == leido;
    }
    if (ok) printf("%lld RX %.*s\n", (long long)(ms() - g_t0), (int)(ast - l - 1), l + 1);
    else printf("%lld RX MALA %s\n", (long long)(ms() - g_t0), l);
}

/* Escucha hasta `hasta` (ms monotonicos) e imprime las lineas recibidas. */
static void escuchar_hasta(int64_t hasta) {
    for (;;) {
        int64_t falta = hasta - ms();
        if (falta <= 0) return;
        struct pollfd p = { .fd = g_fd, .events = POLLIN };
        if (poll(&p, 1, (int)falta) <= 0) continue;
        char b[256];
        ssize_t r = read(g_fd, b, sizeof(b));
        for (ssize_t i = 0; i < r; i++) {
            if (b[i] == '\n') {
                if (g_len > 0 && g_linea[g_len - 1] == '\r') g_len--;
                g_linea[g_len] = '\0';
                imprimir(g_linea);
                fflush(stdout);
                g_len = 0;
            } else if (g_len + 1 < sizeof(g_linea)) {
                g_linea[g_len++] = b[i];
            }
        }
    }
}

static void env(int t, int h, int estado) {
    char c[64];
    if (estado == 0)
        snprintf(c, sizeof(c), "ENV,%08u,%c%05d,%05d,0", (unsigned)g_seq, t < 0 ? '-' : '+',
                 abs(t), h);
    else
        snprintf(c, sizeof(c), "ENV,%08u,,,%d", (unsigned)g_seq, estado);
    g_seq = (g_seq + 1u) % 100000000u;
    trama(c);
}

static void cada_3_s(int n, int t, int h, int estado) {
    for (int i = 0; i < n; i++) {
        int64_t inicio = ms();
        env(t, h, estado);
        escuchar_hasta(inicio + 3000);
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "uso: sim_stm32 <tty> <orden>...\n");
        return 2;
    }
    g_fd = open(argv[1], O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (g_fd < 0) { perror("sim: open"); return 1; }
    struct termios tio;
    if (tcgetattr(g_fd, &tio) == 0) {
        cfmakeraw(&tio);
        cfsetispeed(&tio, B115200);
        cfsetospeed(&tio, B115200);
        tcsetattr(g_fd, TCSANOW, &tio);
    }
    g_t0 = ms();
    for (int i = 2; i < argc; i++) {
        const char *o = argv[i];
        if (strcmp(o, "env") == 0 && i + 3 < argc) {
            int t = atoi(argv[i + 1]) * 100, h = atoi(argv[i + 2]) * 100, n = atoi(argv[i + 3]);
            i += 3;
            cada_3_s(n, t, h, 0);
        } else if (strcmp(o, "falla") == 0 && i + 1 < argc) {
            cada_3_s(atoi(argv[++i]), 0, 0, 1);
        } else if (strcmp(o, "sta") == 0) {
            trama("STA,00000042,0,0,0.3.0,00850,02048");
        } else if (strcmp(o, "btn") == 0) {
            trama("BTN,RST");
        } else if (strcmp(o, "basura") == 0) {
            const char *malas = "$ENV,00009999,+02450,05310,0*00\r\nruido";
            escribir(malas, strlen(malas));  /* checksum malo + bytes sueltos */
            trama("ABC,1");                  /* tipo desconocido */
        } else if (strcmp(o, "espera") == 0 && i + 1 < argc) {
            escuchar_hasta(ms() + 1000LL * atoi(argv[++i]));
        } else if (strcmp(o, "aleatorio") == 0 && i + 1 < argc) {
            const int64_t fin = ms() + 1000LL * atoi(argv[++i]);
            uint32_t x = 2463534242u;
            while (ms() < fin) {
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                int64_t inicio = ms();
                if (x % 50u == 0u) {
                    escribir("$ENV,0*00\r\n", 11);          /* trama mala */
                } else if (x % 20u == 0u) {
                    env(0, 0, 1 + (int)(x % 3u));            /* falla del sensor */
                } else {
                    env(1500 + (int)(x % 1200u), 3000 + (int)((x >> 8) % 3000u), 0);
                }
                if (x % 7u == 0u) trama("STA,00000042,0,0,0.3.0,00850,02048");
                escuchar_hasta(inicio + 3000);
            }
        } else {
            fprintf(stderr, "sim: orden desconocida: %s\n", o);
            return 2;
        }
    }
    close(g_fd);
    return 0;
}
