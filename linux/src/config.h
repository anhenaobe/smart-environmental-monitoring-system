#ifndef ENVMOND_CONFIG_H
#define ENVMOND_CONFIG_H

/*
 * Configuracion de envmond: validacion, carga y guardado atomico de
 * <dir>/config (docs/interfaces.md §3; RF-32, RF-41, RF-42, RF-43).
 *
 * Los umbrales se guardan en centesimas (igual que el parser UART) para no
 * usar coma flotante ni depender del locale. En el archivo se escriben como
 * decimales con punto: 1800 -> "18.0", 1825 -> "18.25".
 *
 * Pruebas: PU-07 (guardado atomico), PU-14 (validacion) y PU-15 (hash).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alarm.h"

#define CONFIG_NOMBRE_MAX   40u   /* bytes, sin el '\0' */
#define CONFIG_NTP_MAX      253u  /* longitud maxima de un nombre de host */
#define CONFIG_HASH_MAX     128u  /* capacidad de admin_hash con '\0' */
#define CONFIG_PASSWORD_MIN 8u
#define CONFIG_PASSWORD_MAX 64u

/* Rangos de cada campo (RF-21, RF-22, RF-42; rango del DHT11 V1.3). */
#define CONFIG_INTERVALO_MIN 60u
#define CONFIG_INTERVALO_MAX 3600u
#define CONFIG_RETENCION_MIN 1u
#define CONFIG_RETENCION_MAX 30u
#define CONFIG_TEMP_MIN_CENTI (-2000)
#define CONFIG_TEMP_MAX_CENTI 6000
#define CONFIG_HR_MIN_CENTI   500
#define CONFIG_HR_MAX_CENTI   9500
/* Decision de Mateo (2026-11-27): max - min >= 2 x histeresis de RF-24, para
 * que exista la banda [min + h, max - h] donde la alarma se desactiva. */
#define CONFIG_ANCHO_MIN_TEMP_CENTI (2 * ALARM_HISTERESIS_TEMP_CENTI)  /* 1,0 °C */
#define CONFIG_ANCHO_MIN_HR_CENTI   (2 * ALARM_HISTERESIS_HR_CENTI)    /* 4 puntos */

typedef struct {
    char nombre[CONFIG_NOMBRE_MAX + 1u];
    char admin_hash[CONFIG_HASH_MAX];
    char ntp[CONFIG_NTP_MAX + 1u];      /* "" = sin NTP */
    uint32_t intervalo_registro_s;
    int32_t temp_min_centi;
    int32_t temp_max_centi;
    int32_t hr_min_centi;
    int32_t hr_max_centi;
    uint32_t retencion_dias;
} config_t;

/* Campos, en el orden en que aparecen en el formulario y en el archivo. */
typedef enum {
    CONFIG_CAMPO_NOMBRE = 0,
    CONFIG_CAMPO_PASSWORD,   /* en el archivo: admin_hash */
    CONFIG_CAMPO_NTP,
    CONFIG_CAMPO_INTERVALO,
    CONFIG_CAMPO_TEMP_MIN,
    CONFIG_CAMPO_TEMP_MAX,
    CONFIG_CAMPO_HR_MIN,
    CONFIG_CAMPO_HR_MAX,
    CONFIG_CAMPO_RETENCION,
    CONFIG_N_CAMPOS
} config_campo_t;

typedef enum {
    CONFIG_MOTIVO_OK = 0,
    CONFIG_MOTIVO_VACIO,     /* campo obligatorio vacio */
    CONFIG_MOTIVO_FORMATO,   /* no es un numero / hash invalido / host invalido */
    CONFIG_MOTIVO_RANGO,     /* numero fuera del rango permitido */
    CONFIG_MOTIVO_ORDEN,     /* minimo >= maximo (se marca en el campo maximo) */
    CONFIG_MOTIVO_ESTRECHO,  /* maximo - minimo < 2 x histeresis de RF-24 (en el maximo) */
    CONFIG_MOTIVO_LONGITUD,  /* texto demasiado corto o largo */
    CONFIG_MOTIVO_CARACTER   /* caracter no permitido */
} config_motivo_t;

