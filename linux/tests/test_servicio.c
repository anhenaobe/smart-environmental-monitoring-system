/*
 * Pruebas de desarrollo del nucleo servicio de envmond: modos bring-up,
 * normal y reset, latido SYS (RF-13), alarma en SYS,2, BTN/ACK y factory
 * reset (RF-52, RF-53), arranque con reset.pending y aplicacion de la
 * configuracion (RF-44). Relojes simulados; directorios temporales reales.
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
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmocka.h>
#include "servicio.h"

#define T0 ((time_t)1795770000)  /* 2026-11-27 09:00:00 UTC */

/* ---------- entorno ---------- */

static char g_base[256], g_cfg[300], g_datos[300];

typedef struct {
    char tx[256][16];
    size_t n_tx;
    char log[64][256];
    size_t n_log;
} captura_t;

static captura_t g_cap;

static void cap_tx(void *ctx, const char *c) {
    captura_t *k = ctx;
    if (k->n_tx < 256) snprintf(k->tx[k->n_tx++], sizeof(k->tx[0]), "%s", c);
}

static void cap_log(void *ctx, const char *m) {
    captura_t *k = ctx;
    if (k->n_log < 64) snprintf(k->log[k->n_log++], sizeof(k->log[0]), "%s", m);
}

static void vaciar(const char *dir) {
    DIR *d = opendir(dir);
    if (d == NULL) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (e->d_name[0] != '.') unlinkat(dirfd(d), e->d_name, 0);
    closedir(d);
}

static int preparar(void **state) {
    (void)state;
    const char *t = getenv("TMPDIR");
    snprintf(g_base, sizeof(g_base), "%s/envmond-servicio-XXXXXX", t != NULL ? t : "/tmp");
    if (mkdtemp(g_base) == NULL) return -1;
    snprintf(g_cfg, sizeof(g_cfg), "%s/etc", g_base);
    snprintf(g_datos, sizeof(g_datos), "%s/datos", g_base);
    setenv("TZ", "UTC", 1);
    tzset();
    return (mkdir(g_cfg, 0700) == 0 && mkdir(g_datos, 0700) == 0) ? 0 : -1;
}

static int terminar(void **state) {
    (void)state;
    vaciar(g_cfg);
    vaciar(g_datos);
    rmdir(g_cfg);
    rmdir(g_datos);
    return rmdir(g_base);
}

static int antes(void **state) {
    (void)state;
    vaciar(g_cfg);
    vaciar(g_datos);
    memset(&g_cap, 0, sizeof(g_cap));
    return 0;
}

static bool existe(const char *dir, const char *a) {
    char p[512];
    struct stat st;
    snprintf(p, sizeof(p), "%s/%s", dir, a);
    return stat(p, &st) == 0;
}

static unsigned archivos(const char *dir) {
    unsigned n = 0;
    DIR *d = opendir(dir);
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (e->d_name[0] != '.') n++;
    closedir(d);
    return n;
}

static config_t config_prueba(void) {
    config_t c;
    config_sugerida(&c);  /* 18-22 °C, 35-50 % HR, 60 s, 7 dias */
    assert_int_equal(config_hash_password("clave-segura-1", c.admin_hash), 0);
    return c;
}

static void configurar_equipo(void) {
    config_t c = config_prueba();
    assert_int_equal(config_guardar(g_cfg, &c, NULL), 0);
}

static servicio_params_t params(const reset_fs_ops_t *ops) {
    servicio_params_t p = { g_cfg, g_datos, cap_tx, cap_log, &g_cap, ops };
    return p;
}

/* Simulador: reloj monotonico y de pared avanzan juntos. */
typedef struct {
    servicio_t s;
    int64_t ms;
    uint32_t seq;
} sim_t;

static void sim_init(sim_t *x, const reset_fs_ops_t *ops) {
    memset(x, 0, sizeof(*x));
    servicio_params_t p = params(ops);
    assert_int_equal(servicio_init(&x->s, &p, 0, T0), 0);
}

static time_t pared(const sim_t *x) { return T0 + (time_t)(x->ms / 1000); }

