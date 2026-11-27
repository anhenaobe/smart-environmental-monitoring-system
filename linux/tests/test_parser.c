/*
 * Pruebas de desarrollo del parser UART de envmond (PU-04, PU-05, PU-06).
 * Vectores: docs/protocolo-uart.md v0.2 (los mismos de tests/test_protocol.c).
 * La verificacion formal la hace otro integrante (docs/matriz-verificacion.md).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>
#include "parser.h"

/* ---------- utilidades ---------- */

typedef struct {
    size_t env, sta, btn;
    parser_msg_t ultimo;
} recibido_t;

/* Alimenta s[0..n) por bloques y cuenta los mensajes completos. */
static recibido_t alimentar_n(parser_t *p, const char *s, size_t n) {
    recibido_t r;
    memset(&r, 0, sizeof(r));
    const uint8_t *d = (const uint8_t *)s;
    while (n > 0u) {
        parser_msg_t m;
        parser_tipo_t t;
        size_t usados = parser_bloque(p, d, n, &m, &t);
        assert_true(usados >= 1u && usados <= n);
        d += usados;
        n -= usados;
        if (t == PARSER_ENV) r.env++;
        else if (t == PARSER_STA) r.sta++;
        else if (t == PARSER_BTN) r.btn++;
        if (t != PARSER_NADA) r.ultimo = m;
    }
    return r;
}

static recibido_t alimentar(parser_t *p, const char *s) { return alimentar_n(p, s, strlen(s)); }

/* Arma "$<carga>*CS\r\n" en dst; devuelve la longitud. */
static size_t armar(char *dst, size_t cap, const char *carga) {
    uint8_t cs = 0;
    for (const char *q = carga; *q; ++q) cs ^= (uint8_t)*q;
    int n = snprintf(dst, cap, "$%s*%02X\r\n", carga, (unsigned)cs);
    assert_true(n > 0 && (size_t)n < cap);
    return (size_t)n;
}

/* Alimenta una trama armada a partir de su carga. */
static recibido_t alimentar_carga(parser_t *p, const char *carga) {
    char t[256];
    size_t n = armar(t, sizeof(t), carga);
    return alimentar_n(p, t, n);
}

static void esperar_contadores(const parser_t *p, uint32_t ok, uint32_t cs, uint32_t trama,
                               uint32_t desc, uint32_t perdidas) {
    const parser_contadores_t *c = parser_contadores(p);
    assert_non_null(c);
    assert_int_equal(c->tramas_ok, ok);
    assert_int_equal(c->err_checksum, cs);
    assert_int_equal(c->err_trama, trama);
    assert_int_equal(c->desconocidas, desc);
    assert_int_equal(c->perdidas, perdidas);
}

static void esperar_env(const parser_msg_t *m, uint32_t seq, bool valida, int32_t temp,
                        uint16_t hr, uint8_t estado) {
    assert_int_equal(m->tipo, PARSER_ENV);
    assert_int_equal(m->u.env.seq, seq);
    assert_int_equal(m->u.env.valida, valida);
    assert_int_equal(m->u.env.temp_centi, temp);
    assert_int_equal(m->u.env.hr_centi, hr);
    assert_int_equal(m->u.env.estado, estado);
}

/* ---------- PU-04: tramas validas de todos los tipos ---------- */

static void pu04_env_vectores_protocolo(void **state) {
    (void)state;
    static const struct {
        const char *trama;
        uint32_t seq;
        bool valida;
        int32_t temp;
        uint16_t hr;
        uint8_t estado;
    } v[] = {
        {"$ENV,00000000,+02450,05310,0*42\r\n", 0u, true, 2450, 5310u, 0u},
        {"$ENV,00000001,+02451,05308,0*4B\r\n", 1u, true, 2451, 5308u, 0u},
        {"$ENV,00000042,+02450,05310,0*44\r\n", 42u, true, 2450, 5310u, 0u},
        {"$ENV,00000043,,,1*6B\r\n", 43u, false, 0, 0u, 1u},
    };
    for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
        parser_t p;
        parser_init(&p);
        recibido_t r = alimentar(&p, v[i].trama);
        assert_int_equal(r.env, 1u);
        esperar_env(&r.ultimo, v[i].seq, v[i].valida, v[i].temp, v[i].hr, v[i].estado);
        esperar_contadores(&p, 1u, 0u, 0u, 0u, 0u);
    }
}

