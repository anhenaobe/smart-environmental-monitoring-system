#ifndef ENV_DHT11_H
#define ENV_DHT11_H

#include <stdint.h>

typedef enum {
    DHT11_OK = 0,
    DHT11_TIMEOUT = 1,       /* no reply, invalid pulse timing, or stuck bus */
    DHT11_BAD_CHECKSUM = 2,  /* sum of first four octets, modulo 256 */
    DHT11_INVALID_VALUE = 3  /* valid checksum but invalid payload */
} dht11_status_t;

/* Decode the five bytes in ASAIR DHT11 V1.3 (20170331) format:
 * RH integer, RH fraction (=0), T magnitude, T tenths (MSB=sign), sum.
 * Outputs are only written for DHT11_OK and use the UART centi-units. */
dht11_status_t env_dht11_decode(const uint8_t frame[5],
                                int32_t *temp_centi, uint16_t *rh_centi);

/* Decode a measured high-pulse width in whole microseconds.
 * Nominal DHT11 pulses are 23..27 us (0) / 68..74 us (1); reject
 * gross timing errors and ambiguous middle widths rather than guess. */
int env_dht11_bit(uint32_t high_us);
#endif
