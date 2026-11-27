/*
 * Pruebas de desarrollo del modulo alarm de envmond:
 *   PU-13 histeresis y persistencia de la alarma (RF-24), y la frescura
 *   "sin datos" de RF-25 (la prueba formal de RF-25 es PI-05).
 * La verificacion formal la hace otro integrante (docs/matriz-verificacion.md).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <cmocka.h>
#include "alarm.h"

/* Perfil sugerido: 18-22 °C y 35-50 % HR (criterios-conservacion.md §3). */
static const alarm_umbrales_t PERFIL = { 1800, 2200, 3500, 5000 };

typedef struct {
    alarm_t a;
    uint32_t seq;
    int64_t t;
} sim_t;

static void sim_init(sim_t *s) {
    alarm_init(&s->a, &PERFIL);
    s->seq = 0;
    s->t = 0;
}

/* Una ENV valida nueva cada 3 s. */
static void lectura(sim_t *s, int32_t temp, uint16_t hr) {
    parser_env_t e = { .seq = s->seq++, .estado = 0, .valida = true,
                       .temp_centi = temp, .hr_centi = hr };
    alarm_env(&s->a, s->t, &e);
    s->t += 3000;
}

static void lecturas(sim_t *s, unsigned n, int32_t temp, uint16_t hr) {
    for (unsigned i = 0; i < n; i++) lectura(s, temp, hr);
}

static void invalida(sim_t *s, uint8_t estado) {
    parser_env_t e = { .seq = s->seq++, .estado = estado, .valida = false };
    alarm_env(&s->a, s->t, &e);
    s->t += 3000;
}

static alarm_estado_t estado_en(const sim_t *s, int64_t t) {
    alarm_estado_t e;
    alarm_estado(&s->a, t, &e);
    return e;
}

static alarm_estado_t estado(const sim_t *s) { return estado_en(s, s->t - 3000); }

#define T_OK 2000
#define H_OK 4200

/* ---------- activacion: 5 lecturas consecutivas ---------- */

static void pu13_cuatro_no_cinco_si(void **state) {
    (void)state;
    static const struct { int32_t t; uint16_t h; bool temp; } casos[] = {
        { 2201, H_OK, true },   /* temperatura alta */
        { 1799, H_OK, true },   /* temperatura baja */
        { T_OK, 5001, false },  /* HR alta */
        { T_OK, 3499, false },  /* HR baja */
    };
    for (size_t i = 0; i < sizeof(casos) / sizeof(casos[0]); i++) {
        sim_t s;
        sim_init(&s);
        lecturas(&s, 4, casos[i].t, casos[i].h);
        assert_false(estado(&s).alarma);
        lectura(&s, casos[i].t, casos[i].h);
        alarm_estado_t e = estado(&s);
        assert_true(e.alarma);
        assert_int_equal(e.alarma_temp, casos[i].temp);
        assert_int_equal(e.alarma_hr, !casos[i].temp);
    }
}

static void pu13_los_limites_estan_dentro(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lecturas(&s, 10, 2200, 5000);
    lecturas(&s, 10, 1800, 3500);
    assert_false(estado(&s).alarma);
}

static void pu13_lectura_en_rango_interrumpe_la_racha(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lecturas(&s, 4, 2300, H_OK);
    lectura(&s, T_OK, H_OK);
    lecturas(&s, 4, 2300, H_OK);
    assert_false(estado(&s).alarma);
    lectura(&s, 2300, H_OK);
    assert_true(estado(&s).alarma_temp);
}

static void pu13_env_invalida_interrumpe_la_racha(void **state) {
    (void)state;
    for (uint8_t est = 1; est <= 3; est++) {
        sim_t s;
        sim_init(&s);
        lecturas(&s, 4, 2300, H_OK);
        invalida(&s, est);
        lecturas(&s, 4, 2300, H_OK);
        assert_false(estado(&s).alarma);
        lectura(&s, 2300, H_OK);
        assert_true(estado(&s).alarma);
    }
}