static void pu04_env_negativos_y_estados(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    /* Ejemplo de la tabla de ENV: -00520 = -5,20 grados C. */
    recibido_t r = alimentar_carga(&p, "ENV,00000044,-00520,05310,0");
    esperar_env(&r.ultimo, 44u, true, -520, 5310u, 0u);
    r = alimentar_carga(&p, "ENV,00000045,-99999,00000,0");
    esperar_env(&r.ultimo, 45u, true, -99999, 0u, 0u);
    r = alimentar_carga(&p, "ENV,00000046,+99999,10000,0");
    esperar_env(&r.ultimo, 46u, true, 99999, 10000u, 0u);
    r = alimentar_carga(&p, "ENV,00000047,-00000,10000,0");
    esperar_env(&r.ultimo, 47u, true, 0, 10000u, 0u);
    /* Estados de falla 1, 2 y 3: temp y hr vacios. */
    for (uint8_t e = 1u; e <= 3u; e++) {
        char carga[32];
        snprintf(carga, sizeof(carga), "ENV,%08u,,,%u", 47u + e, (unsigned)e);
        r = alimentar_carga(&p, carga);
        esperar_env(&r.ultimo, 47u + e, false, 0, 0u, e);
    }
    esperar_contadores(&p, 7u, 0u, 0u, 0u, 0u);
    /* seq maximo, en un parser nuevo para no mezclarlo con PU-06. */
    parser_init(&p);
    r = alimentar_carga(&p, "ENV,99999999,+02450,05310,0");
    esperar_env(&r.ultimo, 99999999u, true, 2450, 5310u, 0u);
    esperar_contadores(&p, 1u, 0u, 0u, 0u, 0u);
}

static void pu04_sta(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    recibido_t r = alimentar(&p, "$STA,00000003,0,0,0.1.0,00850,02048*77\r\n");
    assert_int_equal(r.sta, 1u);
    const parser_sta_t *s = &r.ultimo.u.sta;
    assert_int_equal(r.ultimo.tipo, PARSER_STA);
    assert_int_equal(s->uptime_s, 3u);
    assert_int_equal(s->err_i2c, 0u);
    assert_int_equal(s->causa_reset, 0u);
    assert_string_equal(s->fw, "0.1.0");
    assert_int_equal(s->lazo_max_us, 850u);
    assert_int_equal(s->pila_libre_b, 2048u);

    /* Anchos acordados: 1 a 10 digitos con tope uint32, causa 0..4. */
    r = alimentar_carga(&p, "STA,4294967295,4294967295,4,10.200.3000,99999,00000");
    s = &r.ultimo.u.sta;
    assert_int_equal(s->uptime_s, 4294967295u);
    assert_int_equal(s->err_i2c, 4294967295u);
    assert_int_equal(s->causa_reset, 4u);
    assert_string_equal(s->fw, "10.200.3000");
    assert_int_equal(s->lazo_max_us, 99999u);
    assert_int_equal(s->pila_libre_b, 0u);

    r = alimentar_carga(&p, "STA,7,12,2,1.0.0,00001,65535");
    s = &r.ultimo.u.sta;
    assert_int_equal(s->uptime_s, 7u);
    assert_int_equal(s->err_i2c, 12u);
    assert_int_equal(s->causa_reset, 2u);
    assert_string_equal(s->fw, "1.0.0");
    assert_int_equal(s->lazo_max_us, 1u);
    assert_int_equal(s->pila_libre_b, 65535u);
    esperar_contadores(&p, 3u, 0u, 0u, 0u, 0u);
}

