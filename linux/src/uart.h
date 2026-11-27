#ifndef ENVMOND_UART_H
#define ENVMOND_UART_H

/*
 * Puerto serie de envmond hacia el STM32 (docs/protocolo-uart.md §1):
 * 115200 baudios, 8N1, sin control de flujo, modo crudo y no bloqueante.
 * La salida pasa por un bufer pequeno que el lazo vacia con POLLOUT.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define UART_RUTA_MAX 128u
#define UART_TX_MAX   512u   /* ~15 tramas SYS/ACK pendientes */

typedef struct {
    int fd;                  /* -1 si esta cerrado */
    char ruta[UART_RUTA_MAX];
    uint8_t tx[UART_TX_MAX];
    size_t tx_len;
    uint32_t bytes_rx, bytes_tx, tramas_tx, tramas_descartadas_tx;
} uart_t;

void uart_init(uart_t *u);

/* Abre y configura el puerto. Devuelve 0, o -1 con errno. */
int uart_abrir(uart_t *u, const char *ruta);
void uart_cerrar(uart_t *u);
bool uart_abierto(const uart_t *u);

/* Lee lo disponible sin bloquear. Devuelve los bytes leidos, 0 si no hay
 * datos y -1 si el puerto fallo (el llamador lo cierra y lo reabre). */
ssize_t uart_leer(uart_t *u, uint8_t *buf, size_t n);

/* Arma "$<carga>*<CS>\r\n" en out. Devuelve la longitud, o 0 si no cabe o
 * la carga tiene caracteres no permitidos ('$', '*', control). */
size_t uart_formatear(const char *carga, char *out, size_t n);

/* Encola una trama (por ejemplo "SYS,1") y trata de enviarla. Devuelve 0, o
 * -1 si el puerto esta cerrado o el bufer esta lleno (se descarta). */
int uart_enviar(uart_t *u, const char *carga);

/* Escribe lo pendiente sin bloquear. Devuelve 0, o -1 si el puerto fallo. */
int uart_vaciar(uart_t *u);
bool uart_tx_pendiente(const uart_t *u);

#endif
