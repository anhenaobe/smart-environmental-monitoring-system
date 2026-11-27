/*
 * Pruebas de desarrollo del modulo reset de envmond:
 *   PU-12 factory reset con inyeccion de falla antes y despues del marcador
 *   persistente y de cada borrado (RF-53).
 * Un proceso hijo ejecuta el reset y "se corta" (_exit) justo antes de la
 * operacion de archivo numero N; el padre simula el siguiente arranque. Se
 * prueba N = 1, 2, ... hasta que el reset termina sin corte.
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
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmocka.h>
#include "reset.h"

static char g_base[256], g_cfg[300], g_datos[300], g_sub[320];

static void ruta(char *b, size_t n, const char *dir, const char *a) { snprintf(b, n, "%s/%s", dir, a); }

static void escribir(const char *dir, const char *a, const char *txt) {
    char p[512];
    ruta(p, sizeof(p), dir, a);
    FILE *f = fopen(p, "wb");
    assert_non_null(f);
    fputs(txt, f);
    fclose(f);
}

static bool existe(const char *dir, const char *a) {
    char p[512];
    struct stat st;
    ruta(p, sizeof(p), dir, a);
    return lstat(p, &st) == 0;
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
    snprintf(g_base, sizeof(g_base), "%s/envmond-reset-XXXXXX", t != NULL ? t : "/tmp");
    if (mkdtemp(g_base) == NULL) return -1;
    snprintf(g_cfg, sizeof(g_cfg), "%s/etc", g_base);
    snprintf(g_datos, sizeof(g_datos), "%s/datos", g_base);
    snprintf(g_sub, sizeof(g_sub), "%s/subdir", g_datos);
    return (mkdir(g_cfg, 0700) == 0 && mkdir(g_datos, 0700) == 0 && mkdir(g_sub, 0700) == 0) ? 0 : -1;
}

static int terminar(void **state) {
    (void)state;
    vaciar(g_sub);
    rmdir(g_sub);
    vaciar(g_datos);
    vaciar(g_cfg);
    rmdir(g_datos);
    rmdir(g_cfg);
    vaciar(g_base);  /* archivo "ack" */
    return rmdir(g_base);
}

static const char *const HISTORIAL[] = { "2026-11-25.csv", "2026-11-26.csv", "2026-11-27.csv" };
#define N_HIST (sizeof(HISTORIAL) / sizeof(HISTORIAL[0]))

/* Equipo configurado con historial y un enlace simbolico entre los datos. */
static void equipo_configurado(void) {
    vaciar(g_cfg);
    vaciar(g_datos);
    escribir(g_cfg, "config", "nombre=Gabinete\n");
    for (size_t i = 0; i < N_HIST; i++) escribir(g_datos, HISTORIAL[i], "x\n");
    char p[512];
    ruta(p, sizeof(p), g_datos, "enlace");
    assert_int_equal(symlink("/nada", p), 0);
    escribir(g_sub, "no-se-toca", "x");
}

static unsigned historial_presente(void) {
    unsigned n = 0;
    for (size_t i = 0; i < N_HIST; i++) n += existe(g_datos, HISTORIAL[i]) ? 1u : 0u;
    return n;
}

static void assert_bring_up_limpio(void) {
    assert_false(reset_pendiente(g_cfg));
    assert_false(existe(g_cfg, "config"));
    assert_false(existe(g_cfg, "config.tmp"));
    assert_int_equal(historial_presente(), 0);
    assert_false(existe(g_datos, "enlace"));
    assert_true(existe(g_sub, "no-se-toca"));  /* no borra subdirectorios */
}

static void assert_intacto(void) {
    assert_false(reset_pendiente(g_cfg));
    assert_true(existe(g_cfg, "config"));
    assert_int_equal(historial_presente(), N_HIST);
}

/* ---------- operaciones con corte o error inyectado ---------- */

static int g_corte;        /* cortar antes de la llamada numero g_corte (0 = nunca) */
static int g_error;        /* devolver error en la llamada numero g_error (0 = nunca) */
static int g_llamadas;

static bool paso(void) {
    g_llamadas++;
    if (g_corte != 0 && g_llamadas == g_corte) _exit(0);
    if (g_error != 0 && g_llamadas == g_error) { errno = EIO; return false; }
    return true;
}
static int op_crear(const char *r) { return paso() ? reset_fs_ops_posix()->crear(r) : -1; }
static int op_sinc(int fd) { return paso() ? reset_fs_ops_posix()->sincronizar(fd) : -1; }
static int op_sinc_dir(const char *d) { return paso() ? reset_fs_ops_posix()->sincronizar_dir(d) : -1; }
static int op_borrar(const char *r) { return paso() ? reset_fs_ops_posix()->borrar(r) : -1; }
static const reset_fs_ops_t OPS = { op_crear, op_sinc, op_sinc_dir, op_borrar };

/* Hijo: marcar -> (ACK) -> completar. Sale con 2 si llego al final sin
 * corte; con 0 si se corto. Deja el archivo "ack" si confirmo el reset. */
static int ejecutar_con_corte(int corte) {
    fflush(NULL);
    pid_t pid = fork();
    assert_true(pid >= 0);
    if (pid == 0) {
        g_corte = corte;
        g_error = 0;
        g_llamadas = 0;
        if (reset_marcar(g_cfg, &OPS) != 0) _exit(3);
        escribir(g_base, "ack", "ACK,RST");  /* solo tras el marcador durable */
        if (reset_completar(g_cfg, g_datos, &OPS) != 0) _exit(4);
        _exit(2);
    }
    int st;
    assert_int_equal(waitpid(pid, &st, 0), pid);
    assert_true(WIFEXITED(st));
    return WEXITSTATUS(st);
}

