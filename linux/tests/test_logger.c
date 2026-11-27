/*
 * Pruebas de desarrollo del modulo logger de envmond:
 *   PU-09 agregacion por intervalo (RF-21),
 *   PU-11 rotacion diaria y retencion con reloj simulado (RF-22).
 * La verificacion formal la hace otro integrante (docs/matriz-verificacion.md).
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
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cmocka.h>
#include "logger.h"

/* 2026-11-27T00:00:00Z */
#define T0 ((time_t)1795737600)
#define DIA 86400

/* ---------- utilidades ---------- */

static char g_dir[256];

static void zona(const char *tz) {
    setenv("TZ", tz, 1);
    tzset();
}

static void vaciar_dir(void) {
    DIR *d = opendir(g_dir);
    if (d == NULL) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        unlinkat(dirfd(d), e->d_name, 0);
    }
    closedir(d);
}

static int preparar(void **state) {
    (void)state;
    const char *base = getenv("TMPDIR");
    snprintf(g_dir, sizeof(g_dir), "%s/envmond-logger-XXXXXX", base != NULL ? base : "/tmp");
    return mkdtemp(g_dir) != NULL ? 0 : -1;
}

static int terminar(void **state) {
    (void)state;
    vaciar_dir();
    return rmdir(g_dir);
}

static int antes(void **state) {
    (void)state;
    vaciar_dir();
    zona("UTC");
    return 0;
}

static parser_env_t env_ok(uint32_t seq, int32_t t, uint16_t h) {
    parser_env_t e = { .seq = seq, .estado = 0, .valida = true, .temp_centi = t, .hr_centi = h };
    return e;
}

static parser_env_t env_mala(uint32_t seq, uint8_t estado) {
    parser_env_t e = { .seq = seq, .estado = estado, .valida = false };
    return e;
}

static size_t leer(const char *archivo, char *buf, size_t n) {
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", g_dir, archivo);
    FILE *f = fopen(p, "rb");
    if (f == NULL) { buf[0] = '\0'; return 0; }
    size_t r = fread(buf, 1, n - 1u, f);
    fclose(f);
    buf[r] = '\0';
    return r;
}

static void escribir(const char *archivo, const char *contenido) {
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", g_dir, archivo);
    FILE *f = fopen(p, "wb");
    assert_non_null(f);
    fputs(contenido, f);
    fclose(f);
}

static bool existe(const char *archivo) {
    char p[512];
    struct stat st;
    snprintf(p, sizeof(p), "%s/%s", g_dir, archivo);
    return stat(p, &st) == 0;
}

static unsigned contar_csv(void) {
    unsigned n = 0;
    DIR *d = opendir(g_dir);
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (strstr(e->d_name, ".csv") != NULL) n++;
    closedir(d);
    return n;
}

static unsigned contar_lineas(const char *s) {
    unsigned n = 0;
    for (; *s != '\0'; s++)
        if (*s == '\n') n++;
    return n;
}

/* Linea `i` (0 = cabecera) del texto, sin el '\n'. */
static void linea(const char *s, unsigned i, char *out, size_t n) {
    for (unsigned k = 0; k < i; k++) {
        s = strchr(s, '\n');
        assert_non_null(s);
        s++;
    }
    const char *fin = strchr(s, '\n');
    assert_non_null(fin);
    snprintf(out, n, "%.*s", (int)(fin - s), s);
}

/* ---------- PU-09: agregacion ---------- */

/* Calculo de referencia hecho a mano (equivale a PROMEDIO, MIN y MAX en una
 * hoja de calculo, redondeando a centesimas):
 *   temp = 24,50 24,60 24,70 24,41  -> prom 24,5525 -> 24,55; min 24,41; max 24,70
 *   hr   = 53,10 53,00 53,30 53,21  -> prom 53,1525 -> 53,15; min 53,00; max 53,30
 * Las dos ENV con estado 1 y 3 se ignoran. */
