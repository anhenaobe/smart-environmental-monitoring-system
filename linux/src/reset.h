#ifndef ENVMOND_RESET_H
#define ENVMOND_RESET_H

/*
 * Factory reset recuperable de envmond (docs/interfaces.md §4; RF-52, RF-53).
 *
 *   1. reset_marcar: crear <dir_config>/reset.pending y sincronizarlo junto
 *      con su directorio. Solo si lo logra se responde $ACK,RST.
 *   2. reset_completar: borrar los archivos regulares de <dir_datos>,
 *      sincronizar, borrar config (y config.tmp), sincronizar, retirar el
 *      marcador y sincronizar. Cada paso es idempotente: si un corte lo
 *      interrumpe, el siguiente arranque lo repite desde el principio.
 *
 * Pruebas: PU-12 (linux/tests/test_reset.c), con un corte simulado en cada
 * operacion de archivo.
 */

#include <stdbool.h>

#define RESET_MARCADOR "reset.pending"

/* Operaciones de archivo; se sustituyen en las pruebas para inyectar fallas. */
typedef struct {
    int (*crear)(const char *ruta);            /* devuelve fd o -1 */
    int (*sincronizar)(int fd);
    int (*sincronizar_dir)(const char *dir);   /* EINVAL no es error */
    int (*borrar)(const char *ruta);           /* ENOENT no es error */
} reset_fs_ops_t;

const reset_fs_ops_t *reset_fs_ops_posix(void);

/* true si existe el marcador (un reset quedo pendiente). */
bool reset_pendiente(const char *dir_config);

/* Paso 1. Devuelve 0 si el marcador es durable; -1 si no (se intenta
 * retirarlo y NO se debe enviar ACK). ops == NULL usa las operaciones POSIX. */
int reset_marcar(const char *dir_config, const reset_fs_ops_t *ops);

/* Paso 2. Devuelve 0 si termino (sin marcador), -1 si fallo un paso (el
 * marcador sigue y el borrado se puede reintentar). */
int reset_completar(const char *dir_config, const char *dir_datos, const reset_fs_ops_t *ops);

#endif