static void pu13_hueco_de_seq_interrumpe_la_racha(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lecturas(&s, 4, 2300, H_OK);
    s.seq++;  /* se perdio una trama */
    lecturas(&s, 4, 2300, H_OK);
    assert_false(estado(&s).alarma);
    lectura(&s, 2300, H_OK);
    assert_true(estado(&s).alarma);
}

static void pu13_seq_repetido_no_es_lectura_nueva(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lecturas(&s, 4, 2300, H_OK);
    s.seq--;  /* la cuarta llega repetida */
    lectura(&s, 2300, H_OK);
    assert_false(estado(&s).alarma);
    lectura(&s, 2300, H_OK);
    assert_true(estado(&s).alarma);
}

static void pu13_desbordamiento_de_seq_es_consecutivo(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    s.seq = 99999997u;
    lecturas(&s, 3, 2300, H_OK);  /* 99999997..99999999 */
    s.seq = 0;
    lecturas(&s, 2, 2300, H_OK);  /* 0, 1 */
    assert_true(estado(&s).alarma);
}

static void pu13_variables_independientes(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lecturas(&s, 5, 2300, 5500);
    alarm_estado_t e = estado(&s);
    assert_true(e.alarma_temp && e.alarma_hr && e.alarma);
    lectura(&s, T_OK, 5500);  /* la temperatura vuelve; la HR sigue fuera */
    e = estado(&s);
    assert_false(e.alarma_temp);
    assert_true(e.alarma_hr && e.alarma);
}

/* ---------- desactivacion con histeresis ---------- */

static void pu13_histeresis_temperatura(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lecturas(&s, 5, 2300, H_OK);
    lectura(&s, 2200, H_OK);   /* en rango, pero sin superar la histeresis */
    assert_true(estado(&s).alarma_temp);
    lectura(&s, 2151, H_OK);
    assert_true(estado(&s).alarma_temp);
    lectura(&s, 2150, H_OK);   /* 22,0 - 0,5 */
    assert_false(estado(&s).alarma_temp);

    sim_init(&s);
    lecturas(&s, 5, 1700, H_OK);
    lectura(&s, 1849, H_OK);
    assert_true(estado(&s).alarma_temp);
    lectura(&s, 1850, H_OK);   /* 18,0 + 0,5 */
    assert_false(estado(&s).alarma_temp);
}

static void pu13_histeresis_humedad(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lecturas(&s, 5, T_OK, 5100);
    lectura(&s, T_OK, 4801);
    assert_true(estado(&s).alarma_hr);
    lectura(&s, T_OK, 4800);   /* 50 - 2 */
    assert_false(estado(&s).alarma_hr);

    sim_init(&s);
    lecturas(&s, 5, T_OK, 3400);
    lectura(&s, T_OK, 3699);
    assert_true(estado(&s).alarma_hr);
    lectura(&s, T_OK, 3700);   /* 35 + 2 */
    assert_false(estado(&s).alarma_hr);
}

/* Con la resolucion entera del DHT11 (1 °C, 1 % HR) la histeresis de 0,5 °C
 * se comporta como un escalon de 1 °C: con maximo 22 °C, 22 no apaga y 21 si. */
static void pu13_resolucion_dht11_escalon_entero(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lecturas(&s, 5, 2300, 4500);
    lectura(&s, 2200, 4500);
    assert_true(estado(&s).alarma_temp);
    lectura(&s, 2100, 4500);
    assert_false(estado(&s).alarma_temp);

    sim_init(&s);
    lecturas(&s, 5, 2000, 5100);
    lectura(&s, 2000, 4900);   /* 49 % todavia esta dentro de la histeresis */
    assert_true(estado(&s).alarma_hr);
    lectura(&s, 2000, 4800);
    assert_false(estado(&s).alarma_hr);
}

static void pu13_alarma_se_conserva_sin_datos(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lecturas(&s, 5, 2300, H_OK);
    invalida(&s, 1);
    alarm_estado_t e = estado(&s);
    assert_false(e.valida);
    assert_true(e.alarma);
    e = estado_en(&s, s.t + 60000);  /* un minuto sin tramas */
    assert_false(e.valida);
    assert_true(e.alarma);
    lectura(&s, T_OK, H_OK);         /* vuelve una lectura en banda */
    e = estado(&s);
    assert_true(e.valida);
    assert_false(e.alarma);
}

