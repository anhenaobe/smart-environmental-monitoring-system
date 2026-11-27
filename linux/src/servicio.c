/*
 * Nucleo de envmond (ver servicio.h).
 */
#include "servicio.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void registrar(const servicio_t *s, const char *fmt, ...) {
    if (s->log == NULL) return;
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    s->log(s->ctx, msg);
}

static void enviar(const servicio_t *s, const char *carga) {
    if (s->tx != NULL) s->tx(s->ctx, carga);
}

static alarm_umbrales_t umbrales_de(const config_t *c) {
    alarm_umbrales_t u = { c->temp_min_centi, c->temp_max_centi, c->hr_min_centi,
                           c->hr_max_centi };
    return u;
}

/* En bring-up no hay umbrales: alarm solo se usa para la frescura de la
 * medicion (RF-25) y su alarma nunca se publica. */
static void entrar_bringup(servicio_t *s) {
    config_t sugerida;
    config_sugerida(&sugerida);
    const alarm_umbrales_t u = umbrales_de(&sugerida);
    s->modo = SERVICIO_BRINGUP;
    s->hay_config = false;
    s->logger_activo = false;
    alarm_init(&s->alarm, &u);
}

static void entrar_normal(servicio_t *s, const config_t *c) {
    const alarm_umbrales_t u = umbrales_de(c);
    s->cfg = *c;
    s->hay_config = true;
    s->modo = SERVICIO_NORMAL;
    alarm_init(&s->alarm, &u);
    s->logger_activo = logger_init(&s->logger, s->dir_datos, c->intervalo_registro_s,
                                   c->retencion_dias) == 0;
    if (!s->logger_activo) registrar(s, "historial: directorio de datos no valido");
}

static bool completar_reset(servicio_t *s) {
    if (reset_completar(s->dir_config, s->dir_datos, s->reset_ops) != 0) {
        if (!s->reset_incompleto) registrar(s, "reset: no se pudo completar el borrado; se reintenta");
        s->reset_incompleto = true;
        return false;
    }
    s->reset_incompleto = false;
    s->resets++;
    registrar(s, "reset: configuracion e historial borrados");
    return true;
}

static bool copiar_dir(char *dst, const char *src) {
    if (src == NULL) return false;
    const size_t n = strlen(src);
    if (n == 0u || n >= SERVICIO_DIR_MAX) return false;
    memcpy(dst, src, n + 1u);
    return true;
}

int servicio_init(servicio_t *s, const servicio_params_t *p, int64_t mono_ms, time_t ahora) {
    (void)ahora;
    memset(s, 0, sizeof(*s));
    if (!copiar_dir(s->dir_config, p->dir_config) || !copiar_dir(s->dir_datos, p->dir_datos))
        return -1;
    s->tx = p->tx;
    s->log = p->log;
    s->ctx = p->ctx;
    s->reset_ops = p->reset_ops;
    parser_init(&s->parser);
    entrar_bringup(s);
    s->proximo_sys_ms = mono_ms;  /* primer SYS en el primer tick */

    if (reset_pendiente(s->dir_config)) {
        registrar(s, "arranque: existe %s, se completa el reset", RESET_MARCADOR);
        if (!completar_reset(s)) {
            s->modo = SERVICIO_RESET;
            s->fin_reset_ms = mono_ms;
        }
        return 0;
    }

    config_t c;
    char detalle[128] = "";
    switch (config_cargar(s->dir_config, &c, detalle, sizeof(detalle))) {
    case CONFIG_CARGA_OK:
        entrar_normal(s, &c);
        registrar(s, "arranque: configuracion cargada (%s)", c.nombre);
        break;
    case CONFIG_CARGA_NO_EXISTE:
        registrar(s, "arranque: sin configuracion, modo bring-up");
        break;
    case CONFIG_CARGA_INVALIDA:
        registrar(s, "arranque: configuracion invalida (%s); modo bring-up, el archivo se conserva",
                  detalle);
        break;
    case CONFIG_CARGA_ERROR_IO:
        registrar(s, "arranque: no se pudo leer la configuracion (%s); modo bring-up", detalle);
        break;
    }
    return 0;
}

static void manejar_btn(servicio_t *s, int64_t mono_ms) {
    /* Idempotente: un BTN repetido (ACK perdido) vuelve a marcar, confirmar
     * y completar; borrar lo que ya no existe no es error. */
    if (reset_marcar(s->dir_config, s->reset_ops) != 0) {
        registrar(s, "reset: no se pudo persistir %s; no se envia ACK", RESET_MARCADOR);
        return;
    }
    enviar(s, "ACK,RST");
    enviar(s, "SYS,3");
    registrar(s, "reset: BTN,RST recibido, marcador persistido y ACK enviado");
    s->modo = SERVICIO_RESET;
    s->hay_config = false;
    s->logger_activo = false;  /* el historial se borra: no se vacia */
    s->fin_reset_ms = mono_ms + SERVICIO_SYS3_MS;
    s->proximo_sys_ms = mono_ms + SERVICIO_SYS_PERIODO_MS;
    (void)completar_reset(s);
}

static void manejar_env(servicio_t *s, const parser_env_t *e, int64_t mono_ms, time_t ahora) {
    s->hay_env = true;
    s->ultima_env = *e;
    alarm_env(&s->alarm, mono_ms, e);
    if (s->modo == SERVICIO_NORMAL && s->logger_activo && logger_muestra(&s->logger, ahora, e) != 0)
        registrar(s, "historial: no se pudo escribir en %s", s->dir_datos);
}

