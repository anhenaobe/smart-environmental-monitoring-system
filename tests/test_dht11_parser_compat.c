#include "parser.h"
#include "env_dht11.h"
#include "env_protocol.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static parser_msg_t feed(parser_t *p, const char *payload) {
    char wire[65];
    size_t n = env_frame(wire,sizeof wire,payload);
    assert(n);
    parser_msg_t msg;
    parser_tipo_t t=PARSER_NADA;
    for (size_t i=0;i<n;i++) t=parser_byte(p,(uint8_t)wire[i],&msg);
    assert(t != PARSER_NADA);
    return msg;
}
int main(void) {
    parser_t p; parser_init(&p);
    const uint8_t data[5]={53,0,24,4,81};
    int32_t temp; uint16_t rh;
    assert(env_dht11_decode(data,&temp,&rh)==DHT11_OK);
    char payload[65]; snprintf(payload,sizeof payload,"ENV,00000000,%+06d,%05u,0",(int)temp,(unsigned)rh);
    parser_msg_t msg=feed(&p,payload);
    assert(msg.tipo==PARSER_ENV && msg.u.env.valida && msg.u.env.temp_centi==2440 && msg.u.env.hr_centi==5300);
    msg=feed(&p,"ENV,00000001,,,1"); assert(!msg.u.env.valida && msg.u.env.estado==1);
    msg=feed(&p,"ENV,00000002,,,2"); assert(!msg.u.env.valida && msg.u.env.estado==2);
    msg=feed(&p,"ENV,00000003,,,3"); assert(!msg.u.env.valida && msg.u.env.estado==3);
    msg=feed(&p,"STA,00000010,2,0,0.3.0,00850,02048");
    assert(msg.tipo==PARSER_STA && msg.u.sta.err_i2c==2 && !strcmp(msg.u.sta.fw,"0.3.0"));
    assert(parser_contadores(&p)->tramas_ok==5 && parser_contadores(&p)->perdidas==0);
    puts("DHT11 -> env_protocol -> parser envmond: PASS");
}
