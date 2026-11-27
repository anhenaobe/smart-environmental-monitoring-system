#include "env_dht11.h"
#include "env_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t checksum(uint8_t *frame) {
    return (uint8_t)((unsigned)frame[0] + frame[1] + frame[2] + frame[3]);
}

static void valid(uint8_t frame[5], int32_t tc_expect, uint16_t rh_expect) {
    int32_t tc = -999999;
    uint16_t rh = 0xffff;
    assert(env_dht11_decode(frame, &tc, &rh) == DHT11_OK);
    assert(tc == tc_expect && rh == rh_expect);
}

static void invalid(uint8_t frame[5], dht11_status_t expect) {
    int32_t tc = -999999;
    uint16_t rh = 0xffff;
    assert(env_dht11_decode(frame, &tc, &rh) == expect);
    assert(tc == -999999 && rh == 0xffff);
}

/* Confirm the existing ENV field sizes, signed centi-units and UART
 * checksum stay interoperable when the received sensor changes. Keep
 * these reference vectors independent of the production HAL driver. */
static void uart_vector(uint8_t dht[5], unsigned long seq, const char *expected) {
    int32_t tc = 0;
    uint16_t rh = 0;
    char payload[ENV_MAX_FRAME + 1u], frame[ENV_MAX_FRAME + 1u];
    const dht11_status_t status = env_dht11_decode(dht, &tc, &rh);
    if (status != DHT11_OK)
        (void)snprintf(payload, sizeof(payload), "ENV,%08lu,,,%u", seq, (unsigned)status);
    else
        (void)snprintf(payload, sizeof(payload), "ENV,%08lu,%+06ld,%05u,0",
                       seq, (long)tc, (unsigned)rh);
    assert(env_frame(frame, sizeof(frame), payload) == strlen(expected));
    assert(strcmp(frame, expected) == 0);
}

int main(void) {
    /* ASAIR V1.3 example (53% RH / +24.4 C) and old integer-only frame. */
    uint8_t frac[] = {53, 0, 24, 4, 81};
    valid(frac, 2440, 5300);
    uart_vector(frac, 0, "$ENV,00000000,+02440,05300,0*42\r\n");
    uint8_t integer[] = {33, 0, 26, 0, 59};
    valid(integer, 2600, 3300);
    uint8_t negative[] = {53, 0, 10, 0x81, 0};
    negative[4] = checksum(negative);
    valid(negative, -1010, 5300);
    uart_vector(negative, 42, "$ENV,00000042,-01010,05300,0*40\r\n");
    uint8_t minus_fraction[] = {40, 0, 0, 0x83, 0};
    minus_fraction[4] = checksum(minus_fraction);
    valid(minus_fraction, -30, 4000);

    uint8_t min[] = {5, 0, 20, 0x80, 0};
    min[4] = checksum(min);
    valid(min, -2000, 500);
    uint8_t max[] = {95, 0, 60, 0, 155};
    valid(max, 6000, 9500);

    frac[4] ^= 1;
    invalid(frac, DHT11_BAD_CHECKSUM);
    uart_vector(frac, 43, "$ENV,00000043,,,2*68\r\n");
    /* Reject unknown RH fraction, implausible T fraction, out-of-range T/RH. */
    uint8_t bad_rh_fraction[] = {50, 1, 20, 0, 71};
    invalid(bad_rh_fraction, DHT11_INVALID_VALUE);
    uart_vector(bad_rh_fraction, 44, "$ENV,00000044,,,3*6E\r\n");
    uint8_t bad_t_fraction[] = {50, 0, 20, 10, 80};
    invalid(bad_t_fraction, DHT11_INVALID_VALUE);
    uint8_t bad_t[] = {50, 0, 60, 1, 111};
    invalid(bad_t, DHT11_INVALID_VALUE);
    uint8_t bad_negative[] = {50, 0, 20, 0x81, 0};
    bad_negative[4] = checksum(bad_negative);
    invalid(bad_negative, DHT11_INVALID_VALUE);
    uint8_t bad_rh[] = {96, 0, 20, 0, 116};
    invalid(bad_rh, DHT11_INVALID_VALUE);
    uint8_t zero[] = {0, 0, 0, 0, 0};
    invalid(zero, DHT11_INVALID_VALUE);
    uint8_t rollover[] = {95, 0, 10, 0x81, 0};
    /* This is valid even if high bits of sum overflow the checksum octet.
     * Use a payload invalid by value to check modulo reduction explicitly. */
    rollover[2] = 40;
    rollover[4] = checksum(rollover);
    assert(rollover[4] == 8);
    invalid(rollover, DHT11_INVALID_VALUE);
    ++rollover[4];
    invalid(rollover, DHT11_BAD_CHECKSUM);

    assert(env_dht11_bit(23) == 0);
    assert(env_dht11_bit(27) == 0);
    assert(env_dht11_bit(68) == 1);
    assert(env_dht11_bit(74) == 1);
    assert(env_dht11_bit(9) == -1);
    assert(env_dht11_bit(43) == 0);
    assert(env_dht11_bit(44) == -1);
    assert(env_dht11_bit(51) == -1);
    assert(env_dht11_bit(52) == 1);
    assert(env_dht11_bit(100) == 1);
    assert(env_dht11_bit(101) == -1);
    puts("DHT11 decode, pulse classification, and UART vectors OK");
    return 0;
}