static void pu04_btn(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    recibido_t r = alimentar(&p, "$BTN,RST*21\r\n");
    assert_int_equal(r.btn, 1u);
    assert_int_equal(r.ultimo.tipo, PARSER_BTN);
    esperar_contadores(&p, 1u, 0u, 0u, 0u, 0u);
}

/* Lo que recibe Linux en la sesion de ejemplo (§6), en un solo bloque. */
static void pu04_sesion_en_bloque(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    recibido_t r = alimentar(&p,
                             "$STA,00000003,0,0,0.1.0,00850,02048*77\r\n"
                             "$ENV,00000000,+02450,05310,0*42\r\n"
                             "$ENV,00000001,+02451,05308,0*4B\r\n"
                             "$BTN,RST*21\r\n");
    assert_int_equal(r.sta, 1u);
    assert_int_equal(r.env, 2u);
    assert_int_equal(r.btn, 1u);
    esperar_contadores(&p, 4u, 0u, 0u, 0u, 0u);
}

/* Linea de exactamente 64 bytes se acepta; 65 es error de trama. */
static void pu04_limite_64_bytes(void **state) {
    (void)state;
    char carga[80], t[96];
    parser_t p;
    parser_init(&p);
    /* 64 = '$' + carga + "*CS\r\n" -> carga de 58 bytes. Tipo desconocido
     * para alcanzar el limite (ENV/STA validos son mas cortos). */
    memset(carga, 'X', 58);
    memcpy(carga, "ZZZ,", 4);
    carga[58] = '\0';
    size_t n = armar(t, sizeof(t), carga);
    assert_int_equal(n, 64u);
    alimentar_n(&p, t, n);
    esperar_contadores(&p, 0u, 0u, 0u, 1u, 0u);

    carga[58] = 'X';
    carga[59] = '\0';
    n = armar(t, sizeof(t), carga);
    assert_int_equal(n, 65u);
    alimentar_n(&p, t, n);
    esperar_contadores(&p, 0u, 0u, 1u, 1u, 0u);
}

/* ---------- PU-05: tramas invalidas ---------- */

static void pu05_checksum_malo(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    recibido_t r = alimentar(&p, "$ENV,00000042,+02450,05310,0*45\r\n");
    assert_int_equal(r.env, 0u);
    r = alimentar(&p, "$ENV,00000042,+02450,05311,0*44\r\n"); /* dato alterado */
    assert_int_equal(r.env, 0u);
    r = alimentar(&p, "$BTN,RST*00\r\n");
    assert_int_equal(r.btn, 0u);
    esperar_contadores(&p, 0u, 3u, 0u, 0u, 0u);
}

static void pu05_trama_truncada(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    /* Cortada por el inicio de la siguiente trama. */
    recibido_t r = alimentar(&p, "$ENV,000000$BTN,RST*21\r\n");
    assert_int_equal(r.btn, 1u);
    esperar_contadores(&p, 1u, 0u, 1u, 0u, 0u);
    /* Cortada en el checksum, en el '*' o sin '\r'. */
    alimentar(&p, "$BTN,RST*2\r\n");
    alimentar(&p, "$BTN,RST\r\n");
    alimentar(&p, "$BTN,RST*21\n");
    alimentar(&p, "$\r\n");
    alimentar(&p, "$*00\r\n");
    esperar_contadores(&p, 1u, 0u, 6u, 0u, 0u);
}

static void pu05_mas_de_64_bytes(void **state) {
    (void)state;
    char linea[200];
    parser_t p;
    parser_init(&p);
    linea[0] = '$';
    memset(linea + 1, 'A', 69);
    memcpy(linea + 70, "\r\n", 2);
    alimentar_n(&p, linea, 72); /* 72 bytes: error al llegar a 64 */
    esperar_contadores(&p, 0u, 0u, 1u, 0u, 0u);

    /* Linea enorme sin '\n': un solo error y el resto se descarta. */
    memset(linea + 1, 'B', sizeof(linea) - 1);
    alimentar_n(&p, linea, sizeof(linea));
    recibido_t r = alimentar(&p, "$BTN,RST*21\r\n");
    assert_int_equal(r.btn, 1u);
    esperar_contadores(&p, 1u, 0u, 2u, 0u, 0u);
}

