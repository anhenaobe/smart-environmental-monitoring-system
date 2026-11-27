/*
 * Parser de tramas UART STM32 -> Milk-V (docs/protocolo-uart.md v0.2).
 *
 * Encuadre propio en lugar de reutilizar firmware/src/env_protocol.c:
 * env_parse() devuelve 0 tanto por checksum malo como por formato invalido y
 * env_rx_byte() acumula todo en un unico contador `errors`, pero el protocolo
 * (§2) exige contar por separado errores de checksum, de trama y tramas
 * desconocidas. Propuesta pendiente de acordar con Andrew: agregar en
 * firmware/ un env_parse_status() que devuelva OK / CHECKSUM / TRAMA; con eso
 * este archivo podria delegar el encuadre en env_protocol.c. Mientras tanto,
 * ambas implementaciones se prueban con los mismos vectores del protocolo
 * (tests/test_protocol.c y linux/tests/test_parser.c).
 */
#include "parser.h"

#include <string.h>

#define SEQ_MODULO 100000000u /* seq vuelve a 0 despues de 99999999 */
#define MAX_CAMPOS 6u

typedef struct {
    const char *s;
    size_t n;
} campo_t;

typedef enum { DEC_OK, DEC_TRAMA, DEC_DESCONOCIDA } decodificacion_t;

static int hex_mayuscula(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool es_digito(char c) { return c >= '0' && c <= '9'; }

/* Caracter permitido dentro de la carga util: ASCII imprimible sin '$' ni '*'. */
static bool caracter_carga(char c) {
    unsigned char u = (unsigned char)c;
    return u >= 32u && u <= 126u && c != '$' && c != '*';
}

/* Exactamente `ancho` digitos decimales. */
static bool digitos_exactos(campo_t c, size_t ancho, uint32_t *v) {
    if (c.n != ancho) return false;
    uint32_t acc = 0;
    for (size_t i = 0; i < c.n; i++) {
        if (!es_digito(c.s[i])) return false;
        acc = acc * 10u + (uint32_t)(c.s[i] - '0');
    }
    *v = acc;
    return true;
}

/* De 1 a 10 digitos decimales, con valor <= UINT32_MAX. */
static bool digitos_u32(campo_t c, uint32_t *v) {
    if (c.n < 1u || c.n > 10u) return false;
    uint64_t acc = 0;
    for (size_t i = 0; i < c.n; i++) {
        if (!es_digito(c.s[i])) return false;
        acc = acc * 10u + (uint64_t)(c.s[i] - '0');
    }
    if (acc > UINT32_MAX) return false;
    *v = (uint32_t)acc;
    return true;
}

/* "MAJ.MIN.PAT": tres grupos de al menos un digito separados por '.'. */
static bool version_fw(campo_t c, char *dst) {
    if (c.n < 5u || c.n >= PARSER_FW_MAX) return false;
    unsigned puntos = 0;
    size_t grupo = 0;
    for (size_t i = 0; i < c.n; i++) {
        if (c.s[i] == '.') {
            if (grupo == 0u) return false;
            puntos++;
            grupo = 0;
        } else if (es_digito(c.s[i])) {
            grupo++;
        } else {
            return false;
        }
    }
    if (puntos != 2u || grupo == 0u) return false;
    memcpy(dst, c.s, c.n);
    dst[c.n] = '\0';
    return true;
}

/* Divide s[0..n) por ','. Devuelve el numero de campos, o MAX_CAMPOS + 1 si
 * hay mas de los que caben. */
static size_t dividir(const char *s, size_t n, campo_t *campos) {
    size_t k = 0, inicio = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || s[i] == ',') {
            if (k == MAX_CAMPOS) return MAX_CAMPOS + 1u;
            campos[k].s = s + inicio;
            campos[k].n = i - inicio;
            k++;
            inicio = i + 1u;
        }
    }
    return k;
}