static void trama(sim_t *x, const char *carga) {
    uint8_t cs = 0;
    for (const char *p = carga; *p; p++) cs ^= (uint8_t)*p;
    char b[80];
    int n = snprintf(b, sizeof(b), "$%s*%02X\r\n", carga, cs);
    servicio_bytes(&x->s, (const uint8_t *)b, (size_t)n, x->ms, pared(x));
}

static void crudo(sim_t *x, const char *b) {
    servicio_bytes(&x->s, (const uint8_t *)b, strlen(b), x->ms, pared(x));
}

static void env(sim_t *x, int temp_c, int hr) {
    char c[64];
    snprintf(c, sizeof(c), "ENV,%08u,%c%05d,%05d,0", (unsigned)x->seq++, temp_c < 0 ? '-' : '+',
             abs(temp_c) * 100, hr * 100);
    trama(x, c);
}

/* Avanza `ms` milisegundos con un tick cada 100 ms. */
static void avanzar(sim_t *x, int64_t ms) {
    const int64_t fin = x->ms + ms;
    while (x->ms < fin) {
        x->ms += 100;
        servicio_tick(&x->s, x->ms, pared(x));
    }
}

/* n ENV cada 3 s con ticks entre medio. */
static void lecturas(sim_t *x, unsigned n, int temp_c, int hr) {
    for (unsigned i = 0; i < n; i++) {
        env(x, temp_c, hr);
        avanzar(x, 3000);
    }
}

static size_t contar_tx(const char *c, size_t desde) {
    size_t n = 0;
    for (size_t i = desde; i < g_cap.n_tx; i++) n += strcmp(g_cap.tx[i], c) == 0;
    return n;
}

static const char *ultimo_sys(void) {
    for (size_t i = g_cap.n_tx; i > 0; i--)
        if (strncmp(g_cap.tx[i - 1], "SYS,", 4) == 0) return g_cap.tx[i - 1];
    return "";
}

static bool log_contiene(const char *t) {
    for (size_t i = 0; i < g_cap.n_log; i++)
        if (strstr(g_cap.log[i], t) != NULL) return true;
    return false;
}

/* ---------- bring-up ---------- */

static void bringup_sys0_cada_segundo(void **state) {
    (void)state;
    sim_t x;
    sim_init(&x, NULL);
    assert_int_equal(servicio_modo(&x.s), SERVICIO_BRINGUP);
    servicio_tick(&x.s, 0, T0);
    assert_int_equal(g_cap.n_tx, 1);
    assert_string_equal(g_cap.tx[0], "SYS,0");
    avanzar(&x, 900);
    assert_int_equal(g_cap.n_tx, 1);
    avanzar(&x, 100);
    assert_int_equal(g_cap.n_tx, 2);
    avanzar(&x, 8000);
    assert_int_equal(contar_tx("SYS,0", 0), 10);  /* t = 0..9 s */
    assert_int_equal(g_cap.n_tx, 10);
    assert_true(log_contiene("modo bring-up"));
}

static void bringup_sin_historial_ni_alarmas(void **state) {
    (void)state;
    sim_t x;
    sim_init(&x, NULL);
    lecturas(&x, 50, 35, 90);  /* fuera de cualquier perfil, 150 s */
    assert_int_equal(contar_tx("SYS,2", 0), 0);
    assert_int_equal(contar_tx("SYS,1", 0), 0);
    assert_int_equal(archivos(g_datos), 0);  /* sin historial */
    servicio_instantanea_t e;
    servicio_instantanea(&x.s, x.ms - 3000, pared(&x), &e);
    assert_int_equal(e.estado_sistema, 0);
    assert_true(e.valida);  /* la medicion se muestra igual */
    assert_int_equal(e.medicion.temp_centi, 3500);
    assert_false(e.alarma_temp || e.alarma_hr);
    assert_int_equal(e.uart.tramas_ok, 50);
    servicio_detener(&x.s);
    assert_int_equal(archivos(g_datos), 0);
}

static void config_invalida_es_bringup_y_se_conserva(void **state) {
    (void)state;
    configurar_equipo();
    char p[512];
    snprintf(p, sizeof(p), "%s/config", g_cfg);
    FILE *f = fopen(p, "ab");
    fputs("clave_rara=1\n", f);
    fclose(f);
    sim_t x;
    sim_init(&x, NULL);
    assert_int_equal(servicio_modo(&x.s), SERVICIO_BRINGUP);
    assert_true(existe(g_cfg, "config"));
    assert_true(log_contiene("configuracion invalida (clave desconocida"));
}

