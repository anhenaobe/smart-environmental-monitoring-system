#include "env_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char msg[ENV_MAX_FRAME+1], payload[ENV_MAX_FRAME+1];
    const uint8_t vector[]={0xBE,0xEF};
    assert(env_sht_crc8(vector,2)==0x92);
    assert(env_temp_centi(0)==-4500 && env_temp_centi(65535)==13000);
    assert(env_humidity_centi(0)==0 && env_humidity_centi(65535)==10000);
    const char *val="ENV,00000042,+02450,05310,0";
    size_t n=env_frame(msg,sizeof(msg),val);
    assert(n>0 && n<=64);
    assert(!strcmp(msg,"$ENV,00000042,+02450,05310,0*44\r\n"));
    assert(env_parse(msg,n,payload,sizeof(payload))==strlen(val));
    assert(!strcmp(payload,val));
    env_rx_t rx;env_rx_init(&rx);
    size_t got=0;
    for(size_t i=0;i<n;i++) got=env_rx_byte(&rx,msg[i],payload,sizeof(payload));
    assert(got==strlen(val));
    assert(!strcmp(payload,val));
    msg[5]='X';
    for(size_t i=0;i<n;i++) got=env_rx_byte(&rx,msg[i],payload,sizeof(payload));
    assert(!got && rx.errors==1);
    /* Re-synchronization after overlong and fragmented data. */
    for(int i=0;i<90;i++) env_rx_byte(&rx,(i==0)?'$':'A',payload,sizeof(payload));
    assert(rx.errors>=2);
    n=env_frame(msg,sizeof(msg),"SYS,1");assert(n && !strcmp(msg,"$SYS,1*44\r\n"));
    for(size_t i=0;i<n;i++) got=env_rx_byte(&rx,msg[i],payload,sizeof(payload));
    assert(got==5 && !strcmp(payload,"SYS,1"));
    assert(env_frame(msg,5,"SYS,1")==0);
    puts("PASS: SHT31 CRC, conversion, ENV/SYS vectors, checksum, re-sync and bounds");
    return 0;
}