static bool decodificar_env(const campo_t *c, size_t k, parser_env_t *e) {
    uint32_t v;
    if (k != 4u) return false;
    if (!digitos_exactos(c[0], 8u, &e->seq)) return false;
    if (!digitos_exactos(c[3], 1u, &v) || v > 3u) return false;
    e->estado = (uint8_t)v;
    e->valida = (v == 0u);
    e->temp_centi = 0;
    e->hr_centi = 0;
    if (!e->valida) {
        /* Con estado != 0, temp y hr van vacios (protocolo §3). */
        return c[1].n == 0u && c[2].n == 0u;
    }
    /* temp: signo + 5 digitos, en centesimas de grado C. */
    if (c[1].n != 6u || (c[1].s[0] != '+' && c[1].s[0] != '-')) return false;
    campo_t magnitud = {c[1].s + 1, 5u};
    if (!digitos_exactos(magnitud, 5u, &v)) return false;
    e->temp_centi = (c[1].s[0] == '-') ? -(int32_t)v : (int32_t)v;
    /* hr: 5 digitos en centesimas de %. Ademas se exige hr <= 10000 (100,00 %):
     * mas no es una humedad relativa posible, el firmware nunca lo genera
     * (env_humidity_centi() <= 10000) y no cabria en hr_centi (uint16_t). */
    if (!digitos_exactos(c[2], 5u, &v) || v > 10000u) return false;
    e->hr_centi = (uint16_t)v;
    return true;
}

static bool decodificar_sta(const campo_t *c, size_t k, parser_sta_t *s) {
    uint32_t v;
    if (k != 6u) return false;
    if (!digitos_u32(c[0], &s->uptime_s)) return false;
    if (!digitos_u32(c[1], &s->err_i2c)) return false;
    if (!digitos_exactos(c[2], 1u, &v) || v > 4u) return false;
    s->causa_reset = (uint8_t)v;
    if (!version_fw(c[3], s->fw)) return false;
    if (!digitos_exactos(c[4], 5u, &s->lazo_max_us)) return false;
    if (!digitos_exactos(c[5], 5u, &s->pila_libre_b)) return false;
    return true;
}

/* Decodifica la carga util (sin '$' ni "*CS"), ya validada por checksum. */
static decodificacion_t decodificar(const char *carga, size_t n, parser_msg_t *out) {
    /* <TIPO>: tres letras mayusculas, seguidas de ',' o del fin de la carga. */
    if (n < 3u) return DEC_TRAMA;
    for (size_t i = 0; i < 3u; i++) {
        if (carga[i] < 'A' || carga[i] > 'Z') return DEC_TRAMA;
    }
    if (n > 3u && carga[3] != ',') return DEC_TRAMA;

    parser_tipo_t tipo;
    if (memcmp(carga, "ENV", 3) == 0) tipo = PARSER_ENV;
    else if (memcmp(carga, "STA", 3) == 0) tipo = PARSER_STA;
    else if (memcmp(carga, "BTN", 3) == 0) tipo = PARSER_BTN;
    else return DEC_DESCONOCIDA; /* incluye SYS y ACK, que van Linux -> STM32 */

    if (n == 3u) return DEC_TRAMA; /* tipo conocido sin campos */
    campo_t campos[MAX_CAMPOS];
    size_t k = dividir(carga + 4, n - 4u, campos);

    parser_msg_t m;
    memset(&m, 0, sizeof(m));
    m.tipo = tipo;
    bool ok;
    switch (tipo) {
    case PARSER_ENV: ok = decodificar_env(campos, k, &m.u.env); break;
    case PARSER_STA: ok = decodificar_sta(campos, k, &m.u.sta); break;
    default: ok = (k == 1u && campos[0].n == 3u && memcmp(campos[0].s, "RST", 3) == 0); break;
    }
    if (!ok) return DEC_TRAMA;
    *out = m;
    return DEC_OK;
}

/*
 * Deteccion de tramas perdidas por seq (RF-12, PU-06):
 *  - El primer ENV solo fija la referencia.
 *  - hueco = (seq - (ultimo + 1)) mod 10^8, asi 99999999 -> 0 no es perdida.
 *  - Un seq repetido no suma.
 *  - Si un STA trae uptime_s menor que el anterior, el STM32 se reinicio: el
 *    siguiente ENV se cuenta desde 0 (seq = 0 no suma; seq = 2 suma 2).
 *  - Interpretacion propia (no definida en el protocolo v0.2): si seq avanza
 *    mas de medio rango (10^8 / 2) sin STA de reinicio, se interpreta como un
 *    retroceso por reinicio cuyo STA se perdio. Se toma como nueva referencia,
 *    no suma a `perdidas` y se cuenta en reinicios_inferidos.
 */
