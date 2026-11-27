#ifndef ENVMOND_ALARM_H
#define ENVMOND_ALARM_H

/*
 * Alarmas por umbral y frescura de la medicion (RF-23, RF-24, RF-25).
 *
 * Logica pura: no hace E/S ni lee el reloj; el llamador pasa el tiempo en
 * milisegundos de un reloj monotonico (CLOCK_MONOTONIC). Valores en
 * centesimas, como el parser UART.
 *
 * Pruebas: PU-13 (linux/tests/test_alarm.c).
 */

#include <stdbool.h>
#include <stdint.h>

#include "parser.h"

/* RF-24: lecturas nuevas, validas y consecutivas fuera de rango para activar. */
#define ALARM_LECTURAS_ACTIVAR 5u
/* RF-24: histeresis para desactivar (0,5 °C y 2 puntos de HR). */
#define ALARM_HISTERESIS_TEMP_CENTI 50
#define ALARM_HISTERESIS_HR_CENTI   200
/* RF-25: sin una medicion valida nueva en los ultimos 5 s -> "sin datos". */
#define ALARM_FRESCURA_MS 5000

typedef struct {
    int32_t temp_min_centi;
    int32_t temp_max_centi;
    int32_t hr_min_centi;
    int32_t hr_max_centi;
} alarm_umbrales_t;

/* Estado interno de una variable (tratarlo como opaco). */
typedef struct {
    uint8_t racha;   /* lecturas consecutivas fuera de rango */
    bool activa;
} alarm_var_t;

/* Estado interno (tratarlo como opaco). */
typedef struct {
    alarm_umbrales_t u;
    alarm_var_t temp, hr;
    bool hay_seq;
    uint32_t ultimo_seq;
    bool hay_valida;
    bool ultima_invalida;     /* la ENV mas reciente traia estado != 0 */
    int64_t ts_valida_ms;
    parser_env_t ultima;      /* ultima ENV valida */
} alarm_t;

/* Resultado para la web, el LED (SYS) y /api/estado. */
typedef struct {
    bool valida;              /* medicion valida y fresca; si no, "sin datos" */
    parser_env_t medicion;    /* solo tiene sentido si valida */
    bool alarma_temp;
    bool alarma_hr;
    bool alarma;              /* alarma_temp || alarma_hr */
} alarm_estado_t;

void alarm_init(alarm_t *a, const alarm_umbrales_t *u);

/* Cambia los umbrales (pagina de ajustes, RF-32). Las rachas y las alarmas
 * activas se reinician: se evaluan de nuevo con los umbrales nuevos. */
void alarm_umbrales(alarm_t *a, const alarm_umbrales_t *u);

/* Procesa una ENV aceptada por el parser, recibida en ahora_ms.
 *  - seq repetido: se ignora (no es una lectura nueva).
 *  - hueco de seq o ENV invalida: la racha se interrumpe.
 *  - ENV invalida: "sin datos" de inmediato; la alarma conserva su estado. */
void alarm_env(alarm_t *a, int64_t ahora_ms, const parser_env_t *env);

void alarm_estado(const alarm_t *a, int64_t ahora_ms, alarm_estado_t *out);

#endif