/* ---------- normal y alarma ---------- */

static void normal_sys1_y_alarma_sys2(void **state) {
    (void)state;
    configurar_equipo();
    sim_t x;
    sim_init(&x, NULL);
    assert_int_equal(servicio_modo(&x.s), SERVICIO_NORMAL);
    servicio_tick(&x.s, 0, T0);
    assert_string_equal(g_cap.tx[0], "SYS,1");
    lecturas(&x, 3, 20, 40);
    assert_string_equal(ultimo_sys(), "SYS,1");
    lecturas(&x, 4, 30, 40);       /* 4 fuera de rango: todavia no */
    assert_string_equal(ultimo_sys(), "SYS,1");
    const size_t antes_quinta = g_cap.n_tx;
    lecturas(&x, 1, 30, 40);       /* quinta: alarma */
    assert_true(contar_tx("SYS,2", antes_quinta) >= 2);
    lecturas(&x, 1, 20, 40);       /* vuelve dentro de la histeresis */
    assert_string_equal(ultimo_sys(), "SYS,1");
}

static void normal_escribe_historial(void **state) {
    (void)state;
    configurar_equipo();
    sim_t x;
    sim_init(&x, NULL);
    lecturas(&x, 25, 20, 40);  /* 75 s: cierra el intervalo de las 09:00 */
    assert_true(existe(g_datos, "2026-11-27.csv"));
    servicio_detener(&x.s);
    assert_true(log_contiene("fin: tramas_ok=25"));
}

/* ---------- factory reset ---------- */

static void btn_reset_completo(void **state) {
    (void)state;
    configurar_equipo();
    sim_t x;
    sim_init(&x, NULL);
    lecturas(&x, 25, 20, 40);
    assert_true(existe(g_datos, "2026-11-27.csv"));
    const size_t desde = g_cap.n_tx;
    trama(&x, "BTN,RST");
    assert_string_equal(g_cap.tx[desde], "ACK,RST");      /* despues del marcador */
    assert_string_equal(g_cap.tx[desde + 1], "SYS,3");
    assert_int_equal(servicio_modo(&x.s), SERVICIO_RESET);
    assert_false(existe(g_cfg, "config"));
    assert_false(existe(g_cfg, RESET_MARCADOR));
    assert_int_equal(archivos(g_datos), 0);
    avanzar(&x, 1900);                                     /* SYS,3 durante 2 s */
    assert_string_equal(ultimo_sys(), "SYS,3");
    assert_int_equal(contar_tx("SYS,3", desde), 2);
    avanzar(&x, 100);
    assert_int_equal(servicio_modo(&x.s), SERVICIO_BRINGUP);
    assert_string_equal(ultimo_sys(), "SYS,0");
    lecturas(&x, 30, 20, 40);
    assert_int_equal(archivos(g_datos), 0);                /* bring-up: sin historial */
    assert_int_equal(contar_tx("SYS,1", desde), 0);
}

static void btn_repetido_siempre_responde_ack(void **state) {
    (void)state;
    configurar_equipo();
    sim_t x;
    sim_init(&x, NULL);
    trama(&x, "BTN,RST");
    avanzar(&x, 1000);
    trama(&x, "BTN,RST");                                  /* el ACK se perdio */
    avanzar(&x, 3000);
    trama(&x, "BTN,RST");                                  /* ya en bring-up */
    assert_int_equal(contar_tx("ACK,RST", 0), 3);
    assert_int_equal(servicio_modo(&x.s), SERVICIO_RESET);
    avanzar(&x, 2000);
    assert_int_equal(servicio_modo(&x.s), SERVICIO_BRINGUP);
    assert_false(existe(g_cfg, RESET_MARCADOR));
}

