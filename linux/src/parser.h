#ifndef ENVMOND_PARSER_H
#define ENVMOND_PARSER_H

/*
 * Parser de tramas UART STM32 -> Milk-V para envmond.
 *
 * Unica definicion valida del protocolo: docs/protocolo-uart.md (v0.2).
 * El parser no hace E/S ni usa memoria dinamica: recibe bytes y devuelve
 * mensajes decodificados y contadores (RF-10, RF-11, RF-12, RF-62).
 * Pruebas: PU-04, PU-05 y PU-06 (linux/tests/test_parser.c).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Longitud maxima de una linea, incluidos '$' y "\r\n" (protocolo §2). */
#define PARSER_MAX_LINE 64u
/* Capacidad de parser_sta_t.fw, incluido el '\0'. */
#define PARSER_FW_MAX 12u

typedef enum {
    PARSER_NADA = 0,
    PARSER_ENV,
    PARSER_STA,
    PARSER_BTN /* solo existe BTN,RST; no lleva mas datos */
} parser_tipo_t;

typedef struct {
    uint32_t seq;        /* 0..99999999 */
    uint8_t estado;      /* 0..3 */
    bool valida;         /* estado == 0 */
    int32_t temp_centi;  /* centesimas de grado C; 0 si !valida */
    uint16_t hr_centi;   /* centesimas de %HR; 0 si !valida */
} parser_env_t;

typedef struct {
    uint32_t uptime_s;
    uint32_t err_i2c;
    uint8_t causa_reset;      /* 0..4 */
    char fw[PARSER_FW_MAX];   /* "MAJ.MIN.PAT" */
    uint32_t lazo_max_us;     /* 0..99999 */
    uint32_t pila_libre_b;    /* 0..99999 */
} parser_sta_t;

typedef struct {
    parser_tipo_t tipo;
    union {
        parser_env_t env;
        parser_sta_t sta;
    } u;
} parser_msg_t;

/* Mismos nombres que el objeto "uart" de GET /api/estado (interfaces.md §2). */
typedef struct {
    uint32_t tramas_ok;
    uint32_t err_checksum;
    uint32_t err_trama;
    uint32_t desconocidas;
    uint32_t perdidas;
} parser_contadores_t;

/* Estado interno: tratarlo como opaco y usar solo las funciones de abajo. */
typedef struct {
    char buf[PARSER_MAX_LINE];
    size_t len;
    bool en_trama;
    parser_contadores_t cont;
    bool hay_seq;
    uint32_t ultimo_seq;
    bool hay_uptime;
    uint32_t ultimo_uptime;
    bool reinicio_pendiente;
    uint32_t reinicios_inferidos;
} parser_t;

void parser_init(parser_t *p);

/* Procesa un byte. Si completa un mensaje valido, llena *out y devuelve su
 * tipo; en otro caso devuelve PARSER_NADA. */
parser_tipo_t parser_byte(parser_t *p, uint8_t b, parser_msg_t *out);

/* Consume bytes de datos[0..n) hasta completar un mensaje o agotar el bloque.
 * Devuelve cuantos bytes uso y deja el tipo en *tipo (PARSER_NADA si no
 * completo ninguno). El llamador repite con el resto del bloque. */
size_t parser_bloque(parser_t *p, const uint8_t *datos, size_t n,
                     parser_msg_t *out, parser_tipo_t *tipo);

const parser_contadores_t *parser_contadores(const parser_t *p);

/* Retrocesos de seq sin STA previo que se trataron como reinicio del STM32.
 * Dato interno de diagnostico: no forma parte de /api/estado. */
uint32_t parser_reinicios_inferidos(const parser_t *p);

#endif