static void pu05_basura_antes_de_dolar(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    static const char basura[] = "\x00\xff\r\n*44\r\nENV,00000042,+02450,05310,0*44\r\n\x7f garbage";
    alimentar_n(&p, basura, sizeof(basura) - 1u);
    esperar_contadores(&p, 0u, 0u, 0u, 0u, 0u);
    recibido_t r = alimentar(&p, "ruido$ENV,00000042,+02450,05310,0*44\r\n");
    assert_int_equal(r.env, 1u);
    esperar_contadores(&p, 1u, 0u, 0u, 0u, 0u);
}

static void pu05_tipo_desconocido(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    alimentar(&p, "$SYS,1*44\r\n"); /* Linux -> STM32: no se espera de este lado */
    alimentar(&p, "$ACK,RST*30\r\n");
    alimentar_carga(&p, "XYZ,1,2,3");
    alimentar_carga(&p, "QQQ");
    esperar_contadores(&p, 0u, 0u, 0u, 4u, 0u);
}

/* Checksum correcto pero campos con formato invalido -> error de trama. */
static void pu05_campos_invalidos(void **state) {
    (void)state;
    static const char *const cargas[] = {
        "ENV,0000042,+02450,05310,0",    /* seq de 7 digitos */
        "ENV,000000042,+02450,05310,0",  /* seq de 9 digitos */
        "ENV,0000004A,+02450,05310,0",   /* seq no numerico */
        "ENV,00000042,02450,05310,0",    /* temp sin signo */
        "ENV,00000042,+2450,05310,0",    /* temp de 4 digitos */
        "ENV,00000042,+02450,5310,0",    /* hr de 4 digitos */
        "ENV,00000042,+02450,10001,0",   /* hr > 100,00 % */
        "ENV,00000042,+02450,05310,4",   /* estado fuera de 0..3 */
        "ENV,00000042,+02450,05310,",    /* estado vacio */
        "ENV,00000042,+02450,05310,00",  /* estado de 2 digitos */
        "ENV,00000042,,,0",              /* estado 0 sin datos */
        "ENV,00000043,+02450,05310,1",   /* estado 1 con datos */
        "ENV,00000043,,05310,1",         /* estado 1 con hr */
        "ENV,00000042,+02450,05310",     /* falta un campo */
        "ENV,00000042,+02450,05310,0,9", /* sobra un campo */
        "ENV",                           /* tipo sin campos */
        "STA,00000003,0,5,0.1.0,00850,02048",       /* causa_reset 5 */
        "STA,4294967296,0,0,0.1.0,00850,02048",     /* uptime > uint32 */
        "STA,00000000003,0,0,0.1.0,00850,02048",    /* uptime de 11 digitos */
        "STA,,0,0,0.1.0,00850,02048",               /* uptime vacio */
        "STA,00000003,0,0,0.1,00850,02048",         /* fw sin PAT */
        "STA,00000003,0,0,0..0,00850,02048",        /* fw con grupo vacio */
        "STA,00000003,0,0,a.1.0,00850,02048",       /* fw no numerico */
        "STA,00000003,0,0,1000.200.300,00850,02048", /* fw no cabe (12 bytes) */
        "STA,00000003,0,0,0.1.0,0850,02048",        /* lazo de 4 digitos */
        "STA,00000003,0,0,0.1.0,00850,020480",      /* pila de 6 digitos */
        "STA,00000003,0,0,0.1.0,00850",             /* faltan campos */
        "BTN,XXX",
        "BTN,RST,1",
        "BTN,",
        "env,00000042,+02450,05310,0", /* tipo en minusculas */
        "EN,00000042",                 /* tipo de dos letras */
        "ENVX,00000042,+02450,05310,0",
    };
    const size_t total = sizeof(cargas) / sizeof(cargas[0]);
    parser_t p;
    parser_init(&p);
    for (size_t i = 0; i < total; i++) {
        recibido_t r = alimentar_carga(&p, cargas[i]);
        assert_int_equal(r.env + r.sta + r.btn, 0u);
        assert_int_equal(parser_contadores(&p)->err_trama, i + 1u);
    }
    /* Estructura invalida: checksum en minuscula, '*' repetido, control. */
    alimentar(&p, "$ENV,00000001,+02451,05308,0*4b\r\n");
    alimentar(&p, "$BTN,R*T*21\r\n");
    alimentar(&p, "$BTN,R\tT*21\r\n");
    esperar_contadores(&p, 0u, 0u, (uint32_t)total + 3u, 0u, 0u);
}

