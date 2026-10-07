#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define H2H_RELAY_RPC_MAX 4096u
/* Caller serializes this bounded byte-only transport. No JSON/crypto/storage. */
typedef struct {
    char bytes[H2H_RELAY_RPC_MAX+1];
    uint16_t transaction,total,received,offset;
    uint32_t generation;
    bool active,queued,working,reply,error;
} h2h_relay_transport_t;
void h2h_relay_transport_reset(h2h_relay_transport_t *t);
bool h2h_relay_transport_write(h2h_relay_transport_t *t,const uint8_t *f,size_t n);
size_t h2h_relay_transport_read(const h2h_relay_transport_t *t,uint8_t out[20]);
char *h2h_relay_transport_take(h2h_relay_transport_t *t,uint32_t *generation);
bool h2h_relay_transport_finish(h2h_relay_transport_t *t,uint32_t generation,size_t length);
