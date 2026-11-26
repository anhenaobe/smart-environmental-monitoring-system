#include "env_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void vector(const char *expected, const char *payload) {
    char frame[ENV_MAX_FRAME+1], decoded[ENV_MAX_FRAME+1];
    size_t n = env_frame(frame, sizeof(frame), payload);
    assert(n == strlen(expected));
    assert(!strcmp(frame, expected));
    assert(env_parse(frame, n, decoded, sizeof(decoded)) == strlen(payload));
    assert(!strcmp(payload, decoded));
}

static size_t feed(env_rx_t *rx, const char *data, size_t n, char *payload) {
    size_t last = 0;
    for (size_t i=0; i<n; ++i)
        last = env_rx_byte(rx, data[i], payload, ENV_MAX_FRAME+1);
    return last;
}

int main(void) {
    char msg[ENV_MAX_FRAME+1], payload[ENV_MAX_FRAME+1];
    const uint8_t crc_vector[]={0xBE,0xEF};
    assert(env_sht_crc8(crc_vector,2)==0x92);
    for (unsigned i=0; i<16; ++i) {
        uint8_t wrong[]={0xBE,0xEF};
        wrong[i/8] ^= (uint8_t)(1u << (i%8));
        assert(env_sht_crc8(wrong,2)!=0x92);
    }
    /* Exhaustive conversion vs. double precision datasheet equation. */
    for (unsigned raw=0; raw<=65535u; ++raw) {
        double t = -4500.0 + 17500.0 * raw / 65535.0;
        double h = 10000.0 * raw / 65535.0;
        double dt=env_temp_centi((uint16_t)raw)-t;
        double dh=env_humidity_centi((uint16_t)raw)-h;
        assert(dt>=-0.501 && dt<=0.501 && dh>=-0.501 && dh<=0.501);
    }
    vector("$ENV,00000042,+02450,05310,0*44\r\n","ENV,00000042,+02450,05310,0");
    vector("$ENV,00000043,,,1*6B\r\n","ENV,00000043,,,1");
    vector("$STA,00000003,0,0,0.1.0,00850,02048*77\r\n","STA,00000003,0,0,0.1.0,00850,02048");
    vector("$A*41\r\n","A"); /* Framing layer; semantic layer rejects unknown types. */
    vector("$SYS,0*45\r\n","SYS,0");
    vector("$SYS,1*44\r\n","SYS,1");
    vector("$SYS,3*46\r\n","SYS,3");
    vector("$BTN,RST*21\r\n","BTN,RST");
    vector("$ACK,RST*30\r\n","ACK,RST");

    env_rx_t rx;env_rx_init(&rx);
    size_t n=env_frame(msg,sizeof(msg),"ENV,00000042,+02450,05310,0");
    assert(feed(&rx,msg,n,payload)==strlen("ENV,00000042,+02450,05310,0"));
    msg[5]='X';
    assert(!feed(&rx,msg,n,payload) && rx.errors==1);
    /* Re-synchronize after overlong and truncated frames, not random noise. */
    for(int i=0;i<90;i++) env_rx_byte(&rx,(i==0)?'$':'A',payload,sizeof(payload));
    assert(rx.errors==2);
    assert(!feed(&rx,"noise",5,payload) && rx.errors==2);
    assert(!feed(&rx,"$SYS",4,payload));
    n=env_frame(msg,sizeof(msg),"SYS,1");
    assert(feed(&rx,msg,n,payload)==5 && !strcmp(payload,"SYS,1") && rx.errors==3);
    assert(env_frame(msg,5,"SYS,1")==0);
    assert(env_frame(msg,sizeof(msg),"x*y")==0);
    assert(env_frame(msg,sizeof(msg),"x$y")==0);
    assert(env_frame(msg,sizeof(msg),"x\ny")==0);
    assert(env_frame(msg,sizeof(msg),"x\xFF")==0);
    assert(env_parse("$\x7F*7F\r\n",7,payload,sizeof(payload))==0);

    /* Exact maximum frame and too-long payload. */
    char limit[60];memset(limit,'A',sizeof(limit));limit[58]='\0';
    n=env_frame(msg,sizeof(msg),limit);
    assert(n==64 && feed(&rx,msg,n,payload)==58);
    limit[58]='A';limit[59]='\0';
    assert(!env_frame(msg,sizeof(msg),limit));

    /* Deterministic parser stress with hostile bytes, then recovery. */
    uint32_t prng=0x87654321u;
    for(unsigned i=0;i<100000u;i++) {
        prng=1664525u*prng+1013904223u;
        (void)env_rx_byte(&rx,(char)(prng>>24),payload,sizeof(payload));
    }
    n=env_frame(msg,sizeof(msg),"SYS,1");
    assert(feed(&rx,msg,n,payload)==5 && !strcmp(payload,"SYS,1"));
    puts("PASS: CRC bit errors, exhaustive conversion, protocol vectors, re-sync, size limits and parser stress");
    return 0;
}
