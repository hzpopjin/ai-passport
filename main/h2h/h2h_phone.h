#pragma once
#include "h2h_fullsave.h"
#include "pocket_profile.h"
#define H2H_PHONE_PROTOCOL "pocket-phone-jwt-v1"
#define H2H_PHONE_RPC_MAX 4096u
/* Caller supplies an authenticated current BLE connection, never a JWT. */
typedef struct {
    h2h_pocket_t *pocket;
    h2h_fullsave_t *full;
    const h2h_fullsave_io_t *io;
    const char *device;
    bool (*sha)(const void *,size_t,uint8_t[32]);
    pocket_profile_t *profile;
} h2h_phone_t;
size_t h2h_phone_rpc(h2h_phone_t *phone,const char *input,size_t length,char out[H2H_PHONE_RPC_MAX+1]);
