#pragma once
#include "stayc_game.h"
#include "h2h_model.h"

/* Private, epoch-bound local extension. Neither cloud48 nor pocket384 changes. */
typedef struct {
    uint32_t magic, version;
    uint64_t epoch;
    h2h_save_t cloud_base;
    stayc_game_save_t gameplay;
    uint32_t settled_claims, checksum;
} stayc_game_record_t;
typedef struct {
    stayc_game_record_t persisted;
    bool healthy, blocked, has_record;
    uint32_t settled_claims;
} stayc_game_store_t;
typedef enum { STAYC_GAME_READ_MISSING, STAYC_GAME_READ_OK, STAYC_GAME_READ_ERROR } stayc_game_read_t;
typedef bool (*stayc_game_write_t)(const void *bytes,size_t size,void *context);
#define STAYC_GAME_RECORD_BYTES 164u
void stayc_game_record_encode(const stayc_game_record_t *record,uint8_t bytes[STAYC_GAME_RECORD_BYTES]);
bool stayc_game_record_decode(stayc_game_record_t *record,const void *bytes,size_t size);

/* Base restores preserve local wallet/ownership/records; infeasible claims are
 * reconciled against restored progress, with paid tombstones retained. They do not claim cloud V4 support. */
void stayc_game_project(const stayc_game_t *m,h2h_save_t *base);
/* Project an already snapshotted V4 save without a temporary gameplay model. */
bool stayc_game_project_save(const stayc_game_save_t *save,h2h_save_t *base);
bool stayc_game_import_base(stayc_game_t *m,const h2h_save_t *base,bool preserve_extension);
void stayc_game_store_load(stayc_game_store_t *store,stayc_game_t *m,uint64_t epoch,
                           const h2h_save_t *base,stayc_game_read_t result,const void *bytes,size_t size);
bool stayc_game_store_write(stayc_game_store_t *store,const stayc_game_save_t *save,uint64_t epoch,
                            const h2h_save_t *cloud_base,stayc_game_write_t write,void *context);