/* PU-12: corte antes de cada operacion de archivo. */
static void pu12_corte_en_cada_paso(void **state) {
    (void)state;
    int corte = 1;
    for (;; corte++) {
        equipo_configurado();
        char ack[512];
        ruta(ack, sizeof(ack), g_base, "ack");
        unlink(ack);

        const int r = ejecutar_con_corte(corte);
        assert_true(r == 0 || r == 2);
        const bool hubo_ack = existe(g_base, "ack");

        /* Invariante justo despues del corte: sin marcador, el equipo esta
         * intacto o ya limpio; nunca hay historial sin configuracion ni al reves. */
        if (!reset_pendiente(g_cfg)) {
            const bool intacto = existe(g_cfg, "config") && historial_presente() == N_HIST;
            const bool limpio = !existe(g_cfg, "config") && historial_presente() == 0;
            assert_true(intacto || limpio);
            if (intacto) assert_false(hubo_ack);  /* ACK implica marcador durable */
        }
        /* Orden de interfaces.md §4: primero el historial, despues config. */
        if (!existe(g_cfg, "config")) assert_int_equal(historial_presente(), 0);

        /* Siguiente arranque: si hay marcador, completar antes de la web. */
        if (reset_pendiente(g_cfg)) assert_int_equal(reset_completar(g_cfg, g_datos, NULL), 0);

        if (hubo_ack) {
            assert_bring_up_limpio();                  /* confirmado: siempre termina */
        } else if (corte == 1) {
            assert_intacto();                          /* antes de crear el marcador */
        } else {
            /* Marcador creado pero quiza no durable: estado coherente. */
            if (existe(g_cfg, "config")) assert_intacto();
            else assert_bring_up_limpio();
        }
        if (r == 2) break;  /* el reset termino sin corte */
    }
    /* marcar: crear, fsync, fsync dir (3); completar: 4 borrados de datos,
     * fsync, 2 borrados de config, fsync, borrar marcador, fsync (10). */
    assert_int_equal(corte, 14);
}

/* PU-12: errores (sin corte) en cada paso. */
static void pu12_error_al_marcar_no_confirma(void **state) {
    (void)state;
    for (int e = 1; e <= 3; e++) {
        equipo_configurado();
        g_corte = 0;
        g_llamadas = 0;
        g_error = e;
        assert_int_equal(reset_marcar(g_cfg, &OPS), -1);
        g_error = 0;
        assert_intacto();  /* sin marcador durable: se conserva el estado anterior */
    }
}

static void pu12_error_al_borrar_se_reintenta(void **state) {
    (void)state;
    for (int e = 1; e <= 10; e++) {
        equipo_configurado();
        assert_int_equal(reset_marcar(g_cfg, NULL), 0);
        g_corte = 0;
        g_llamadas = 0;
        g_error = e;
        assert_int_equal(reset_completar(g_cfg, g_datos, &OPS), -1);
        g_error = 0;
        /* Hasta el borrado del marcador (llamada 9) el marcador sigue; si
         * falla el ultimo fsync (10), el marcador ya se retiro pero el
         * reintento sigue siendo inofensivo. */
        assert_int_equal(reset_pendiente(g_cfg), e <= 9);
        assert_int_equal(reset_completar(g_cfg, g_datos, NULL), 0);
        assert_bring_up_limpio();
    }
}

static void pu12_repetir_es_idempotente(void **state) {
    (void)state;
    equipo_configurado();
    escribir(g_cfg, "config.tmp", "resto");
    assert_int_equal(reset_marcar(g_cfg, NULL), 0);
    assert_true(reset_pendiente(g_cfg));
    assert_int_equal(reset_completar(g_cfg, g_datos, NULL), 0);
    assert_bring_up_limpio();
    /* BTN repetido con el reset ya hecho: marcar y completar otra vez. */
    assert_int_equal(reset_marcar(g_cfg, NULL), 0);
    assert_int_equal(reset_completar(g_cfg, g_datos, NULL), 0);
    assert_bring_up_limpio();
}

static void pu12_sin_directorio_de_datos(void **state) {
    (void)state;
    char cfg[400], nodatos[400];
    snprintf(cfg, sizeof(cfg), "%s", g_cfg);
    snprintf(nodatos, sizeof(nodatos), "%s/no-existe", g_base);
    vaciar(g_cfg);
    escribir(g_cfg, "config", "x\n");
    assert_int_equal(reset_marcar(cfg, NULL), 0);
    assert_int_equal(reset_completar(cfg, nodatos, NULL), 0);
    assert_false(existe(g_cfg, "config"));
    assert_false(reset_pendiente(g_cfg));
}

int main(void) {
    const struct CMUnitTest pruebas[] = {
        cmocka_unit_test(pu12_corte_en_cada_paso),
        cmocka_unit_test(pu12_error_al_marcar_no_confirma),
        cmocka_unit_test(pu12_error_al_borrar_se_reintenta),
        cmocka_unit_test(pu12_repetir_es_idempotente),
        cmocka_unit_test(pu12_sin_directorio_de_datos),
    };
    return cmocka_run_group_tests_name("reset", pruebas, preparar, terminar);
}
