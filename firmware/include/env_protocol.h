#ifndef ENV_PROTOCOL_H
#define ENV_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define ENV_MAX_FRAME 64u

typedef struct {
    char bytes[ENV_MAX_FRAME + 1u];
    size_t length;
    unsigned errors;
} env_rx_t;

/* The payload omits '$', '*CS' and CRLF. Return bytes written, or 0 on error. */
size_t env_frame(char *dst, size_t cap, const char *payload);
/* Validates complete raw frame, returns payload length on success, 0 on error. */
size_t env_parse(const char *raw, size_t len, char *payload, size_t cap);
void env_rx_init(env_rx_t *rx);
/* One byte in; positive result means a validated payload is ready. */
size_t env_rx_byte(env_rx_t *rx, char byte, char *payload, size_t cap);
/* Temperature/relative humidity from SHT31 raw values, in centi-units. */
int32_t env_temp_centi(uint16_t raw);
uint16_t env_humidity_centi(uint16_t raw);
uint8_t env_sht_crc8(const uint8_t *data, size_t len);
#endif
