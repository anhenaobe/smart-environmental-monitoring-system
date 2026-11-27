/*
 * Historial agregado de envmond (ver logger.h).
 */
#define _GNU_SOURCE

#include "logger.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FILA_MAX 192u
#define NOMBRE_CSV_LEN 14u  /* "AAAA-MM-DD.csv" */

/* Dias desde 1970-01-01 para una fecha civil (algoritmo de H. Hinnant). */
static int64_t dias_desde_civil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static bool fecha_local(time_t t, struct tm *tm) { return localtime_r(&t, tm) != NULL; }

static int64_t dia_local(const struct tm *tm) {
    return dias_desde_civil(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}

/* Inicio del intervalo que contiene t (division hacia abajo, tambien < 0). */
static time_t alinear(time_t t, uint32_t intervalo) {
    const int64_t i = (int64_t)intervalo;
    int64_t q = (int64_t)t / i;
    if ((int64_t)t % i < 0) q--;
    return (time_t)(q * i);
}

static void reiniciar_acumulado(logger_t *lg) {
    lg->n = 0;
    lg->suma_temp = 0;
    lg->suma_hr = 0;
}

int logger_init(logger_t *lg, const char *dir, uint32_t intervalo_s, uint32_t retencion_dias) {
    memset(lg, 0, sizeof(*lg));
    if (dir == NULL || intervalo_s == 0u || retencion_dias == 0u) return -1;
    const size_t n = strlen(dir);
    if (n == 0u || n >= sizeof(lg->dir)) return -1;
    memcpy(lg->dir, dir, n + 1u);
    lg->intervalo_s = intervalo_s;
    lg->retencion_dias = retencion_dias;
    return 0;
}

/* Promedio redondeado al entero mas cercano (mitades lejos de cero). */
static int32_t promedio(int64_t suma, uint32_t n) {
    const int64_t n2 = 2 * (int64_t)n;
    const int64_t s2 = 2 * suma;
    return (int32_t)(s2 >= 0 ? (s2 + n) / n2 : (s2 - n) / n2);
}

static void centi(char *buf, size_t n, int32_t v) {
    const int32_t a = v < 0 ? -v : v;
    snprintf(buf, n, "%s%d.%02d", v < 0 ? "-" : "", (int)(a / 100), (int)(a % 100));
}

static int formatear_fila(const logger_t *lg, char *buf, size_t n) {
    const int hora_ok = lg->inicio >= LOGGER_HORA_MINIMA ? 1 : 0;
    int r;
    if (lg->n == 0u) {
        r = snprintf(buf, n, "%lld,%d,0,,,,,,\n", (long long)lg->inicio, hora_ok);
    } else {
        char tp[16], tmin[16], tmax[16], hp[16], hmin[16], hmax[16];
        centi(tp, sizeof(tp), promedio(lg->suma_temp, lg->n));
        centi(tmin, sizeof(tmin), lg->min_temp);
        centi(tmax, sizeof(tmax), lg->max_temp);
        centi(hp, sizeof(hp), promedio(lg->suma_hr, lg->n));
        centi(hmin, sizeof(hmin), lg->min_hr);
        centi(hmax, sizeof(hmax), lg->max_hr);
        r = snprintf(buf, n, "%lld,%d,%u,%s,%s,%s,%s,%s,%s\n", (long long)lg->inicio, hora_ok,
                     (unsigned)lg->n, tp, tmin, tmax, hp, hmin, hmax);
    }
    return (r < 0 || (size_t)r >= n) ? -1 : r;
}

static int sincronizar_dir(const char *dir) {
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    int r = fsync(fd);
    if (r != 0 && errno == EINVAL) r = 0;
    close(fd);
    return r;
}

static int escribir_todo(int fd, const char *buf, size_t n) {
    while (n > 0u) {
        ssize_t r = write(fd, buf, n);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf += r;
        n -= (size_t)r;
    }
    return 0;
}

/* Agrega la fila del intervalo en curso al CSV de su dia local: una sola
 * escritura y un fsync por intervalo (RNF-08). */
static int escribir_fila(const logger_t *lg) {
    struct tm tm;
    if (!fecha_local(lg->inicio, &tm)) return -1;
    char ruta[LOGGER_DIR_MAX + 32u];
    int r = snprintf(ruta, sizeof(ruta), "%s/%04d-%02d-%02d.csv", lg->dir, tm.tm_year + 1900,
                     tm.tm_mon + 1, tm.tm_mday);
    if (r < 0 || (size_t)r >= sizeof(ruta)) return -1;

    char fila[FILA_MAX];
    const int len_fila = formatear_fila(lg, fila, sizeof(fila));
    if (len_fila < 0) return -1;

    int fd = open(ruta, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return -1; }

    char buf[sizeof(LOGGER_CABECERA) + FILA_MAX + 1u];
    size_t len = 0;
    const bool nuevo = st.st_size == 0;
    if (nuevo) {
        memcpy(buf, LOGGER_CABECERA, sizeof(LOGGER_CABECERA) - 1u);
        len = sizeof(LOGGER_CABECERA) - 1u;
    } else {
        /* Si un corte dejo una fila a medias, la nueva empieza en otra linea. */
        char ultimo = '\n';
        if (pread(fd, &ultimo, 1, st.st_size - 1) == 1 && ultimo != '\n') buf[len++] = '\n';
    }
    memcpy(buf + len, fila, (size_t)len_fila);
    len += (size_t)len_fila;

    int res = escribir_todo(fd, buf, len);
    if (res == 0) res = fsync(fd);
    if (close(fd) != 0) res = -1;
    if (res == 0 && nuevo) res = sincronizar_dir(lg->dir);
    return res == 0 ? 0 : -1;
}

static bool nombre_csv(const char *s, int64_t *dia) {
    if (strlen(s) != NOMBRE_CSV_LEN || strcmp(s + 10, ".csv") != 0) return false;
    static const size_t pos_digitos[] = { 0, 1, 2, 3, 5, 6, 8, 9 };
    for (size_t i = 0; i < sizeof(pos_digitos) / sizeof(pos_digitos[0]); i++)
        if (s[pos_digitos[i]] < '0' || s[pos_digitos[i]] > '9') return false;
    if (s[4] != '-' || s[7] != '-') return false;
    const int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    const int m = (s[5] - '0') * 10 + (s[6] - '0');
    const int d = (s[8] - '0') * 10 + (s[9] - '0');
    if (m < 1 || m > 12 || d < 1 || d > 31) return false;
    *dia = dias_desde_civil(y, m, d);
    return true;
}

int logger_retener(logger_t *lg, time_t ahora) {
    struct tm tm;
    if (!fecha_local(ahora, &tm)) return -1;
    const int64_t primero_que_queda = dia_local(&tm) - ((int64_t)lg->retencion_dias - 1);
    DIR *d = opendir(lg->dir);
    if (d == NULL) return -1;
    int borrados = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int64_t dia;
        if (!nombre_csv(e->d_name, &dia) || dia >= primero_que_queda) continue;
        if (unlinkat(dirfd(d), e->d_name, 0) == 0) borrados++;
    }
    closedir(d);
    return borrados;
}