#define SEQ_MAX_PRUEBA 100000000u

/* Generador pseudoaleatorio determinista (xorshift32). */
static uint32_t rng_estado = 0x12345678u;
static uint32_t rng(void) {
    rng_estado ^= rng_estado << 13;
    rng_estado ^= rng_estado >> 17;
    rng_estado ^= rng_estado << 5;
    return rng_estado;
}
static uint32_t rng_hasta(uint32_t n) { return rng() % n; }

/* 10 000 tramas aleatorias mezcladas con errores conocidos: cada contador
 * debe terminar en el valor esperado y los mensajes validos decodificarse
 * campo por campo. */
static void pu05_10000_tramas_aleatorias(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    uint32_t esp_ok = 0, esp_cs = 0, esp_trama = 0, esp_desc = 0;
    uint32_t seq = rng_hasta(SEQ_MAX_PRUEBA), uptime = 0;

    for (unsigned i = 0; i < 10000u; i++) {
        char carga[64], t[128];
        size_t n;
        const uint32_t clase = rng_hasta(8u);
        const uint32_t tipo = rng_hasta(3u);

        /* Carga valida del tipo elegido. */
        int32_t temp = (int32_t)rng_hasta(199999u) - 99999;
        uint32_t hr = rng_hasta(10001u), estado = rng_hasta(4u);
        uint32_t err_i2c = rng(), causa = rng_hasta(5u), lazo = rng_hasta(100000u),
                 pila = rng_hasta(100000u);
        uint32_t seq_trama = (clase == 0u) ? seq : rng_hasta(SEQ_MAX_PRUEBA);
        uint32_t uptime_trama = (clase == 0u) ? uptime + 1u + rng_hasta(20u) : rng();
        if (tipo == 0u) {
            if (estado == 0u)
                snprintf(carga, sizeof(carga), "ENV,%08u,%c%05u,%05u,0", (unsigned)seq_trama,
                         temp < 0 ? '-' : '+', (unsigned)(temp < 0 ? -temp : temp), (unsigned)hr);
            else
                snprintf(carga, sizeof(carga), "ENV,%08u,,,%u", (unsigned)seq_trama, (unsigned)estado);
        } else if (tipo == 1u) {
            snprintf(carga, sizeof(carga), "STA,%u,%u,%u,%u.%u.%u,%05u,%05u",
                     (unsigned)uptime_trama, (unsigned)err_i2c, (unsigned)causa,
                     (unsigned)rng_hasta(10u), (unsigned)rng_hasta(100u), (unsigned)rng_hasta(1000u),
                     (unsigned)lazo, (unsigned)pila);
        } else {
            snprintf(carga, sizeof(carga), "BTN,RST");
        }
        n = armar(t, sizeof(t), carga);

        switch (clase) {
        case 0: { /* valida: se decodifica y se compara campo por campo */
            recibido_t r = alimentar_n(&p, t, n);
            esp_ok++;
            if (tipo == 0u) {
                assert_int_equal(r.env, 1u);
                esperar_env(&r.ultimo, seq, estado == 0u, estado == 0u ? temp : 0,
                            (uint16_t)(estado == 0u ? hr : 0u), (uint8_t)estado);
                seq = (seq + 1u) % SEQ_MAX_PRUEBA; /* consecutivo: sin perdidas */
            } else if (tipo == 1u) {
                assert_int_equal(r.sta, 1u);
                assert_int_equal(r.ultimo.u.sta.uptime_s, uptime_trama);
                assert_int_equal(r.ultimo.u.sta.err_i2c, err_i2c);
                assert_int_equal(r.ultimo.u.sta.causa_reset, causa);
                assert_int_equal(r.ultimo.u.sta.lazo_max_us, lazo);
                assert_int_equal(r.ultimo.u.sta.pila_libre_b, pila);
                uptime = uptime_trama;
            } else {
                assert_int_equal(r.btn, 1u);
            }
            break;
        }
        case 1: /* un bit cambiado en el checksum */
            t[n - 3u] = (t[n - 3u] == '0') ? '1' : '0';
            alimentar_n(&p, t, n);
            esp_cs++;
            break;
        case 2: /* un byte de la carga alterado (sigue siendo imprimible) */
            t[1u + rng_hasta((uint32_t)(n - 6u))] ^= 0x01;
            if (memchr(t + 1, '$', n - 1u) || memchr(t + 1, '*', n - 6u)) {
                /* La alteracion produjo '$' o '*': se descarta este caso. */
                alimentar(&p, "$BTN,RST*21\r\n");
                esp_ok++;
            } else {
                alimentar_n(&p, t, n);
                esp_cs++;
            }
            break;
        case 3: /* truncada: la corta el '$' de la siguiente trama */
            alimentar_n(&p, t, 1u + rng_hasta((uint32_t)(n - 2u)));
            alimentar(&p, "$BTN,RST*21\r\n");
            esp_trama++;
            esp_ok++;
            break;
        case 4: { /* mas de 64 bytes sin '\n' */
            char larga[160];
            larga[0] = '$';
            size_t largo = 64u + rng_hasta(90u);
            for (size_t k = 1; k < largo; k++) larga[k] = (char)('A' + rng_hasta(26u));
            alimentar_n(&p, larga, largo);
            esp_trama++;
            break;
        }
        case 5: { /* basura sin '$' ni '\n' antes de una trama valida */
            char basura[40];
            size_t nb = 1u + rng_hasta(sizeof(basura));
            for (size_t k = 0; k < nb; k++) {
                char c;
                do c = (char)rng_hasta(256u); while (c == '$' || c == '\n');
                basura[k] = c;
            }
            alimentar_n(&p, basura, nb);
            alimentar(&p, "$BTN,RST*21\r\n");
            esp_ok++;
            break;
        }
        case 6: { /* tipo desconocido con checksum correcto */
            char otra[64];
            /* Primera letra U..Z: nunca forma ENV, STA ni BTN. */
            snprintf(otra, sizeof(otra), "%c%c%c%s", 'U' + (char)rng_hasta(6u),
                     'A' + (char)rng_hasta(26u), 'A' + (char)rng_hasta(26u), carga + 3);
            n = armar(t, sizeof(t), otra);
            alimentar_n(&p, t, n);
            esp_desc++;
            break;
        }
        default: /* campo invalido con checksum correcto */
            carga[strlen(carga) - 1u] = 'Z';
            n = armar(t, sizeof(t), carga);
            alimentar_n(&p, t, n);
            esp_trama++;
            break;
        }
    }
    esperar_contadores(&p, esp_ok, esp_cs, esp_trama, esp_desc, 0u);
    assert_int_equal(parser_reinicios_inferidos(&p), 0u);
    assert_int_equal(esp_ok + esp_cs + esp_trama + esp_desc >= 10000u, true);
}