static void pu09_promedio_minimo_maximo(void **state) {
    (void)state;
    logger_t lg;
    assert_int_equal(logger_init(&lg, g_dir, 60, 7), 0);
    const time_t t = T0 + 9 * 3600;  /* 09:00:00, inicio de intervalo */
    parser_env_t e;
    e = env_ok(1, 2450, 5310); assert_int_equal(logger_muestra(&lg, t + 0, &e), 0);
    e = env_ok(2, 2460, 5300); assert_int_equal(logger_muestra(&lg, t + 3, &e), 0);
    e = env_mala(3, 1);        assert_int_equal(logger_muestra(&lg, t + 6, &e), 0);
    e = env_ok(4, 2470, 5330); assert_int_equal(logger_muestra(&lg, t + 9, &e), 0);
    e = env_mala(5, 3);        assert_int_equal(logger_muestra(&lg, t + 12, &e), 0);
    e = env_ok(6, 2441, 5321); assert_int_equal(logger_muestra(&lg, t + 59, &e), 0);
    assert_false(existe("2026-11-27.csv"));  /* nada se escribe antes del cierre */
    assert_int_equal(logger_tick(&lg, t + 60), 0);

    char buf[1024], l[256];
    leer("2026-11-27.csv", buf, sizeof(buf));
    assert_int_equal(contar_lineas(buf), 2);
    linea(buf, 0, l, sizeof(l));
    assert_string_equal(l, "inicio_utc,hora_ok,n_validas,temp_prom,temp_min,temp_max,hr_prom,hr_min,hr_max");
    linea(buf, 1, l, sizeof(l));
    assert_string_equal(l, "1795770000,1,4,24.55,24.41,24.70,53.15,53.00,53.30");
}

static void pu09_intervalo_sin_validas_queda_vacio(void **state) {
    (void)state;
    logger_t lg;
    assert_int_equal(logger_init(&lg, g_dir, 60, 7), 0);
    parser_env_t e = env_mala(1, 1);
    assert_int_equal(logger_muestra(&lg, T0 + 10, &e), 0);
    for (time_t s = T0 + 11; s <= T0 + 60; s++) assert_int_equal(logger_tick(&lg, s), 0);
    char buf[1024], l[256];
    leer("2026-11-27.csv", buf, sizeof(buf));
    linea(buf, 1, l, sizeof(l));
    assert_string_equal(l, "1795737600,1,0,,,,,,");
}

static void pu09_negativos_y_redondeo(void **state) {
    (void)state;
    logger_t lg;
    assert_int_equal(logger_init(&lg, g_dir, 60, 7), 0);
    parser_env_t e;
    /* -5,20 y -5,25 -> prom -5,225 -> -5,23 (mitad lejos de cero) */
    e = env_ok(1, -520, 9500); logger_muestra(&lg, T0, &e);
    e = env_ok(2, -525, 9499); logger_muestra(&lg, T0 + 3, &e);
    logger_tick(&lg, T0 + 60);
    char buf[1024], l[256];
    leer("2026-11-27.csv", buf, sizeof(buf));
    linea(buf, 1, l, sizeof(l));
    assert_string_equal(l, "1795737600,1,2,-5.23,-5.25,-5.20,95.00,94.99,95.00");
}

/* 20 ENV por minuto durante 30 min con valores pseudoaleatorios y alguna
 * invalida; cada fila se compara con un calculo de referencia en double. */
static void pu09_serie_contra_referencia(void **state) {
    (void)state;
    logger_t lg;
    assert_int_equal(logger_init(&lg, g_dir, 60, 7), 0);
    uint32_t x = 12345u;
    char esperado[64][128];
    unsigned filas = 0;
    for (unsigned min = 0; min < 30; min++) {
        double st = 0, sh = 0;
        int32_t tmin = 99999, tmax = -99999, hmin = 99999, hmax = -99999;
        unsigned n = 0;
        for (unsigned k = 0; k < 20; k++) {
            x = x * 1103515245u + 12345u;
            const time_t t = T0 + (time_t)(min * 60u + k * 3u);
            parser_env_t e;
            if ((x >> 28) == 0u) {
                e = env_mala(min * 20u + k, 1);
            } else {
                const int32_t tv = -2000 + (int32_t)((x >> 8) % 8001u);
                const int32_t hv = 500 + (int32_t)((x >> 4) % 9001u);
                e = env_ok(min * 20u + k, tv, (uint16_t)hv);
                st += tv; sh += hv; n++;
                if (tv < tmin) tmin = tv;
                if (tv > tmax) tmax = tv;
                if (hv < hmin) hmin = hv;
                if (hv > hmax) hmax = hv;
            }
            assert_int_equal(logger_muestra(&lg, t, &e), 0);
        }
        const double pt = st / n, ph = sh / n;
        const int32_t rt = (int32_t)(pt + (pt >= 0 ? 0.5 : -0.5));
        const int32_t rh = (int32_t)(ph + 0.5);
#define C(v) ((v) < 0 ? "-" : ""), abs(v) / 100, abs(v) % 100
        snprintf(esperado[filas++], sizeof(esperado[0]),
                 "%lld,1,%u,%s%d.%02d,%s%d.%02d,%s%d.%02d,%s%d.%02d,%s%d.%02d,%s%d.%02d",
                 (long long)(T0 + (time_t)min * 60), n, C(rt), C(tmin), C(tmax), C(rh), C(hmin),
                 C(hmax));
#undef C
    }
    logger_tick(&lg, T0 + 30 * 60);
    char buf[8192], l[256];
    leer("2026-11-27.csv", buf, sizeof(buf));
    assert_int_equal(contar_lineas(buf), 31);
    for (unsigned i = 0; i < filas; i++) {
        linea(buf, i + 1u, l, sizeof(l));
        assert_string_equal(l, esperado[i]);
    }
}

