#pragma once
#include "h2h_pocket.h"
#include "stayc_game_store.h"

#define H2H_FULL_JOURNAL_BYTES 1024u
#define H2H_FULL_POINTER_BYTES 32u
#define H2H_FULL_PROFILE "stayc-epoch-v2-164"
typedef struct {
    uint8_t local[164], remote[164], recovery[164];
    uint64_t revision, remote_revision, generation;
    bool has_local, dirty, conflict, restore_ack, has_remote, has_recovery, server_full, healthy;
    int bank;
} h2h_fullsave_t;
typedef struct {
    /* read returns 1=present,0=missing,-1=error; exact lengths are required. */
    int (*read)(int bank, void *bytes, size_t length, void *context);
    bool (*write)(int bank, const void *bytes, size_t length, void *context);
    void *context; /* bank0/1=complete journal; bank2=commit pointer */
} h2h_fullsave_io_t;
void h2h_fullsave_init(h2h_fullsave_t *f);
bool h2h_fullsave_valid(const void *bytes,size_t length,uint64_t epoch);
/* A missing pointer keeps legacy state authoritative; a bad pointer locks. */
bool h2h_fullsave_load(h2h_fullsave_t *f,h2h_pocket_t *p,const h2h_fullsave_io_t *io);
bool h2h_fullsave_commit(h2h_fullsave_t *f,h2h_pocket_t *p,
                        const h2h_fullsave_t *next,const h2h_pocket_record_t *owner,
                        const h2h_fullsave_io_t *io,bool model_changed);
bool h2h_fullsave_seed(h2h_fullsave_t *f,h2h_pocket_t *p,const void *legacy,size_t length,
                      const h2h_fullsave_io_t *io);
bool h2h_fullsave_local(h2h_fullsave_t *f,h2h_pocket_t *p,const stayc_game_save_t *game,
                       uint32_t settled,const h2h_fullsave_io_t *io);
bool h2h_fullsave_pocket_write(h2h_fullsave_t *f,h2h_pocket_t *p,const void *bytes,size_t length,
                              const h2h_fullsave_io_t *io);
bool h2h_fullsave_reply(h2h_fullsave_t *f,h2h_pocket_t *p,const uint8_t sent[164],
                       uint64_t sent_revision,uint64_t revision,const uint8_t remote[164],
                       bool conflict,bool restore,const h2h_fullsave_io_t *io);
bool h2h_fullsave_restore(h2h_fullsave_t *f,h2h_pocket_t *p,const h2h_fullsave_io_t *io);