/* Bytes totalmente aleatorios: no debe fallar ni leer fuera del bufer
 * (Valgrind) y cada evento contado corresponde a un '$' recibido. */
static void pu05_bytes_aleatorios(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    uint32_t dolares = 0;
    for (unsigned i = 0; i < 200000u; i++) {
        uint8_t b = (uint8_t)rng_hasta(256u);
        if (rng_hasta(16u) == 0u) b = (uint8_t)"$*\r\n,"[rng_hasta(5u)];
        if (b == '$') dolares++;
        parser_msg_t m;
        (void)parser_byte(&p, b, &m);
    }
    const parser_contadores_t *c = parser_contadores(&p);
    assert_true(c->tramas_ok + c->err_checksum + c->err_trama + c->desconocidas <= dolares);
    /* Sigue funcionando despues del ruido. */
    recibido_t r = alimentar(&p, "$BTN,RST*21\r\n");
    assert_int_equal(r.btn, 1u);
}

static void pu05_argumentos_nulos(void **state) {
    (void)state;
    parser_t p;
    parser_msg_t m;
    parser_tipo_t t = PARSER_BTN;
    parser_init(NULL);
    assert_int_equal(parser_byte(NULL, '$', &m), PARSER_NADA);
    assert_int_equal(parser_bloque(NULL, (const uint8_t *)"$", 1u, &m, &t), 0u);
    assert_int_equal(t, PARSER_NADA);
    assert_null(parser_contadores(NULL));
    assert_int_equal(parser_reinicios_inferidos(NULL), 0u);
    parser_init(&p);
    assert_int_equal(parser_bloque(&p, NULL, 5u, &m, &t), 0u);
    /* out == NULL: se procesa igual y cuenta. */
    const char *s = "$BTN,RST*21\r\n";
    parser_tipo_t ult = PARSER_NADA;
    for (const char *q = s; *q; ++q) ult = parser_byte(&p, (uint8_t)*q, NULL);
    assert_int_equal(ult, PARSER_BTN);
    esperar_contadores(&p, 1u, 0u, 0u, 0u, 0u);
}