static void pu09_alineacion_e_intervalo_configurable(void **state) {
    (void)state;
    logger_t lg;
    /* Intervalo de 3600 s: el primer intervalo empieza a las 10:00 aunque el
     * servicio arranque a las 10:20; se agrega por tiempo, no por conteo. */
    assert_int_equal(logger_init(&lg, g_dir, 3600, 7), 0);
    const time_t t = T0 + 10 * 3600 + 20 * 60;
    for (unsigned k = 0; k < 400; k++) {
        parser_env_t e = env_ok(k, 2000 + (int32_t)(k % 3u), 4000);
        logger_muestra(&lg, t + (time_t)k * 3, &e);
    }
    logger_tick(&lg, T0 + 11 * 3600);
    char buf[1024], l[256];
    leer("2026-11-27.csv", buf, sizeof(buf));
    assert_int_equal(contar_lineas(buf), 2);
    linea(buf, 1, l, sizeof(l));
    assert_string_equal(l, "1795773600,1,400,20.01,20.00,20.02,40.00,40.00,40.00");
}

static void pu09_hora_no_valida_y_reloj_hacia_atras(void **state) {
    (void)state;
    logger_t lg;
    assert_int_equal(logger_init(&lg, g_dir, 60, 30), 0);
    /* Arranque sin hora valida (1970): la fila se marca con hora_ok = 0. */
    parser_env_t e = env_ok(1, 2000, 4000);
    logger_muestra(&lg, 125, &e);
    logger_tick(&lg, 180);
    char buf[1024], l[256];
    leer("1970-01-01.csv", buf, sizeof(buf));
    linea(buf, 1, l, sizeof(l));
    assert_string_equal(l, "120,0,1,20.00,20.00,20.00,40.00,40.00,40.00");

    /* NTP adelanta el reloj: el archivo de 1970 queda fuera de la retencion
     * y se borra con el primer tick del dia nuevo. */
    logger_muestra(&lg, T0 + 30, &e);
    assert_false(existe("1970-01-01.csv"));

    /* El reloj retrocede 10 min: se cierra el intervalo en curso y se abre
     * otro en la hora nueva. */
    logger_muestra(&lg, T0 - 600, &e);
    logger_tick(&lg, T0 - 540);
    leer("2026-11-27.csv", buf, sizeof(buf));
    linea(buf, 1, l, sizeof(l));
    assert_string_equal(l, "1795737600,1,1,20.00,20.00,20.00,40.00,40.00,40.00");
    leer("2026-11-26.csv", buf, sizeof(buf));
    linea(buf, 1, l, sizeof(l));
    assert_string_equal(l, "1795737000,1,1,20.00,20.00,20.00,40.00,40.00,40.00");
}

static void pu09_reinicio_no_repite_cabecera_y_repara_fila_cortada(void **state) {
    (void)state;
    /* Un corte dejo una fila sin terminar. */
    escribir("2026-11-27.csv", LOGGER_CABECERA "1795737600,1,3,20.00,20.0");
    logger_t lg;
    assert_int_equal(logger_init(&lg, g_dir, 60, 7), 0);
    parser_env_t e = env_ok(1, 2100, 4100);
    logger_muestra(&lg, T0 + 60, &e);
    assert_int_equal(logger_vaciar(&lg), 0);  /* al detener el servicio */

    char buf[1024], l[256];
    leer("2026-11-27.csv", buf, sizeof(buf));
    assert_int_equal(contar_lineas(buf), 3);
    linea(buf, 1, l, sizeof(l));
    assert_string_equal(l, "1795737600,1,3,20.00,20.0");
    linea(buf, 2, l, sizeof(l));
    assert_string_equal(l, "1795737660,1,1,21.00,21.00,21.00,41.00,41.00,41.00");
    assert_int_equal(logger_vaciar(&lg), 0);  /* ya cerrado: no escribe otra vez */
    leer("2026-11-27.csv", buf, sizeof(buf));
    assert_int_equal(contar_lineas(buf), 3);
}