/* Datos tal como llegan del formulario de bring-up o ajustes (texto). */
typedef struct {
    const char *nombre;
    const char *password;
    const char *ntp;
    const char *intervalo_registro_s;
    const char *temp_min;
    const char *temp_max;
    const char *hr_min;
    const char *hr_max;
    const char *retencion_dias;
} config_form_t;

/* Valida todos los campos del formulario. motivos[c] queda en
 * CONFIG_MOTIVO_OK o con el motivo del error de ese campo (RF-42: el error
 * se muestra junto al campo). Si no hay errores, llena *out con la
 * configuracion y el hash de la contrasena. Devuelve el numero de campos con
 * error, o -1 si fallo el calculo del hash. */
int config_desde_formulario(const config_form_t *f, config_t *out,
                            config_motivo_t motivos[CONFIG_N_CAMPOS]);

/* Valida una configuracion ya armada (por ejemplo, la leida del archivo).
 * Devuelve el numero de campos con error y llena motivos[] si no es NULL. */
int config_validar(const config_t *c, config_motivo_t motivos[CONFIG_N_CAMPOS]);

/* Valores sugeridos del perfil documental (criterios-conservacion.md §3). */
void config_sugerida(config_t *c);

typedef enum {
    CONFIG_CARGA_OK = 0,
    CONFIG_CARGA_NO_EXISTE,  /* no hay <dir>/config: modo bring-up */
    CONFIG_CARGA_INVALIDA,   /* existe pero esta corrupta o no valida */
    CONFIG_CARGA_ERROR_IO
} config_carga_t;

/* Lee <dir>/config. Ignora <dir>/config.tmp (restos de un guardado
 * interrumpido). Si devuelve INVALIDA o ERROR_IO y detalle != NULL, deja ahi
 * una descripcion breve para el log. El archivo invalido no se borra. */
config_carga_t config_cargar(const char *dir, config_t *out,
                             char *detalle, size_t detalle_n);

/* Operaciones de archivo usadas por config_guardar; se pueden sustituir en
 * las pruebas para inyectar fallas en cada paso (PU-07). */
typedef struct {
    long (*escribir)(int fd, const void *buf, size_t n);
    int (*sincronizar)(int fd);
    int (*renombrar)(const char *desde, const char *hacia);
    int (*sincronizar_dir)(const char *dir);
} config_fs_ops_t;

/* Operaciones reales: write, fsync, rename y fsync del directorio. */
const config_fs_ops_t *config_fs_ops_posix(void);

/* Guardado atomico (RF-43): escribe <dir>/config.tmp, fsync, close, rename
 * a <dir>/config y fsync del directorio. Valida antes de escribir. Devuelve
 * 0 si todo salio bien, -1 si la configuracion no es valida y -2 si fallo un
 * paso de E/S. Si el fallo ocurre antes del rename, <dir>/config queda como
 * estaba; si ocurre despues, ya contiene la configuracion nueva completa.
 * ops == NULL usa config_fs_ops_posix(). */
int config_guardar(const char *dir, const config_t *c, const config_fs_ops_t *ops);

/* Hash SHA-512 de crypt(3) ("$6$" + sal de 16 caracteres de getrandom).
 * Devuelve 0 si tuvo exito. */
int config_hash_password(const char *password, char out[CONFIG_HASH_MAX]);

/* true si la contrasena coincide con el hash; compara en tiempo constante. */
bool config_verificar_password(const char *password, const char *hash);

/* Convierte "18", "18.5", "-5.25" a centesimas. Sin espacios, con punto
 * decimal y como maximo dos decimales. Devuelve false si el formato no es
 * valido. Se expone porque la usan las pruebas y el formulario. */
bool config_parse_centi(const char *s, int32_t *out);

#endif