/* ---------- PU-06: tramas perdidas por seq ---------- */

static void env_seq(parser_t *p, uint32_t seq) {
    char carga[40];
    snprintf(carga, sizeof(carga), "ENV,%08u,+02450,05310,0", (unsigned)seq);
    recibido_t r = alimentar_carga(p, carga);
    assert_int_equal(r.env, 1u);
}

static void sta_uptime(parser_t *p, uint32_t uptime) {
    char carga[64];
    snprintf(carga, sizeof(carga), "STA,%08u,0,0,0.1.0,00850,02048", (unsigned)uptime);
    recibido_t r = alimentar_carga(p, carga);
    assert_int_equal(r.sta, 1u);
}

static uint32_t perdidas(const parser_t *p) { return parser_contadores(p)->perdidas; }

static void pu06_huecos(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    env_seq(&p, 1000u); /* la primera solo fija la referencia */
    assert_int_equal(perdidas(&p), 0u);
    env_seq(&p, 1001u);
    env_seq(&p, 1002u);
    assert_int_equal(perdidas(&p), 0u);
    env_seq(&p, 1005u); /* faltan 1003 y 1004 */
    assert_int_equal(perdidas(&p), 2u);
    env_seq(&p, 1006u);
    env_seq(&p, 1010u); /* faltan 1007..1009 */
    assert_int_equal(perdidas(&p), 5u);
    env_seq(&p, 1010u); /* repetida: no suma */
    assert_int_equal(perdidas(&p), 5u);
    /* Una ENV descartada por checksum no mueve la referencia. */
    alimentar(&p, "$ENV,00000042,+02450,05310,0*45\r\n");
    env_seq(&p, 1012u);
    assert_int_equal(perdidas(&p), 6u);
    assert_int_equal(parser_reinicios_inferidos(&p), 0u);
}