static void pu09_parametros_invalidos(void **state) {
    (void)state;
    logger_t lg;
    assert_int_equal(logger_init(&lg, g_dir, 0, 7), -1);
    assert_int_equal(logger_init(&lg, g_dir, 60, 0), -1);
    assert_int_equal(logger_init(&lg, "", 60, 7), -1);
    assert_int_equal(logger_init(&lg, NULL, 60, 7), -1);
    assert_int_equal(logger_init(&lg, "/ruta/que/no/existe", 60, 7), 0);
    assert_int_equal(logger_tick(&lg, T0), -1);   /* no puede leer el directorio */
    parser_env_t e = env_ok(1, 2000, 4000);
    logger_muestra(&lg, T0 + 1, &e);
    assert_int_equal(logger_tick(&lg, T0 + 60), -1);  /* no puede escribir */
}

/* ---------- PU-11: rotacion y retencion ---------- */

/* 8 dias simulados, del 2026-11-27 al 2026-12-04 (cambio de mes), con una
 * ENV cada 3 s y tick cada segundo; intervalo de 3600 s para que la prueba
 * sea rapida (24 filas por dia). */
static void simular_dias(logger_t *lg, time_t desde, unsigned dias) {
    uint32_t seq = 0;
    for (time_t t = desde; t < desde + (time_t)dias * DIA; t++) {
        if ((t - desde) % 3 == 0) {
            parser_env_t e = env_ok(seq++, 2000, 4500);
            assert_int_equal(logger_muestra(lg, t, &e), 0);
        } else {
            assert_int_equal(logger_tick(lg, t), 0);
        }
    }
    assert_int_equal(logger_tick(lg, desde + (time_t)dias * DIA), 0);
}

static void pu11_ocho_dias_retencion_siete(void **state) {
    (void)state;
    logger_t lg;
    assert_int_equal(logger_init(&lg, g_dir, 3600, 7), 0);
    simular_dias(&lg, T0, 8);
    /* Hoy (2026-12-05 00:00) se abrio el intervalo nuevo, pero su archivo
     * todavia no existe; la retencion conserva hoy y los 6 dias anteriores. */
    static const char *const quedan[] = { "2026-11-29.csv", "2026-11-30.csv", "2026-12-01.csv",
                                          "2026-12-02.csv", "2026-12-03.csv", "2026-12-04.csv" };
    assert_false(existe("2026-11-27.csv"));
    assert_false(existe("2026-11-28.csv"));
    for (size_t i = 0; i < sizeof(quedan) / sizeof(quedan[0]); i++) {
        char buf[4096];
        assert_true(existe(quedan[i]));
        leer(quedan[i], buf, sizeof(buf));
        assert_int_equal(contar_lineas(buf), 25);  /* cabecera + 24 horas */
    }
    assert_int_equal(contar_csv(), 6);

    /* Al cerrar la primera hora del dia 5 se crea su archivo: quedan 7. */
    assert_int_equal(logger_tick(&lg, T0 + 8 * DIA + 3600), 0);
    assert_true(existe("2026-12-05.csv"));
    assert_int_equal(contar_csv(), 7);
}