static int g_falla_crear;
static int op_crear(const char *r) {
    if (g_falla_crear) { errno = EIO; return -1; }
    return reset_fs_ops_posix()->crear(r);
}
static int g_fallos_borrar;
static int op_borrar(const char *r) {
    if (g_fallos_borrar > 0) { g_fallos_borrar--; errno = EIO; return -1; }
    return reset_fs_ops_posix()->borrar(r);
}
static int op_sinc(int fd) { return reset_fs_ops_posix()->sincronizar(fd); }
static int op_sinc_dir(const char *d) { return reset_fs_ops_posix()->sincronizar_dir(d); }
static const reset_fs_ops_t OPS = { op_crear, op_sinc, op_sinc_dir, op_borrar };

static void btn_sin_marcador_durable_no_hay_ack(void **state) {
    (void)state;
    configurar_equipo();
    sim_t x;
    sim_init(&x, &OPS);
    g_falla_crear = 1;
    trama(&x, "BTN,RST");
    g_falla_crear = 0;
    assert_int_equal(contar_tx("ACK,RST", 0), 0);
    assert_int_equal(contar_tx("SYS,3", 0), 0);
    assert_int_equal(servicio_modo(&x.s), SERVICIO_NORMAL);
    assert_true(existe(g_cfg, "config"));
    assert_true(log_contiene("no se envia ACK"));
}

static void reset_incompleto_se_reintenta(void **state) {
    (void)state;
    configurar_equipo();
    sim_t x;
    sim_init(&x, &OPS);
    lecturas(&x, 25, 20, 40);
    g_fallos_borrar = 3;  /* el disco falla en los tres primeros borrados */
    trama(&x, "BTN,RST");
    assert_int_equal(contar_tx("ACK,RST", 0), 1);  /* el marcador si es durable */
    assert_true(existe(g_cfg, RESET_MARCADOR));
    avanzar(&x, 2500);
    /* Cada intento consume un fallo: el borrado termina en el cuarto. */
    for (int i = 0; i < 5 && servicio_modo(&x.s) == SERVICIO_RESET; i++) avanzar(&x, 1000);
    assert_int_equal(servicio_modo(&x.s), SERVICIO_BRINGUP);
    assert_false(existe(g_cfg, RESET_MARCADOR));
    assert_false(existe(g_cfg, "config"));
    assert_int_equal(archivos(g_datos), 0);
    assert_true(log_contiene("se reintenta"));
}

static void arranque_con_reset_pendiente(void **state) {
    (void)state;
    configurar_equipo();
    char p[512];
    snprintf(p, sizeof(p), "%s/2026-11-26.csv", g_datos);
    FILE *f = fopen(p, "wb");
    fputs(LOGGER_CABECERA, f);
    fclose(f);
    snprintf(p, sizeof(p), "%s/%s", g_cfg, RESET_MARCADOR);
    f = fopen(p, "wb");
    fclose(f);

    sim_t x;
    sim_init(&x, NULL);  /* el borrado ocurre dentro de servicio_init */
    assert_false(existe(g_cfg, RESET_MARCADOR));
    assert_false(existe(g_cfg, "config"));
    assert_int_equal(archivos(g_datos), 0);
    assert_int_equal(servicio_modo(&x.s), SERVICIO_BRINGUP);
    servicio_tick(&x.s, 0, T0);
    assert_string_equal(g_cap.tx[0], "SYS,0");
    assert_true(log_contiene("se completa el reset"));
}

/* ---------- aplicar configuracion (bring-up -> normal) ---------- */

static void aplicar_config_pasa_a_normal(void **state) {
    (void)state;
    sim_t x;
    sim_init(&x, NULL);
    avanzar(&x, 1500);
    assert_string_equal(ultimo_sys(), "SYS,0");
    config_t c = config_prueba();
    c.temp_max_centi = 2500;
    assert_int_equal(servicio_aplicar_config(&x.s, &c, x.ms, pared(&x)), 0);
    servicio_tick(&x.s, x.ms, pared(&x));          /* RF-44: de inmediato */
    assert_string_equal(ultimo_sys(), "SYS,1");
    assert_int_equal(servicio_modo(&x.s), SERVICIO_NORMAL);
    config_t leida;
    assert_int_equal(config_cargar(g_cfg, &leida, NULL, 0), CONFIG_CARGA_OK);
    assert_int_equal(leida.temp_max_centi, 2500);
    lecturas(&x, 6, 24, 40);                        /* 24 °C < 25: sin alarma */
    assert_string_equal(ultimo_sys(), "SYS,1");

    c.retencion_dias = 0;                           /* invalida */
    assert_int_equal(servicio_aplicar_config(&x.s, &c, x.ms, pared(&x)), -1);
    trama(&x, "BTN,RST");
    c.retencion_dias = 7;
    assert_int_equal(servicio_aplicar_config(&x.s, &c, x.ms, pared(&x)), -3);
}