void servicio_bytes(servicio_t *s, const uint8_t *datos, size_t n, int64_t mono_ms, time_t ahora) {
    while (n > 0u) {
        parser_msg_t m;
        parser_tipo_t t;
        const size_t usados = parser_bloque(&s->parser, datos, n, &m, &t);
        datos += usados;
        n -= usados;
        if (t == PARSER_NADA) continue;
        s->hay_trama = true;
        s->ts_trama_ms = mono_ms;
        if (t == PARSER_ENV) {
            manejar_env(s, &m.u.env, mono_ms, ahora);
        } else if (t == PARSER_STA) {
            s->hay_sta = true;
            s->sta = m.u.sta;
        } else if (t == PARSER_BTN) {
            manejar_btn(s, mono_ms);
        }
    }
}

uint8_t servicio_estado_sistema(const servicio_t *s, int64_t mono_ms) {
    switch (s->modo) {
    case SERVICIO_RESET: return 3;
    case SERVICIO_BRINGUP: return 0;
    case SERVICIO_NORMAL: break;
    }
    alarm_estado_t e;
    alarm_estado(&s->alarm, mono_ms, &e);
    return e.alarma ? 2 : 1;
}

servicio_modo_t servicio_modo(const servicio_t *s) { return s->modo; }

void servicio_tick(servicio_t *s, int64_t mono_ms, time_t ahora) {
    if (s->modo == SERVICIO_NORMAL && s->logger_activo && logger_tick(&s->logger, ahora) != 0)
        registrar(s, "historial: no se pudo escribir o aplicar la retencion en %s", s->dir_datos);

    if (s->modo == SERVICIO_RESET && mono_ms >= s->fin_reset_ms) {
        if (s->reset_incompleto && mono_ms >= s->proximo_sys_ms) (void)completar_reset(s);
        if (!s->reset_incompleto) {
            entrar_bringup(s);
            s->proximo_sys_ms = mono_ms;  /* SYS,0 de inmediato */
        }
    }

    if (mono_ms >= s->proximo_sys_ms) {
        char carga[8];
        snprintf(carga, sizeof(carga), "SYS,%u", (unsigned)servicio_estado_sistema(s, mono_ms));
        enviar(s, carga);
        s->proximo_sys_ms += SERVICIO_SYS_PERIODO_MS;
        /* Si el lazo se atraso mas de un periodo, no se envian rafagas. */
        if (s->proximo_sys_ms <= mono_ms) s->proximo_sys_ms = mono_ms + SERVICIO_SYS_PERIODO_MS;
    }
}

int servicio_aplicar_config(servicio_t *s, const config_t *c, int64_t mono_ms, time_t ahora) {
    (void)ahora;
    if (s->modo == SERVICIO_RESET) return -3;
    const int r = config_guardar(s->dir_config, c, NULL);
    if (r != 0) {
        registrar(s, "configuracion: no se pudo guardar (%d)", r);
        return r;
    }
    if (s->modo == SERVICIO_NORMAL && s->logger_activo) (void)logger_vaciar(&s->logger);
    entrar_normal(s, c);
    s->proximo_sys_ms = mono_ms;  /* RF-44: SYS,1 en el siguiente tick */
    registrar(s, "configuracion: guardada y aplicada (%s)", c->nombre);
    return 0;
}

void servicio_detener(servicio_t *s) {
    if (s->modo == SERVICIO_NORMAL && s->logger_activo && logger_vaciar(&s->logger) != 0)
        registrar(s, "historial: no se pudo escribir el ultimo intervalo");
    const parser_contadores_t *c = parser_contadores(&s->parser);
    registrar(s, "fin: tramas_ok=%u err_checksum=%u err_trama=%u desconocidas=%u perdidas=%u",
              (unsigned)c->tramas_ok, (unsigned)c->err_checksum, (unsigned)c->err_trama,
              (unsigned)c->desconocidas, (unsigned)c->perdidas);
}

void servicio_instantanea(const servicio_t *s, int64_t mono_ms, time_t ahora,
                          servicio_instantanea_t *out) {
    memset(out, 0, sizeof(*out));
    out->ts = ahora;
    out->estado_sistema = servicio_estado_sistema(s, mono_ms);
    out->hay_config = s->hay_config;
    if (s->hay_config) memcpy(out->nombre, s->cfg.nombre, sizeof(out->nombre));
    alarm_estado_t a;
    alarm_estado(&s->alarm, mono_ms, &a);
    out->valida = a.valida;
    out->hay_env = s->hay_env;
    if (s->hay_env) {
        out->medicion = s->ultima_env;
        if (!a.valida) { out->medicion.temp_centi = 0; out->medicion.hr_centi = 0; }
    }
    out->alarma_temp = s->modo == SERVICIO_NORMAL && a.alarma_temp;
    out->alarma_hr = s->modo == SERVICIO_NORMAL && a.alarma_hr;
    out->uart = *parser_contadores(&s->parser);
    out->seg_desde_ultima = s->hay_trama ? (mono_ms - s->ts_trama_ms) / 1000 : -1;
    out->hay_sta = s->hay_sta;
    if (s->hay_sta) out->stm32 = s->sta;
}

int servicio_plazo_ms(const servicio_t *s, int64_t mono_ms) {
    int64_t d = s->proximo_sys_ms - mono_ms;
    if (s->modo == SERVICIO_RESET && s->fin_reset_ms - mono_ms < d) d = s->fin_reset_ms - mono_ms;
    if (d < 0) d = 0;
    if (d > SERVICIO_SYS_PERIODO_MS) d = SERVICIO_SYS_PERIODO_MS;
    return (int)d;
}
