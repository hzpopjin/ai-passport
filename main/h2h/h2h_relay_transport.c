#include "h2h_relay_transport.h"
#include <string.h>
static uint16_t get16(const uint8_t *p){return p[0]|(uint16_t)p[1]<<8;}
static void put16(uint8_t *p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
void h2h_relay_transport_reset(h2h_relay_transport_t *t){uint32_t g=t->generation+1;if(t->working){t->active=false;t->queued=false;t->reply=false;t->generation=g;return;}memset(t,0,sizeof(*t));t->generation=g;}
bool h2h_relay_transport_write(h2h_relay_transport_t *t,const uint8_t *f,size_t n){
    if(!t||!f||n<3||n>20)return false;
    uint16_t id=get16(f+1);
    switch(f[0]){
    case 0x10:
        if(n!=5||t->working||t->queued||(t->active&&!t->reply)||!get16(f+3)||get16(f+3)>H2H_RELAY_RPC_MAX)return false;
        h2h_relay_transport_reset(t);t->active=true;t->transaction=id;t->total=get16(f+3);return true;
    case 0x11:
        if(n<=5||!t->active||t->working||t->queued||t->reply||id!=t->transaction||get16(f+3)!=t->received||t->received+n-5>t->total)return false;
        memcpy(t->bytes+t->received,f+5,n-5);t->received+=(uint16_t)(n-5);return true;
    case 0x12:
        if(n!=3||!t->active||t->working||t->queued||t->reply||id!=t->transaction||t->received!=t->total)return false;
        t->bytes[t->total]=0;t->queued=true;return true;
    case 0x13:
        if(n!=5||!t->active||id!=t->transaction||get16(f+3)>t->total)return false;
        t->offset=get16(f+3);return true;
    case 0x14:
        if(n!=3||!t->active||id!=t->transaction||t->working)return false;
        h2h_relay_transport_reset(t);return true;
    default:return false;
    }
}
size_t h2h_relay_transport_read(const h2h_relay_transport_t *t,uint8_t out[20]){
    memset(out,0,20);out[0]=1;out[1]=t->error?2:t->reply?1:0;put16(out+2,t->transaction);
    put16(out+4,t->reply?t->total:0);put16(out+6,t->reply?t->offset:0);
    size_t n=t->reply?t->total-t->offset:0;if(n>12)n=12;
    if(n)memcpy(out+8,t->bytes+t->offset,n);
    return n+8;
}
char *h2h_relay_transport_take(h2h_relay_transport_t *t,uint32_t *generation){
    if(!t->queued||t->working)return NULL;
    t->queued=false;t->working=true;*generation=t->generation;return t->bytes;
}
bool h2h_relay_transport_finish(h2h_relay_transport_t *t,uint32_t generation,size_t length){
    if(!t->working)return false;
    if(t->generation!=generation){t->working=false;h2h_relay_transport_reset(t);return false;}if(length>H2H_RELAY_RPC_MAX)return false;
    t->working=false;t->reply=true;t->total=(uint16_t)length;t->offset=0;return true;
}