static int retener_si_cambio_el_dia(logger_t *lg, time_t ahora) {
    struct tm tm;
    if (!fecha_local(ahora, &tm)) return -1;
    const int64_t hoy = dia_local(&tm);
    if (lg->hay_dia_retencion && lg->dia_retencion == hoy) return 0;
    lg->hay_dia_retencion = true;
    lg->dia_retencion = hoy;
    return logger_retener(lg, ahora) < 0 ? -1 : 0;
}

int logger_tick(logger_t *lg, time_t ahora) {
    int res = 0;
    if (lg->abierto && (ahora >= lg->inicio + (time_t)lg->intervalo_s || ahora < lg->inicio)) {
        if (escribir_fila(lg) != 0) res = -1;
        lg->abierto = false;
    }
    if (!lg->abierto) {
        lg->abierto = true;
        lg->inicio = alinear(ahora, lg->intervalo_s);
        reiniciar_acumulado(lg);
    }
    if (retener_si_cambio_el_dia(lg, ahora) != 0) res = -1;
    return res;
}

int logger_muestra(logger_t *lg, time_t ahora, const parser_env_t *env) {
    const int res = logger_tick(lg, ahora);
    if (!env->valida) return res;
    const int32_t t = env->temp_centi;
    const int32_t h = (int32_t)env->hr_centi;
    if (lg->n == 0u) {
        lg->min_temp = lg->max_temp = t;
        lg->min_hr = lg->max_hr = h;
    } else {
        if (t < lg->min_temp) lg->min_temp = t;
        if (t > lg->max_temp) lg->max_temp = t;
        if (h < lg->min_hr) lg->min_hr = h;
        if (h > lg->max_hr) lg->max_hr = h;
    }
    lg->n++;
    lg->suma_temp += t;
    lg->suma_hr += h;
    return res;
}

int logger_vaciar(logger_t *lg) {
    if (!lg->abierto) return 0;
    lg->abierto = false;
    return escribir_fila(lg);
}