static void registrar_env(parser_t *p, uint32_t seq) {
    if (!p->hay_seq) {
        p->hay_seq = true;
    } else if (p->reinicio_pendiente) {
        p->cont.perdidas += seq;
    } else if (seq != p->ultimo_seq) {
        uint32_t avance = (seq + SEQ_MODULO - p->ultimo_seq) % SEQ_MODULO;
        if (avance > SEQ_MODULO / 2u) p->reinicios_inferidos++;
        else p->cont.perdidas += avance - 1u;
    }
    p->reinicio_pendiente = false;
    p->ultimo_seq = seq;
}

static void registrar_sta(parser_t *p, uint32_t uptime_s) {
    if (p->hay_uptime && uptime_s < p->ultimo_uptime) p->reinicio_pendiente = true;
    p->hay_uptime = true;
    p->ultimo_uptime = uptime_s;
}

/* Valida una linea completa buf[0..len) que empieza en '$' y termina en '\n'. */
static parser_tipo_t procesar_linea(parser_t *p, parser_msg_t *out) {
    const char *b = p->buf;
    const size_t len = p->len;
    /* Minimo: '$' + 1 byte de carga + "*CS\r\n". */
    if (len < 7u || b[len - 2u] != '\r' || b[len - 5u] != '*') {
        p->cont.err_trama++;
        return PARSER_NADA;
    }
    int hi = hex_mayuscula(b[len - 4u]), lo = hex_mayuscula(b[len - 3u]);
    if (hi < 0 || lo < 0) {
        p->cont.err_trama++;
        return PARSER_NADA;
    }
    const char *carga = b + 1;
    const size_t n = len - 6u;
    uint8_t cs = 0;
    for (size_t i = 0; i < n; i++) {
        if (!caracter_carga(carga[i])) {
            p->cont.err_trama++;
            return PARSER_NADA;
        }
        cs ^= (uint8_t)carga[i];
    }
    if (cs != (uint8_t)((hi << 4) | lo)) {
        p->cont.err_checksum++;
        return PARSER_NADA;
    }
    parser_msg_t m;
    switch (decodificar(carga, n, &m)) {
    case DEC_DESCONOCIDA:
        p->cont.desconocidas++;
        return PARSER_NADA;
    case DEC_TRAMA:
        p->cont.err_trama++;
        return PARSER_NADA;
    case DEC_OK:
        break;
    }
    p->cont.tramas_ok++;
    if (m.tipo == PARSER_ENV) registrar_env(p, m.u.env.seq);
    else if (m.tipo == PARSER_STA) registrar_sta(p, m.u.sta.uptime_s);
    *out = m;
    return m.tipo;
}

void parser_init(parser_t *p) {
    if (p) memset(p, 0, sizeof(*p));
}

parser_tipo_t parser_byte(parser_t *p, uint8_t b, parser_msg_t *out) {
    parser_msg_t descarte;
    if (!p) return PARSER_NADA;
    if (!out) out = &descarte;
    const char c = (char)b;

    if (c == '$') {
        /* Un '$' a mitad de linea: la trama anterior quedo truncada. */
        if (p->en_trama) p->cont.err_trama++;
        p->en_trama = true;
        p->buf[0] = c;
        p->len = 1u;
        return PARSER_NADA;
    }
    if (!p->en_trama) return PARSER_NADA; /* regla 1: descartar hasta '$' */

    p->buf[p->len++] = c; /* len < PARSER_MAX_LINE por la condicion de abajo */
    if (c == '\n') {
        p->en_trama = false;
        parser_tipo_t t = procesar_linea(p, out);
        p->len = 0u;
        return t;
    }
    if (p->len == PARSER_MAX_LINE) {
        /* Regla 2: 64 bytes sin '\n'. Se descarta y se vuelve a buscar '$'. */
        p->cont.err_trama++;
        p->en_trama = false;
        p->len = 0u;
    }
    return PARSER_NADA;
}

size_t parser_bloque(parser_t *p, const uint8_t *datos, size_t n,
                     parser_msg_t *out, parser_tipo_t *tipo) {
    parser_tipo_t t = PARSER_NADA;
    size_t i = 0;
    if (p && datos) {
        while (i < n && t == PARSER_NADA) t = parser_byte(p, datos[i++], out);
    }
    if (tipo) *tipo = t;
    return i;
}

const parser_contadores_t *parser_contadores(const parser_t *p) {
    return p ? &p->cont : NULL;
}

uint32_t parser_reinicios_inferidos(const parser_t *p) {
    return p ? p->reinicios_inferidos : 0u;
}