static void pu11_retencion_exacta_al_cambiar_de_dia(void **state) {
    (void)state;
    logger_t lg;
    assert_int_equal(logger_init(&lg, g_dir, 3600, 7), 0);
    /* Archivos de 8 dias ya existentes (del 2026-11-27 al 2026-12-04). */
    static const char *const dias[] = { "2026-11-27.csv", "2026-11-28.csv", "2026-11-29.csv",
                                        "2026-11-30.csv", "2026-12-01.csv", "2026-12-02.csv",
                                        "2026-12-03.csv", "2026-12-04.csv" };
    for (size_t i = 0; i < 8; i++) escribir(dias[i], LOGGER_CABECERA);
    /* Otros archivos no se tocan; una fecha futura tampoco. */
    escribir("notas.txt", "x");
    escribir("2026-13-01.csv", "x");
    escribir("2026-11-1.csv", "x");
    escribir("2027-01-01.csv", "x");

    /* Hoy es 2026-12-04: quedan 11-28 .. 12-04 (7 dias). */
    assert_int_equal(logger_tick(&lg, T0 + 7 * DIA + 5), 0);
    assert_false(existe("2026-11-27.csv"));
    for (size_t i = 1; i < 8; i++) assert_true(existe(dias[i]));
    assert_true(existe("notas.txt"));
    assert_true(existe("2026-13-01.csv"));
    assert_true(existe("2026-11-1.csv"));
    assert_true(existe("2027-01-01.csv"));

    /* La retencion corre una vez por dia: 2026-11-28 sigue hasta medianoche. */
    assert_int_equal(logger_tick(&lg, T0 + 8 * DIA - 1), 0);
    assert_true(existe("2026-11-28.csv"));
    assert_int_equal(logger_tick(&lg, T0 + 8 * DIA), 0);
    assert_false(existe("2026-11-28.csv"));
}

static void pu11_retencion_de_un_dia(void **state) {
    (void)state;
    logger_t lg;
    assert_int_equal(logger_init(&lg, g_dir, 60, 1), 0);
    escribir("2026-11-26.csv", LOGGER_CABECERA);
    escribir("2026-11-27.csv", LOGGER_CABECERA);
    assert_int_equal(logger_retener(&lg, T0 + 100), 1);
    assert_false(existe("2026-11-26.csv"));
    assert_true(existe("2026-11-27.csv"));
}

/* La fecha del archivo es la local. En Bogota (UTC-5, sin horario de
 * verano) el intervalo de las 23:00 del 30 de noviembre es 04:00Z del 1 de
 * diciembre y va a 2026-11-30.csv. */
static void pu11_fecha_local_y_cambio_de_mes(void **state) {
    (void)state;
    zona("<-05>5");
    logger_t lg;
    assert_int_equal(logger_init(&lg, g_dir, 3600, 7), 0);
    const time_t t = T0 + 4 * DIA + 4 * 3600;  /* 2026-12-01T04:00Z */
    parser_env_t e = env_ok(1, 2000, 4000);
    logger_muestra(&lg, t, &e);
    logger_muestra(&lg, t + 3600, &e);  /* 00:00 local del 1 de diciembre */
    logger_tick(&lg, t + 7200);
    char buf[1024], l[256];
    leer("2026-11-30.csv", buf, sizeof(buf));
    linea(buf, 1, l, sizeof(l));
    assert_string_equal(l, "1796097600,1,1,20.00,20.00,20.00,40.00,40.00,40.00");
    leer("2026-12-01.csv", buf, sizeof(buf));
    linea(buf, 1, l, sizeof(l));
    assert_string_equal(l, "1796101200,1,1,20.00,20.00,20.00,40.00,40.00,40.00");
    assert_int_equal(contar_csv(), 2);
}

int main(void) {
    const struct CMUnitTest pruebas[] = {
        cmocka_unit_test_setup(pu09_promedio_minimo_maximo, antes),
        cmocka_unit_test_setup(pu09_intervalo_sin_validas_queda_vacio, antes),
        cmocka_unit_test_setup(pu09_negativos_y_redondeo, antes),
        cmocka_unit_test_setup(pu09_serie_contra_referencia, antes),
        cmocka_unit_test_setup(pu09_alineacion_e_intervalo_configurable, antes),
        cmocka_unit_test_setup(pu09_hora_no_valida_y_reloj_hacia_atras, antes),
        cmocka_unit_test_setup(pu09_reinicio_no_repite_cabecera_y_repara_fila_cortada, antes),
        cmocka_unit_test_setup(pu09_parametros_invalidos, antes),
        cmocka_unit_test_setup(pu11_ocho_dias_retencion_siete, antes),
        cmocka_unit_test_setup(pu11_retencion_exacta_al_cambiar_de_dia, antes),
        cmocka_unit_test_setup(pu11_retencion_de_un_dia, antes),
        cmocka_unit_test_setup(pu11_fecha_local_y_cambio_de_mes, antes),
    };
    return cmocka_run_group_tests_name("logger", pruebas, preparar, terminar);
}
