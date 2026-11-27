/*
 * Alarmas por umbral y frescura de la medicion (ver alarm.h).
 */
#include "alarm.h"

#include <string.h>

#define SEQ_MODULO 100000000u

void alarm_init(alarm_t *a, const alarm_umbrales_t *u) {
    memset(a, 0, sizeof(*a));
    a->u = *u;
}

void alarm_umbrales(alarm_t *a, const alarm_umbrales_t *u) {
    a->u = *u;
    memset(&a->temp, 0, sizeof(a->temp));
    memset(&a->hr, 0, sizeof(a->hr));
}

static void interrumpir_rachas(alarm_t *a) {
    a->temp.racha = 0;
    a->hr.racha = 0;
}

/* Fuera de rango: v < min o v > max (los limites pertenecen al rango).
 * Desactivacion: v dentro de [min + h, max - h]. */
static void evaluar(alarm_var_t *var, int32_t v, int32_t min, int32_t max, int32_t h) {
    if (v < min || v > max) {
        if (var->racha < ALARM_LECTURAS_ACTIVAR) var->racha++;
        if (var->racha >= ALARM_LECTURAS_ACTIVAR) var->activa = true;
        return;
    }
    var->racha = 0;
    if (var->activa && v >= min + h && v <= max - h) var->activa = false;
}

void alarm_env(alarm_t *a, int64_t ahora_ms, const parser_env_t *env) {
    if (a->hay_seq) {
        if (env->seq == a->ultimo_seq) return;  /* repetida: no es nueva */
        if (env->seq != (a->ultimo_seq + 1u) % SEQ_MODULO) interrumpir_rachas(a);
    }
    a->hay_seq = true;
    a->ultimo_seq = env->seq;

    if (!env->valida) {
        a->ultima_invalida = true;
        interrumpir_rachas(a);
        return;
    }
    a->ultima_invalida = false;
    a->hay_valida = true;
    a->ts_valida_ms = ahora_ms;
    a->ultima = *env;
    evaluar(&a->temp, env->temp_centi, a->u.temp_min_centi, a->u.temp_max_centi,
            ALARM_HISTERESIS_TEMP_CENTI);
    evaluar(&a->hr, (int32_t)env->hr_centi, a->u.hr_min_centi, a->u.hr_max_centi,
            ALARM_HISTERESIS_HR_CENTI);
}

void alarm_estado(const alarm_t *a, int64_t ahora_ms, alarm_estado_t *out) {
    memset(out, 0, sizeof(*out));
    out->valida = a->hay_valida && !a->ultima_invalida &&
                  ahora_ms - a->ts_valida_ms <= ALARM_FRESCURA_MS;
    if (out->valida) out->medicion = a->ultima;
    out->alarma_temp = a->temp.activa;
    out->alarma_hr = a->hr.activa;
    out->alarma = out->alarma_temp || out->alarma_hr;
}
