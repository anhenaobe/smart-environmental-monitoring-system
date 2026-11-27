#include "env_dht11.h"

dht11_status_t env_dht11_decode(const uint8_t frame[5],
                                int32_t *temp_centi, uint16_t *rh_centi) {
    if (!frame || !temp_centi || !rh_centi) return DHT11_INVALID_VALUE;
    if ((uint8_t)((unsigned)frame[0] + frame[1] + frame[2] + frame[3]) != frame[4])
        return DHT11_BAD_CHECKSUM;

    const unsigned tenth = frame[3] & 0x7Fu;

    /* ASAIR V1.3 documents integer RH and temperature tenths; support
     * the older integer-only byte format as a subset. Do not interpret
     * an unknown derivative's nonzero RH low byte as fictitious digits. */
    if (frame[1] != 0u || tenth > 9u || frame[0] < 5u || frame[0] > 95u)
        return DHT11_INVALID_VALUE;

    int32_t temp = (int32_t)frame[2] * 100 + (int32_t)tenth * 10;
    if (frame[3] & 0x80u) temp = -temp;
    if (temp < -2000 || temp > 6000) return DHT11_INVALID_VALUE;

    *temp_centi = temp;
    *rh_centi = (uint16_t)((unsigned)frame[0] * 100u);
    return DHT11_OK;
}

int env_dht11_bit(uint32_t high_us) {
    if (high_us >= 10u && high_us <= 43u) return 0;
    if (high_us >= 52u && high_us <= 100u) return 1;
    return -1;
}
