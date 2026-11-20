#include "env_protocol.h"
#include <stdio.h>
#include <string.h>

static int hex(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
size_t env_frame(char *dst, size_t cap, const char *payload) {
    if (!dst || !payload) return 0;
    const size_t n = strlen(payload);
    if (!n || n + 6u > ENV_MAX_FRAME || n + 7u > cap) return 0;
    uint8_t cs = 0;
    for (size_t i=0;i<n;i++) cs ^= (uint8_t)payload[i];
    int written = snprintf(dst, cap, "$%s*%02X\r\n", payload, (unsigned)cs);
    return written > 0 && (size_t)written == n+6u ? (size_t)written : 0;
}
size_t env_parse(const char *raw, size_t len, char *payload, size_t cap) {
    if (!raw || !payload || len < 8u || len > ENV_MAX_FRAME) return 0;
    if (raw[0] != '$' || raw[len-2] != '\r' || raw[len-1] != '\n') return 0;
    if (raw[len-5] != '*') return 0;
    int hi = hex((unsigned char)raw[len-4]), lo = hex((unsigned char)raw[len-3]);
    if (hi < 0 || lo < 0) return 0;
    size_t n=len-6u;
    if (n == 0 || n+1u > cap) return 0;
    uint8_t actual=0;
    for (size_t i=0;i<n;i++) {
        if ((unsigned char)raw[i+1] < 32 || raw[i+1] == '*') return 0;
        actual ^= (uint8_t)raw[i+1];
    }
    if (actual != (uint8_t)((hi<<4)|lo)) return 0;
    memcpy(payload,raw+1,n);payload[n]='\0';
    return n;
}
void env_rx_init(env_rx_t *rx) { if (rx) memset(rx,0,sizeof(*rx)); }
size_t env_rx_byte(env_rx_t *rx, char b, char *payload, size_t cap) {
    if (!rx) return 0;
    if (b == '$') {rx->length=0;rx->bytes[rx->length++]=b;return 0;}
    if (!rx->length) return 0;
    if (rx->length >= ENV_MAX_FRAME) {rx->length=0;rx->errors++;return 0;}
    rx->bytes[rx->length++]=b;
    if (b != '\n') return 0;
    size_t result=env_parse(rx->bytes,rx->length,payload,cap);
    if (!result) rx->errors++;
    rx->length=0;
    return result;
}
uint8_t env_sht_crc8(const uint8_t *data, size_t len) {
    uint8_t crc=0xFF;
    if (!data) return crc;
    for (size_t i=0;i<len;i++) {
        crc ^= data[i];
        for (unsigned b=0;b<8;b++) {
            if (crc & 0x80u) crc=(uint8_t)((((unsigned)crc)<<1)^0x31u);
            else crc=(uint8_t)(((unsigned)crc)<<1);
        }
    }
    return crc;
}
int32_t env_temp_centi(uint16_t raw) {
    return -4500 + (int32_t)((17500u * (uint32_t)raw + 32767u) / 65535u);
}
uint16_t env_humidity_centi(uint16_t raw) {
    return (uint16_t)((10000u * (uint32_t)raw + 32767u) / 65535u);
}