static void pu06_desbordamiento(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    env_seq(&p, 99999998u);
    env_seq(&p, 99999999u);
    env_seq(&p, 0u); /* 99999999 -> 0 no es perdida */
    env_seq(&p, 1u);
    assert_int_equal(perdidas(&p), 0u);
    /* Hueco que cruza el desbordamiento: de 99999997 a 2 faltan
     * 99999998, 99999999, 0 y 1. (Llegar a 99999997 desde 1 es un retroceso:
     * se infiere un reinicio, ver pu06_retroceso_sin_sta.) */
    env_seq(&p, 99999997u);
    env_seq(&p, 2u);
    assert_int_equal(perdidas(&p), 4u);
    assert_int_equal(parser_reinicios_inferidos(&p), 1u);
}

static void pu06_reinicio_con_sta(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    sta_uptime(&p, 100u);
    env_seq(&p, 5000u);
    sta_uptime(&p, 110u); /* uptime crece: no es reinicio */
    env_seq(&p, 5002u);   /* falta 5001 */
    assert_int_equal(perdidas(&p), 1u);

    sta_uptime(&p, 3u); /* el STM32 se reinicio */
    env_seq(&p, 0u);    /* seq vuelve a 0: no es perdida */
    env_seq(&p, 1u);
    assert_int_equal(perdidas(&p), 1u);

    sta_uptime(&p, 2u); /* otro reinicio, pero se pierden seq 0 y 1 */
    env_seq(&p, 2u);
    assert_int_equal(perdidas(&p), 3u);
    assert_int_equal(parser_reinicios_inferidos(&p), 0u);
}

static void pu06_retroceso_sin_sta(void **state) {
    (void)state;
    parser_t p;
    parser_init(&p);
    env_seq(&p, 1000u);
    env_seq(&p, 1001u);
    env_seq(&p, 0u); /* se perdio el STA del arranque */
    env_seq(&p, 1u);
    assert_int_equal(perdidas(&p), 0u);
    assert_int_equal(parser_reinicios_inferidos(&p), 1u);
    env_seq(&p, 3u); /* despues del reinicio se siguen contando huecos */
    assert_int_equal(perdidas(&p), 1u);
    /* Cualquier retroceso equivale a avanzar mas de medio rango: tambien se
     * infiere reinicio, aunque sea de un solo paso. */
    env_seq(&p, 2u);
    assert_int_equal(perdidas(&p), 1u);
    assert_int_equal(parser_reinicios_inferidos(&p), 2u);
    assert_int_equal(parser_contadores(&p)->tramas_ok, 6u);
}

int main(void) {
    const struct CMUnitTest pruebas[] = {
        cmocka_unit_test(pu04_env_vectores_protocolo),
        cmocka_unit_test(pu04_env_negativos_y_estados),
        cmocka_unit_test(pu04_sta),
        cmocka_unit_test(pu04_btn),
        cmocka_unit_test(pu04_sesion_en_bloque),
        cmocka_unit_test(pu04_limite_64_bytes),
        cmocka_unit_test(pu05_checksum_malo),
        cmocka_unit_test(pu05_trama_truncada),
        cmocka_unit_test(pu05_mas_de_64_bytes),
        cmocka_unit_test(pu05_basura_antes_de_dolar),
        cmocka_unit_test(pu05_tipo_desconocido),
        cmocka_unit_test(pu05_campos_invalidos),
        cmocka_unit_test(pu05_10000_tramas_aleatorias),
        cmocka_unit_test(pu05_bytes_aleatorios),
        cmocka_unit_test(pu05_argumentos_nulos),
        cmocka_unit_test(pu06_huecos),
        cmocka_unit_test(pu06_desbordamiento),
        cmocka_unit_test(pu06_reinicio_con_sta),
        cmocka_unit_test(pu06_retroceso_sin_sta),
    };
    return cmocka_run_group_tests(pruebas, NULL, NULL);
}
