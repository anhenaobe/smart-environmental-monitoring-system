/*
 * Pruebas de desarrollo del modulo config de envmond:
 *   PU-07 guardado atomico con inyeccion de fallas (RF-43),
 *   PU-14 validacion de la configuracion (RF-42),
 *   PU-15 verificacion de la contrasena con hash crypt SHA-512 (RF-32).
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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmocka.h>
#include "config.h"

#define PASSWORD "clave-segura-1"

/* ---------- utilidades ---------- */

static char g_dir[256];

static int crear_dir(void **state) {
    (void)state;
    const char *base = getenv("TMPDIR");
    snprintf(g_dir, sizeof(g_dir), "%s/envmond-config-XXXXXX", base != NULL ? base : "/tmp");
    return mkdtemp(g_dir) != NULL ? 0 : -1;
}

static void borrar(const char *archivo) {
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", g_dir, archivo);
    unlink(p);
}

static int borrar_dir(void **state) {
    (void)state;
    borrar("config");
    borrar("config.tmp");
    return rmdir(g_dir);
}

static bool existe(const char *archivo) {
    char p[512];
    struct stat st;
    snprintf(p, sizeof(p), "%s/%s", g_dir, archivo);
    return stat(p, &st) == 0;
}

static size_t leer(const char *archivo, char *buf, size_t n) {
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", g_dir, archivo);
    FILE *f = fopen(p, "rb");
    if (f == NULL) return 0;
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

static config_form_t form_valido(void) {
    config_form_t f = {
        .nombre = "Gabinete documental A",
        .password = PASSWORD,
        .ntp = "pool.ntp.org",
        .intervalo_registro_s = "60",
        .temp_min = "18.0",
        .temp_max = "22.0",
        .hr_min = "35",
        .hr_max = "50",
        .retencion_dias = "7",
    };
    return f;
}

/* Configuracion completa con hash real. "version" distingue la vieja de la nueva. */
static config_t config_con(int version) {
    config_t c;
    config_sugerida(&c);
    snprintf(c.nombre, sizeof(c.nombre), "Gabinete v%d", version);
    c.temp_max_centi = 2200 + version;
    assert_int_equal(config_hash_password(PASSWORD, c.admin_hash), 0);
    return c;
}

static void assert_config_igual(const config_t *a, const config_t *b) {
    assert_string_equal(a->nombre, b->nombre);
    assert_string_equal(a->admin_hash, b->admin_hash);
    assert_string_equal(a->ntp, b->ntp);
    assert_int_equal(a->intervalo_registro_s, b->intervalo_registro_s);
    assert_int_equal(a->temp_min_centi, b->temp_min_centi);
    assert_int_equal(a->temp_max_centi, b->temp_max_centi);
    assert_int_equal(a->hr_min_centi, b->hr_min_centi);
    assert_int_equal(a->hr_max_centi, b->hr_max_centi);
    assert_int_equal(a->retencion_dias, b->retencion_dias);
}

/* Prueba un formulario con un solo campo alterado: espera exactamente un
 * error, en `campo`, con `motivo`. */
static void un_error(config_form_t f, config_campo_t campo, config_motivo_t motivo) {
    config_motivo_t m[CONFIG_N_CAMPOS];
    config_t c;
    assert_int_equal(config_desde_formulario(&f, &c, m), 1);
    for (size_t i = 0; i < CONFIG_N_CAMPOS; i++) {
        if (i == (size_t)campo) assert_int_equal(m[i], motivo);
        else assert_int_equal(m[i], CONFIG_MOTIVO_OK);
    }
}

static void sin_error(config_form_t f) {
    config_motivo_t m[CONFIG_N_CAMPOS];
    config_t c;
    assert_int_equal(config_desde_formulario(&f, &c, m), 0);
}

/* ---------- PU-14: validacion ---------- */

static void pu14_valida_se_acepta(void **state) {
    (void)state;
    config_form_t f = form_valido();
    config_motivo_t m[CONFIG_N_CAMPOS];
    config_t c;
    assert_int_equal(config_desde_formulario(&f, &c, m), 0);
    assert_string_equal(c.nombre, "Gabinete documental A");
    assert_string_equal(c.ntp, "pool.ntp.org");
    assert_int_equal(c.intervalo_registro_s, 60);
    assert_int_equal(c.temp_min_centi, 1800);
    assert_int_equal(c.temp_max_centi, 2200);
    assert_int_equal(c.hr_min_centi, 3500);
    assert_int_equal(c.hr_max_centi, 5000);
    assert_int_equal(c.retencion_dias, 7);
    assert_int_equal(config_validar(&c, NULL), 0);
    assert_true(config_verificar_password(PASSWORD, c.admin_hash));
}

static void pu14_campos_vacios(void **state) {
    (void)state;
    config_form_t f;
    f = form_valido(); f.nombre = "";               un_error(f, CONFIG_CAMPO_NOMBRE, CONFIG_MOTIVO_VACIO);
    f = form_valido(); f.nombre = NULL;             un_error(f, CONFIG_CAMPO_NOMBRE, CONFIG_MOTIVO_VACIO);
    f = form_valido(); f.nombre = "   ";            un_error(f, CONFIG_CAMPO_NOMBRE, CONFIG_MOTIVO_VACIO);
    f = form_valido(); f.password = "";             un_error(f, CONFIG_CAMPO_PASSWORD, CONFIG_MOTIVO_VACIO);
    f = form_valido(); f.intervalo_registro_s = ""; un_error(f, CONFIG_CAMPO_INTERVALO, CONFIG_MOTIVO_VACIO);
    f = form_valido(); f.temp_min = "";             un_error(f, CONFIG_CAMPO_TEMP_MIN, CONFIG_MOTIVO_VACIO);
    f = form_valido(); f.temp_max = "";             un_error(f, CONFIG_CAMPO_TEMP_MAX, CONFIG_MOTIVO_VACIO);
    f = form_valido(); f.hr_min = "";               un_error(f, CONFIG_CAMPO_HR_MIN, CONFIG_MOTIVO_VACIO);
    f = form_valido(); f.hr_max = "";               un_error(f, CONFIG_CAMPO_HR_MAX, CONFIG_MOTIVO_VACIO);
    f = form_valido(); f.retencion_dias = "";       un_error(f, CONFIG_CAMPO_RETENCION, CONFIG_MOTIVO_VACIO);
    /* NTP vacio es valido: significa "sin NTP". */
    f = form_valido(); f.ntp = "";                  sin_error(f);
}

static void pu14_fuera_de_rango(void **state) {
    (void)state;
    config_form_t f;
    f = form_valido(); f.intervalo_registro_s = "59";   un_error(f, CONFIG_CAMPO_INTERVALO, CONFIG_MOTIVO_RANGO);
    f = form_valido(); f.intervalo_registro_s = "3601"; un_error(f, CONFIG_CAMPO_INTERVALO, CONFIG_MOTIVO_RANGO);
    f = form_valido(); f.retencion_dias = "0";          un_error(f, CONFIG_CAMPO_RETENCION, CONFIG_MOTIVO_RANGO);
    f = form_valido(); f.retencion_dias = "31";         un_error(f, CONFIG_CAMPO_RETENCION, CONFIG_MOTIVO_RANGO);
    f = form_valido(); f.temp_min = "-20.01";           un_error(f, CONFIG_CAMPO_TEMP_MIN, CONFIG_MOTIVO_RANGO);
    f = form_valido(); f.temp_max = "60.01";            un_error(f, CONFIG_CAMPO_TEMP_MAX, CONFIG_MOTIVO_RANGO);
    f = form_valido(); f.hr_min = "4.99";               un_error(f, CONFIG_CAMPO_HR_MIN, CONFIG_MOTIVO_RANGO);
    f = form_valido(); f.hr_max = "95.01";              un_error(f, CONFIG_CAMPO_HR_MAX, CONFIG_MOTIVO_RANGO);
    /* Limites incluidos. */
    f = form_valido(); f.intervalo_registro_s = "3600"; f.retencion_dias = "30";
    f.temp_min = "-20"; f.temp_max = "60.00"; f.hr_min = "5"; f.hr_max = "95"; sin_error(f);
    f = form_valido(); f.retencion_dias = "1"; sin_error(f);
}

static void pu14_formato_numerico(void **state) {
    (void)state;
    static const char *const malos[] = { "abc", "18,5", "1e2", " 18", "18 ", "18.123",
                                         "18.", ".5", "+18", "--1", "0x10", "12345" };
    for (size_t i = 0; i < sizeof(malos) / sizeof(malos[0]); i++) {
        config_form_t f = form_valido();
        f.temp_min = malos[i];
        un_error(f, CONFIG_CAMPO_TEMP_MIN, CONFIG_MOTIVO_FORMATO);
    }
    config_form_t f;
    f = form_valido(); f.intervalo_registro_s = "60.5"; un_error(f, CONFIG_CAMPO_INTERVALO, CONFIG_MOTIVO_FORMATO);
    f = form_valido(); f.intervalo_registro_s = "-60";  un_error(f, CONFIG_CAMPO_INTERVALO, CONFIG_MOTIVO_FORMATO);
    f = form_valido(); f.retencion_dias = "7d";         un_error(f, CONFIG_CAMPO_RETENCION, CONFIG_MOTIVO_FORMATO);

    int32_t v;
    assert_true(config_parse_centi("18.25", &v)); assert_int_equal(v, 1825);
    assert_true(config_parse_centi("-5.5", &v));  assert_int_equal(v, -550);
    assert_true(config_parse_centi("-0.05", &v)); assert_int_equal(v, -5);
    assert_true(config_parse_centi("0", &v));     assert_int_equal(v, 0);
}

static void pu14_minimo_no_menor_que_maximo(void **state) {
    (void)state;
    config_form_t f;
    f = form_valido(); f.temp_min = "22"; f.temp_max = "22.0"; un_error(f, CONFIG_CAMPO_TEMP_MAX, CONFIG_MOTIVO_ORDEN);
    f = form_valido(); f.temp_min = "25"; f.temp_max = "22";   un_error(f, CONFIG_CAMPO_TEMP_MAX, CONFIG_MOTIVO_ORDEN);
    f = form_valido(); f.hr_min = "50";   f.hr_max = "50";     un_error(f, CONFIG_CAMPO_HR_MAX, CONFIG_MOTIVO_ORDEN);
    f = form_valido(); f.hr_min = "60";   f.hr_max = "40";     un_error(f, CONFIG_CAMPO_HR_MAX, CONFIG_MOTIVO_ORDEN);
    f = form_valido(); f.temp_min = "21.99"; f.temp_max = "22"; un_error(f, CONFIG_CAMPO_TEMP_MAX, CONFIG_MOTIVO_ESTRECHO);

    /* Tambien sobre una configuracion armada (la que se carga del archivo). */
    config_t c = config_con(0);
    config_motivo_t m[CONFIG_N_CAMPOS];
    c.hr_min_centi = c.hr_max_centi;
    assert_int_equal(config_validar(&c, m), 1);
    assert_int_equal(m[CONFIG_CAMPO_HR_MAX], CONFIG_MOTIVO_ORDEN);
}

/* Decision de Mateo: max - min >= 2 x histeresis de RF-24 (1,0 °C y 4 puntos
 * de HR); si no, la banda de desactivacion queda vacia y la alarma no se
 * apagaria nunca. */
static void pu14_rango_minimo_por_histeresis(void **state) {
    (void)state;
    config_form_t f;
    f = form_valido(); f.temp_min = "21";    f.temp_max = "22";    sin_error(f);
    f = form_valido(); f.temp_min = "21.01"; f.temp_max = "22";    un_error(f, CONFIG_CAMPO_TEMP_MAX, CONFIG_MOTIVO_ESTRECHO);
    f = form_valido(); f.temp_min = "20.0";  f.temp_max = "20.5";  un_error(f, CONFIG_CAMPO_TEMP_MAX, CONFIG_MOTIVO_ESTRECHO);
    f = form_valido(); f.hr_min = "46";      f.hr_max = "50";      sin_error(f);
    f = form_valido(); f.hr_min = "46.01";   f.hr_max = "50";      un_error(f, CONFIG_CAMPO_HR_MAX, CONFIG_MOTIVO_ESTRECHO);
    f = form_valido(); f.hr_min = "49";      f.hr_max = "50";      un_error(f, CONFIG_CAMPO_HR_MAX, CONFIG_MOTIVO_ESTRECHO);
    assert_int_equal(CONFIG_ANCHO_MIN_TEMP_CENTI, 100);
    assert_int_equal(CONFIG_ANCHO_MIN_HR_CENTI, 400);

    /* Tambien al cargar el archivo. */
    config_t c = config_con(0);
    config_motivo_t m[CONFIG_N_CAMPOS];
    c.temp_min_centi = 2150;
    c.temp_max_centi = 2200;
    assert_int_equal(config_validar(&c, m), 1);
    assert_int_equal(m[CONFIG_CAMPO_TEMP_MAX], CONFIG_MOTIVO_ESTRECHO);
}

static void pu14_password_longitud(void **state) {
    (void)state;
    char larga[CONFIG_PASSWORD_MAX + 2u];
    config_form_t f;
    f = form_valido(); f.password = "1234567";   un_error(f, CONFIG_CAMPO_PASSWORD, CONFIG_MOTIVO_LONGITUD);
    f = form_valido(); f.password = "12345678";  sin_error(f);
    memset(larga, 'a', CONFIG_PASSWORD_MAX);
    larga[CONFIG_PASSWORD_MAX] = '\0';
    f = form_valido(); f.password = larga;       sin_error(f);
    larga[CONFIG_PASSWORD_MAX] = 'a';
    larga[CONFIG_PASSWORD_MAX + 1u] = '\0';
    f = form_valido(); f.password = larga;       un_error(f, CONFIG_CAMPO_PASSWORD, CONFIG_MOTIVO_LONGITUD);
    f = form_valido(); f.password = "clave\nmala"; un_error(f, CONFIG_CAMPO_PASSWORD, CONFIG_MOTIVO_CARACTER);
    f = form_valido(); f.password = "contraseña ñ"; sin_error(f);
}

static void pu14_nombre_caracteres(void **state) {
    (void)state;
    static const char *const malos[] = {
        "Gab=1", "a<b>", "x\ny", "\"q\"", "a&b", "it's", "tab\tx", "x\ry",
        "Nº 2",            /* U+00BA no es letra latina del rango admitido */
        "3\xC3\x97" "4",   /* signo de multiplicacion */
        "\xC3",            /* UTF-8 incompleto */
        "a\xC3(b",         /* continuacion invalida */
        "\xE2\x82\xAC",    /* euro: 3 bytes */
    };
    for (size_t i = 0; i < sizeof(malos) / sizeof(malos[0]); i++) {
        config_form_t f = form_valido();
        f.nombre = malos[i];
        un_error(f, CONFIG_CAMPO_NOMBRE, CONFIG_MOTIVO_CARACTER);
    }
    config_form_t f;
    f = form_valido(); f.nombre = "Archivo Histórico (sala 2)"; sin_error(f);
    f = form_valido(); f.nombre = "Señal_Ñ-1.0 Łódź";           sin_error(f);
    f = form_valido(); f.nombre = "0123456789012345678901234567890123456789"; sin_error(f);
    f = form_valido(); f.nombre = "01234567890123456789012345678901234567890";
    un_error(f, CONFIG_CAMPO_NOMBRE, CONFIG_MOTIVO_LONGITUD);
}

static void pu14_ntp(void **state) {
    (void)state;
    config_form_t f;
    f = form_valido(); f.ntp = "192.168.1.1";     sin_error(f);
    f = form_valido(); f.ntp = "a-b.ntp.org";     sin_error(f);
    f = form_valido(); f.ntp = "-a.org";          un_error(f, CONFIG_CAMPO_NTP, CONFIG_MOTIVO_FORMATO);
    f = form_valido(); f.ntp = "a.org-";          un_error(f, CONFIG_CAMPO_NTP, CONFIG_MOTIVO_FORMATO);
    f = form_valido(); f.ntp = "a..org";          un_error(f, CONFIG_CAMPO_NTP, CONFIG_MOTIVO_FORMATO);
    f = form_valido(); f.ntp = ".org";            un_error(f, CONFIG_CAMPO_NTP, CONFIG_MOTIVO_FORMATO);
    f = form_valido(); f.ntp = "pool ntp";        un_error(f, CONFIG_CAMPO_NTP, CONFIG_MOTIVO_CARACTER);
    f = form_valido(); f.ntp = "pool.ntp.org=x";  un_error(f, CONFIG_CAMPO_NTP, CONFIG_MOTIVO_CARACTER);
}

static void pu14_varios_errores_a_la_vez(void **state) {
    (void)state;
    config_form_t f = form_valido();
    f.nombre = "";
    f.password = "corta";
    f.hr_max = "200";
    config_motivo_t m[CONFIG_N_CAMPOS];
    config_t c;
    assert_int_equal(config_desde_formulario(&f, &c, m), 3);
    assert_int_equal(m[CONFIG_CAMPO_NOMBRE], CONFIG_MOTIVO_VACIO);
    assert_int_equal(m[CONFIG_CAMPO_PASSWORD], CONFIG_MOTIVO_LONGITUD);
    assert_int_equal(m[CONFIG_CAMPO_HR_MAX], CONFIG_MOTIVO_RANGO);
}

static void pu14_archivo_invalido_no_se_carga_ni_se_borra(void **state) {
    (void)state;
    config_t c = config_con(1);
    assert_int_equal(config_guardar(g_dir, &c, NULL), 0);
    char buf[2048];
    leer("config", buf, sizeof(buf));

    static const struct { const char *buscar, *poner; } cambios[] = {
        { "temp_min=18.0", "temp_min=30.0" },          /* min >= max */
        { "hr_max=50.0", "hr_max=99.0" },              /* fuera de rango */
        { "retencion_dias=7", "retencion_dias=siete" },/* formato */
        { "nombre=", "nombre=<script>" },              /* caracter */
        { "admin_hash=$6$", "admin_hash=$1$" },        /* hash que no es SHA-512 */
        { "ntp=", "xyz=1\nntp=" },                     /* clave desconocida */
        { "ntp=", "ntp=a.org\nntp=" },                 /* clave repetida */
    };
    for (size_t i = 0; i < sizeof(cambios) / sizeof(cambios[0]); i++) {
        char alterado[2048];
        const char *p = strstr(buf, cambios[i].buscar);
        assert_non_null(p);
        size_t pre = (size_t)(p - buf);
        snprintf(alterado, sizeof(alterado), "%.*s%s%s", (int)pre, buf, cambios[i].poner,
                 p + strlen(cambios[i].buscar));
        escribir("config", alterado);
        config_t leida;
        char detalle[128] = "";
        assert_int_equal(config_cargar(g_dir, &leida, detalle, sizeof(detalle)),
                         CONFIG_CARGA_INVALIDA);
        assert_true(detalle[0] != '\0');
        assert_true(existe("config"));
    }
    /* Falta una clave. */
    char sin_ntp[2048];
    const char *p = strstr(buf, "ntp=");
    const char *fin = strchr(p, '\n');
    snprintf(sin_ntp, sizeof(sin_ntp), "%.*s%s", (int)(p - buf), buf, fin + 1);
    escribir("config", sin_ntp);
    config_t leida;
    assert_int_equal(config_cargar(g_dir, &leida, NULL, 0), CONFIG_CARGA_INVALIDA);
    borrar("config");
}

/* ---------- PU-07: guardado atomico ---------- */

static void pu07_ida_y_vuelta(void **state) {
    (void)state;
    config_t c = config_con(0);
    c.temp_min_centi = -550;
    c.temp_max_centi = 1825;
    c.ntp[0] = '\0';
    snprintf(c.nombre, sizeof(c.nombre), "Archivo Histórico (sala 2)");
    assert_int_equal(config_guardar(g_dir, &c, NULL), 0);
    assert_false(existe("config.tmp"));

    char buf[2048];
    leer("config", buf, sizeof(buf));
    assert_non_null(strstr(buf, "temp_min=-5.5\n"));
    assert_non_null(strstr(buf, "temp_max=18.25\n"));
    assert_non_null(strstr(buf, "hr_min=35.0\n"));
    assert_non_null(strstr(buf, "intervalo_registro_s=60\n"));
    assert_non_null(strstr(buf, "ntp=\n"));

    config_t leida;
    assert_int_equal(config_cargar(g_dir, &leida, NULL, 0), CONFIG_CARGA_OK);
    assert_config_igual(&c, &leida);

    struct stat st;
    char p[512];
    snprintf(p, sizeof(p), "%s/config", g_dir);
    assert_int_equal(stat(p, &st), 0);
    assert_int_equal(st.st_mode & 0777, 0600);
    borrar("config");
}

static void pu07_no_existe_es_bring_up(void **state) {
    (void)state;
    config_t leida;
    assert_int_equal(config_cargar(g_dir, &leida, NULL, 0), CONFIG_CARGA_NO_EXISTE);
}

static void pu07_invalida_no_se_escribe(void **state) {
    (void)state;
    config_t c = config_con(0);
    c.retencion_dias = 0;
    assert_int_equal(config_guardar(g_dir, &c, NULL), -1);
    assert_false(existe("config"));
    assert_false(existe("config.tmp"));
}

/* Operaciones con falla inyectada: `g_falla` dice en que paso fallar y
 * `g_abortar` si se simula un corte (_exit sin limpiar) o un error. */
typedef enum { F_NINGUNA, F_ESCRITURA_PARCIAL, F_FSYNC, F_RENAME, F_FSYNC_DIR } falla_t;
static falla_t g_falla;
static bool g_abortar;

static long op_escribir(int fd, const void *buf, size_t n) {
    if (g_falla == F_ESCRITURA_PARCIAL) {
        long r = config_fs_ops_posix()->escribir(fd, buf, n / 2u);
        if (g_abortar) _exit(0);
        (void)r;
        return -1;
    }
    return config_fs_ops_posix()->escribir(fd, buf, n);
}
static int op_sincronizar(int fd) {
    if (g_falla == F_FSYNC) { if (g_abortar) _exit(0); return -1; }
    return config_fs_ops_posix()->sincronizar(fd);
}
static int op_renombrar(const char *a, const char *b) {
    if (g_falla == F_RENAME) { if (g_abortar) _exit(0); return -1; }
    return config_fs_ops_posix()->renombrar(a, b);
}
static int op_sincronizar_dir(const char *d) {
    if (g_falla == F_FSYNC_DIR) { if (g_abortar) _exit(0); return -1; }
    return config_fs_ops_posix()->sincronizar_dir(d);
}
static const config_fs_ops_t OPS_FALLA = { op_escribir, op_sincronizar, op_renombrar,
                                           op_sincronizar_dir };

/* Corte simulado: un hijo ejecuta el guardado y termina con _exit en el
 * paso indicado, sin ejecutar ninguna limpieza. */
static void guardar_con_corte(const config_t *c, falla_t falla) {
    fflush(NULL);
    pid_t pid = fork();
    assert_true(pid >= 0);
    if (pid == 0) {
        g_falla = falla;
        g_abortar = true;
        (void)config_guardar(g_dir, c, &OPS_FALLA);
        _exit(1);  /* no deberia llegar: la falla corta antes */
    }
    int st;
    assert_int_equal(waitpid(pid, &st, 0), pid);
    assert_true(WIFEXITED(st));
    assert_int_equal(WEXITSTATUS(st), 0);
}

static void pu07_error_antes_del_rename_conserva_la_anterior(void **state) {
    (void)state;
    const config_t vieja = config_con(1);
    const config_t nueva = config_con(2);
    const falla_t fallas[] = { F_ESCRITURA_PARCIAL, F_FSYNC, F_RENAME };
    for (size_t i = 0; i < sizeof(fallas) / sizeof(fallas[0]); i++) {
        assert_int_equal(config_guardar(g_dir, &vieja, NULL), 0);
        g_falla = fallas[i];
        g_abortar = false;
        assert_int_equal(config_guardar(g_dir, &nueva, &OPS_FALLA), -2);
        g_falla = F_NINGUNA;
        assert_false(existe("config.tmp"));
        config_t leida;
        assert_int_equal(config_cargar(g_dir, &leida, NULL, 0), CONFIG_CARGA_OK);
        assert_config_igual(&vieja, &leida);
    }
    borrar("config");
}

static void pu07_error_despues_del_rename_deja_la_nueva(void **state) {
    (void)state;
    const config_t vieja = config_con(1);
    const config_t nueva = config_con(2);
    assert_int_equal(config_guardar(g_dir, &vieja, NULL), 0);
    g_falla = F_FSYNC_DIR;
    g_abortar = false;
    assert_int_equal(config_guardar(g_dir, &nueva, &OPS_FALLA), -2);
    g_falla = F_NINGUNA;
    config_t leida;
    assert_int_equal(config_cargar(g_dir, &leida, NULL, 0), CONFIG_CARGA_OK);
    assert_config_igual(&nueva, &leida);
    borrar("config");
}

static void pu07_corte_en_cada_paso(void **state) {
    (void)state;
    const config_t vieja = config_con(1);
    const config_t nueva = config_con(2);
    const struct { falla_t falla; bool queda_nueva; } casos[] = {
        { F_ESCRITURA_PARCIAL, false },  /* config.tmp a medias */
        { F_FSYNC, false },              /* antes de fsync */
        { F_RENAME, false },             /* antes de rename */
        { F_FSYNC_DIR, true },           /* despues de rename */
    };
    for (size_t i = 0; i < sizeof(casos) / sizeof(casos[0]); i++) {
        assert_int_equal(config_guardar(g_dir, &vieja, NULL), 0);
        guardar_con_corte(&nueva, casos[i].falla);
        config_t leida;
        assert_int_equal(config_cargar(g_dir, &leida, NULL, 0), CONFIG_CARGA_OK);
        assert_config_igual(casos[i].queda_nueva ? &nueva : &vieja, &leida);
        /* El siguiente guardado funciona aunque haya quedado config.tmp. */
        assert_int_equal(config_guardar(g_dir, &nueva, NULL), 0);
        assert_int_equal(config_cargar(g_dir, &leida, NULL, 0), CONFIG_CARGA_OK);
        assert_config_igual(&nueva, &leida);
        assert_false(existe("config.tmp"));
    }
    borrar("config");
}

static void pu07_corte_en_el_primer_guardado(void **state) {
    (void)state;
    const config_t nueva = config_con(2);
    guardar_con_corte(&nueva, F_RENAME);
    config_t leida;
    assert_int_equal(config_cargar(g_dir, &leida, NULL, 0), CONFIG_CARGA_NO_EXISTE);
    borrar("config.tmp");
}

static void escribir_n(const char *archivo, const char *datos, size_t n) {
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", g_dir, archivo);
    FILE *f = fopen(p, "wb");
    assert_non_null(f);
    assert_int_equal(fwrite(datos, 1, n, f), n);
    fclose(f);
}

static void pu07_archivo_truncado_se_detecta(void **state) {
    (void)state;
    const config_t c = config_con(1);
    assert_int_equal(config_guardar(g_dir, &c, NULL), 0);
    char original[2048];
    size_t n = leer("config", original, sizeof(original));
    assert_true(n > 0u);
    /* Cualquier prefijo propio del archivo (truncado) se rechaza. */
    for (size_t corte = 0; corte < n; corte++) {
        escribir_n("config", original, corte);
        config_t leida;
        assert_int_equal(config_cargar(g_dir, &leida, NULL, 0), CONFIG_CARGA_INVALIDA);
    }
    escribir_n("config", original, n);
    config_t leida;
    assert_int_equal(config_cargar(g_dir, &leida, NULL, 0), CONFIG_CARGA_OK);
    assert_config_igual(&c, &leida);
    borrar("config");
}

/* ---------- PU-15: contrasena con hash ---------- */

static void pu15_formato_del_hash(void **state) {
    (void)state;
    char h1[CONFIG_HASH_MAX], h2[CONFIG_HASH_MAX];
    assert_int_equal(config_hash_password(PASSWORD, h1), 0);
    assert_int_equal(config_hash_password(PASSWORD, h2), 0);
    assert_memory_equal(h1, "$6$", 3);
    assert_int_equal(strchr(h1 + 3, '$') - (h1 + 3), 16);  /* sal de 16 */
    assert_int_equal(strlen(h1), 3u + 16u + 1u + 86u);
    assert_string_not_equal(h1, h2);  /* sal distinta en cada hash */
    assert_null(strstr(h1, PASSWORD));
}

static void pu15_verificacion(void **state) {
    (void)state;
    char h[CONFIG_HASH_MAX];
    assert_int_equal(config_hash_password(PASSWORD, h), 0);
    assert_true(config_verificar_password(PASSWORD, h));
    assert_false(config_verificar_password("", h));
    assert_false(config_verificar_password(NULL, h));
    assert_false(config_verificar_password("otra-clave-9", h));
    assert_false(config_verificar_password(PASSWORD "x", h));
    assert_false(config_verificar_password("clave-segura-", h));
    /* Un caracter cambiado en cada posicion. */
    char cambiada[] = PASSWORD;
    for (size_t i = 0; i < strlen(cambiada); i++) {
        char orig = cambiada[i];
        cambiada[i] = (char)(orig == 'a' ? 'b' : 'a');
        assert_false(config_verificar_password(cambiada, h));
        cambiada[i] = orig;
    }
    /* Hash mal formado o de otro algoritmo. */
    assert_false(config_verificar_password(PASSWORD, ""));
    assert_false(config_verificar_password(PASSWORD, "$1$abc$def"));
    char roto[CONFIG_HASH_MAX];
    memcpy(roto, h, sizeof(roto));
    roto[strlen(roto) - 1u] = roto[strlen(roto) - 1u] == 'a' ? 'b' : 'a';
    assert_false(config_verificar_password(PASSWORD, roto));
}

static void pu15_archivo_sin_contrasena_en_claro(void **state) {
    (void)state;
    config_form_t f = form_valido();
    config_motivo_t m[CONFIG_N_CAMPOS];
    config_t c;
    assert_int_equal(config_desde_formulario(&f, &c, m), 0);
    assert_int_equal(config_guardar(g_dir, &c, NULL), 0);
    char buf[2048];
    assert_true(leer("config", buf, sizeof(buf)) > 0u);
    assert_null(strstr(buf, PASSWORD));
    assert_non_null(strstr(buf, "admin_hash=$6$"));

    config_t leida;
    assert_int_equal(config_cargar(g_dir, &leida, NULL, 0), CONFIG_CARGA_OK);
    assert_true(config_verificar_password(PASSWORD, leida.admin_hash));
    assert_false(config_verificar_password("clave-segura-2", leida.admin_hash));
    borrar("config");
}

int main(void) {
    const struct CMUnitTest pruebas[] = {
        cmocka_unit_test(pu14_valida_se_acepta),
        cmocka_unit_test(pu14_campos_vacios),
        cmocka_unit_test(pu14_fuera_de_rango),
        cmocka_unit_test(pu14_formato_numerico),
        cmocka_unit_test(pu14_minimo_no_menor_que_maximo),
        cmocka_unit_test(pu14_rango_minimo_por_histeresis),
        cmocka_unit_test(pu14_password_longitud),
        cmocka_unit_test(pu14_nombre_caracteres),
        cmocka_unit_test(pu14_ntp),
        cmocka_unit_test(pu14_varios_errores_a_la_vez),
        cmocka_unit_test(pu14_archivo_invalido_no_se_carga_ni_se_borra),
        cmocka_unit_test(pu07_ida_y_vuelta),
        cmocka_unit_test(pu07_no_existe_es_bring_up),
        cmocka_unit_test(pu07_invalida_no_se_escribe),
        cmocka_unit_test(pu07_error_antes_del_rename_conserva_la_anterior),
        cmocka_unit_test(pu07_error_despues_del_rename_deja_la_nueva),
        cmocka_unit_test(pu07_corte_en_cada_paso),
        cmocka_unit_test(pu07_corte_en_el_primer_guardado),
        cmocka_unit_test(pu07_archivo_truncado_se_detecta),
        cmocka_unit_test(pu15_formato_del_hash),
        cmocka_unit_test(pu15_verificacion),
        cmocka_unit_test(pu15_archivo_sin_contrasena_en_claro),
    };
    return cmocka_run_group_tests_name("config", pruebas, crear_dir, borrar_dir);
}
