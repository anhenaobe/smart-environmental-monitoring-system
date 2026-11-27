#ifndef ENVMOND_LOGGER_H
#define ENVMOND_LOGGER_H

/*
 * Historial agregado de envmond (RF-21, RF-22, RNF-08).
 *
 * Agrega las ENV validas por intervalo de tiempo (60..3600 s) y escribe una
 * fila por intervalo en <dir>/AAAA-MM-DD.csv, con la fecha LOCAL del inicio
 * del intervalo; asi el archivo rota a diario. Borra los archivos mas viejos
 * que la retencion configurada. El reloj de pared lo pasa el llamador
 * (time(NULL) en el servicio, un reloj simulado en las pruebas).
 *
 * Formato interno (no es contrato; la web usara la API JSON):
 *   inicio_utc,hora_ok,n_validas,temp_prom,temp_min,temp_max,hr_prom,hr_min,hr_max
 *   1795770000,1,20,24.50,24.40,24.60,53.10,53.00,53.30
 *   1795770060,1,0,,,,,,
 * inicio_utc: segundos desde la epoca. hora_ok = 0 si el reloj marcaba una
 * fecha anterior a LOGGER_HORA_MINIMA (sin NTP ni hora del navegador).
 *
 * Pruebas: PU-09 (agregacion) y PU-11 (rotacion y retencion).
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "parser.h"

/* 2026-01-01T00:00:00Z: antes de esta fecha el reloj no es confiable. */
#define LOGGER_HORA_MINIMA ((time_t)1767225600)
#define LOGGER_DIR_MAX 256u
#define LOGGER_CABECERA \
    "inicio_utc,hora_ok,n_validas,temp_prom,temp_min,temp_max,hr_prom,hr_min,hr_max\n"

/* Estado interno (tratarlo como opaco). */
typedef struct {
    char dir[LOGGER_DIR_MAX];
    uint32_t intervalo_s;
    uint32_t retencion_dias;
    bool abierto;
    time_t inicio;
    uint32_t n;
    int64_t suma_temp, suma_hr;
    int32_t min_temp, max_temp, min_hr, max_hr;
    bool hay_dia_retencion;
    int64_t dia_retencion;    /* dia local en que se aplico la retencion */
} logger_t;

/* Devuelve 0, o -1 si los parametros no son validos. No toca el disco. */
int logger_init(logger_t *lg, const char *dir, uint32_t intervalo_s, uint32_t retencion_dias);

/* Avanza el reloj: si el intervalo en curso termino (o el reloj retrocedio)
 * escribe su fila y abre el siguiente, alineado a multiplos de intervalo_s
 * en UTC. Aplica la retencion al arrancar y cada vez que cambia el dia
 * local. Llamarlo al menos una vez por segundo. Devuelve 0, o -1 si fallo
 * una escritura (esa fila se pierde y el registro continua). */
int logger_tick(logger_t *lg, time_t ahora);

/* Suma una ENV al intervalo en curso (llama a logger_tick primero). Las ENV
 * con estado != 0 no se agregan. */
int logger_muestra(logger_t *lg, time_t ahora, const parser_env_t *env);

/* Escribe el intervalo en curso aunque no haya terminado (al detener el
 * servicio) y lo cierra. */
int logger_vaciar(logger_t *lg);

/* Borra de dir los AAAA-MM-DD.csv anteriores a los ultimos retencion_dias
 * dias locales (el dia de `ahora` cuenta como uno). Ignora otros archivos y
 * conserva fechas futuras. Devuelve cuantos borro, o -1 si no pudo leer dir. */
int logger_retener(logger_t *lg, time_t ahora);

#endif