static void pu13_cambio_de_umbrales_reinicia(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lecturas(&s, 5, 2300, H_OK);
    assert_true(estado(&s).alarma);
    const alarm_umbrales_t nuevos = { 1800, 2500, 3500, 5000 };
    alarm_umbrales(&s.a, &nuevos);
    assert_false(estado(&s).alarma);
    lecturas(&s, 4, 2600, H_OK);
    assert_false(estado(&s).alarma);
    lectura(&s, 2600, H_OK);
    assert_true(estado(&s).alarma);
}

/* ---------- RF-25: "sin datos" ---------- */

static void rf25_sin_datos_al_arrancar(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    assert_false(estado_en(&s, 0).valida);
}

static void rf25_frescura_de_5_s(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    s.t = 10000;
    lectura(&s, 2450, 5310);
    alarm_estado_t e = estado_en(&s, 10000 + ALARM_FRESCURA_MS);
    assert_true(e.valida);
    assert_int_equal(e.medicion.temp_centi, 2450);
    assert_int_equal(e.medicion.hr_centi, 5310);
    assert_false(estado_en(&s, 10000 + ALARM_FRESCURA_MS + 1).valida);
}

static void rf25_env_invalida_invalida_de_inmediato(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lectura(&s, 2450, 5310);
    int64_t t_invalida = s.t;
    invalida(&s, 2);
    assert_false(estado_en(&s, t_invalida).valida);
    lectura(&s, 2450, 5310);
    assert_true(estado(&s).valida);
}

/* Con ENV cada 3 s, una sola trama perdida deja 6 s sin dato valido: la web
 * muestra "sin datos" entre los 5 y los 6 s. Comportamiento esperado de
 * RF-25 en main; queda anotado para proponer otro umbral a Andrew. */
static void rf25_una_trama_perdida_da_sin_datos_momentaneo(void **state) {
    (void)state;
    sim_t s;
    sim_init(&s);
    lectura(&s, 2450, 5310);          /* t = 0 */
    s.seq++;                          /* la de t = 3 s se pierde */
    assert_true(estado_en(&s, 4000).valida);
    assert_false(estado_en(&s, 5500).valida);
    s.t = 6000;
    lectura(&s, 2450, 5310);
    assert_true(estado_en(&s, 6000).valida);
}

int main(void) {
    const struct CMUnitTest pruebas[] = {
        cmocka_unit_test(pu13_cuatro_no_cinco_si),
        cmocka_unit_test(pu13_los_limites_estan_dentro),
        cmocka_unit_test(pu13_lectura_en_rango_interrumpe_la_racha),
        cmocka_unit_test(pu13_env_invalida_interrumpe_la_racha),
        cmocka_unit_test(pu13_hueco_de_seq_interrumpe_la_racha),
        cmocka_unit_test(pu13_seq_repetido_no_es_lectura_nueva),
        cmocka_unit_test(pu13_desbordamiento_de_seq_es_consecutivo),
        cmocka_unit_test(pu13_variables_independientes),
        cmocka_unit_test(pu13_histeresis_temperatura),
        cmocka_unit_test(pu13_histeresis_humedad),
        cmocka_unit_test(pu13_resolucion_dht11_escalon_entero),
        cmocka_unit_test(pu13_alarma_se_conserva_sin_datos),
        cmocka_unit_test(pu13_cambio_de_umbrales_reinicia),
        cmocka_unit_test(rf25_sin_datos_al_arrancar),
        cmocka_unit_test(rf25_frescura_de_5_s),
        cmocka_unit_test(rf25_env_invalida_invalida_de_inmediato),
        cmocka_unit_test(rf25_una_trama_perdida_da_sin_datos_momentaneo),
    };
    return cmocka_run_group_tests_name("alarm", pruebas, NULL, NULL);
}