/* ---------- instantanea para /api/estado ---------- */

static void instantanea_publica_el_estado(void **state) {
    (void)state;
    configurar_equipo();
    sim_t x;
    sim_init(&x, NULL);
    servicio_instantanea_t e;
    servicio_instantanea(&x.s, 0, T0, &e);
    assert_false(e.valida);
    assert_false(e.hay_env);
    assert_int_equal(e.seg_desde_ultima, -1);
    assert_true(e.hay_config);
    assert_string_equal(e.nombre, "Gabinete documental");

    trama(&x, "STA,00003600,7,2,0.3.0,00850,02048");
    env(&x, 24, 53);
    crudo(&x, "$ENV,00000005,+02450,05310,0*00\r\n"); /* checksum malo */
    avanzar(&x, 2000);
    servicio_instantanea(&x.s, x.ms, pared(&x), &e);
    assert_true(e.valida);
    assert_int_equal(e.estado_sistema, 1);
    assert_int_equal(e.medicion.temp_centi, 2400);
    assert_int_equal(e.medicion.hr_centi, 5300);
    assert_int_equal(e.medicion.estado, 0);
    assert_int_equal(e.uart.tramas_ok, 2);
    assert_int_equal(e.uart.err_checksum, 1);
    assert_int_equal(e.seg_desde_ultima, 2);
    assert_true(e.hay_sta);
    assert_int_equal(e.stm32.uptime_s, 3600);
    assert_int_equal(e.stm32.err_i2c, 7);
    assert_int_equal(e.stm32.causa_reset, 2);
    assert_string_equal(e.stm32.fw, "0.3.0");

    trama(&x, "ENV,00000001,,,1");                    /* falla del sensor */
    servicio_instantanea(&x.s, x.ms, pared(&x), &e);
    assert_false(e.valida);                            /* RF-25: de inmediato */
    assert_int_equal(e.medicion.estado, 1);
    assert_int_equal(e.medicion.temp_centi, 0);
}

static void plazo_de_poll(void **state) {
    (void)state;
    sim_t x;
    sim_init(&x, NULL);
    assert_int_equal(servicio_plazo_ms(&x.s, 0), 0);
    servicio_tick(&x.s, 0, T0);
    assert_int_equal(servicio_plazo_ms(&x.s, 0), 1000);
    assert_int_equal(servicio_plazo_ms(&x.s, 400), 600);
    assert_int_equal(servicio_plazo_ms(&x.s, 5000), 0);
}

int main(void) {
    const struct CMUnitTest pruebas[] = {
        cmocka_unit_test_setup(bringup_sys0_cada_segundo, antes),
        cmocka_unit_test_setup(bringup_sin_historial_ni_alarmas, antes),
        cmocka_unit_test_setup(config_invalida_es_bringup_y_se_conserva, antes),
        cmocka_unit_test_setup(normal_sys1_y_alarma_sys2, antes),
        cmocka_unit_test_setup(normal_escribe_historial, antes),
        cmocka_unit_test_setup(btn_reset_completo, antes),
        cmocka_unit_test_setup(btn_repetido_siempre_responde_ack, antes),
        cmocka_unit_test_setup(btn_sin_marcador_durable_no_hay_ack, antes),
        cmocka_unit_test_setup(reset_incompleto_se_reintenta, antes),
        cmocka_unit_test_setup(arranque_con_reset_pendiente, antes),
        cmocka_unit_test_setup(aplicar_config_pasa_a_normal, antes),
        cmocka_unit_test_setup(instantanea_publica_el_estado, antes),
        cmocka_unit_test_setup(plazo_de_poll, antes),
    };
    return cmocka_run_group_tests_name("servicio", pruebas, preparar, terminar);
}
