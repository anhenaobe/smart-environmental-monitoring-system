#ifndef ENVMOND_SERVICIO_H
#define ENVMOND_SERVICIO_H

/*
 * Nucleo de envmond: une parser, alarm, logger, config y reset y decide el
 * modo del equipo y el latido SYS (RF-13, RF-40, RF-44, RF-52, RF-53).
 *
 * No abre la UART ni lee relojes: el lazo principal (envmond.c) le pasa los
 * bytes recibidos, el tiempo monotonico en ms y la hora de pared, y recibe
 * las tramas a enviar por la funcion tx. Asi se prueba con relojes simulados
 * (linux/tests/test_servicio.c).
 *
 * Dueno unico: solo el hilo del lazo principal llama a estas funciones. Los
 * manejadores HTTP (Fase C) leen una instantanea y piden aplicar
 * configuracion a traves del lazo, protegidos por un mutex corto.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "alarm.h"
#include "config.h"
#include "logger.h"
#include "parser.h"
#include "reset.h"

#define SERVICIO_SYS_PERIODO_MS 1000   /* RF-13: SYS cada 1 s */
#define SERVICIO_SYS3_MS        2000   /* SYS,3 visible 2 s (LED a 5 Hz) */
#define SERVICIO_DIR_MAX        256u

typedef enum {
    SERVICIO_BRINGUP = 0,  /* sin configuracion: SYS,0, sin historial ni alarmas */
    SERVICIO_NORMAL,       /* SYS,1 o SYS,2 */
    SERVICIO_RESET         /* SYS,3 */
} servicio_modo_t;

typedef void (*servicio_tx_fn)(void *ctx, const char *carga);   /* p. ej. "SYS,1" */
typedef void (*servicio_log_fn)(void *ctx, const char *mensaje);

typedef struct {
    const char *dir_config;              /* /etc/envmon */
    const char *dir_datos;               /* /var/lib/envmon */
    servicio_tx_fn tx;
    servicio_log_fn log;                 /* NULL: sin log */
    void *ctx;
    const reset_fs_ops_t *reset_ops;     /* NULL: POSIX */
} servicio_params_t;

/* Estado interno (tratarlo como opaco). */
typedef struct {
    char dir_config[SERVICIO_DIR_MAX];
    char dir_datos[SERVICIO_DIR_MAX];
    servicio_tx_fn tx;
    servicio_log_fn log;
    void *ctx;
    const reset_fs_ops_t *reset_ops;

    servicio_modo_t modo;
    bool hay_config;
    config_t cfg;
    parser_t parser;
    alarm_t alarm;
    logger_t logger;
    bool logger_activo;

    bool hay_env;
    parser_env_t ultima_env;
    bool hay_sta;
    parser_sta_t sta;
    bool hay_trama;
    int64_t ts_trama_ms;

    int64_t fin_reset_ms;
    bool reset_incompleto;
    int64_t proximo_sys_ms;
    uint32_t resets;
} servicio_t;

/* Lo que publica GET /api/estado (docs/interfaces.md §2). */
typedef struct {
    time_t ts;
    uint8_t estado_sistema;              /* 0..3, mismos valores que SYS */
    bool hay_config;
    char nombre[CONFIG_NOMBRE_MAX + 1u];
    bool valida;                         /* false: "sin datos" (RF-25) */
    bool hay_env;
    parser_env_t medicion;               /* seq y estado de la ultima ENV */
    bool alarma_temp, alarma_hr;
    parser_contadores_t uart;
    int64_t seg_desde_ultima;            /* -1 si nunca llego una trama */
    bool hay_sta;
    parser_sta_t stm32;
} servicio_instantanea_t;

/* Arranque: si existe reset.pending completa el reset ANTES de todo lo
 * demas (RF-53); si no, carga la configuracion (sin ella o invalida: modo
 * bring-up). Devuelve 0, o -1 si los parametros no son validos. */
int servicio_init(servicio_t *s, const servicio_params_t *p, int64_t mono_ms, time_t ahora);

/* Bytes recibidos por la UART. */
void servicio_bytes(servicio_t *s, const uint8_t *datos, size_t n, int64_t mono_ms, time_t ahora);

/* Llamarlo en cada vuelta del lazo (al menos una vez por segundo): latido
 * SYS, cierre de intervalos del historial y fin del reset. */
void servicio_tick(servicio_t *s, int64_t mono_ms, time_t ahora);

/* Guarda y aplica una configuracion (bring-up o ajustes): pasa a modo normal
 * y envia SYS de inmediato (RF-44). Devuelve 0; -1 si no es valida; -2 si
 * fallo el guardado; -3 si hay un reset en curso. */
int servicio_aplicar_config(servicio_t *s, const config_t *c, int64_t mono_ms, time_t ahora);

/* Al detener el servicio: escribe el intervalo en curso del historial. */
void servicio_detener(servicio_t *s);

uint8_t servicio_estado_sistema(const servicio_t *s, int64_t mono_ms);
servicio_modo_t servicio_modo(const servicio_t *s);
void servicio_instantanea(const servicio_t *s, int64_t mono_ms, time_t ahora,
                          servicio_instantanea_t *out);

/* Ms hasta el proximo evento temporizado (para el plazo de poll). */
int servicio_plazo_ms(const servicio_t *s, int64_t mono_ms);

#endif
