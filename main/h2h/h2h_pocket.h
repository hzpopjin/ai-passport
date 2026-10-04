#pragma once
#include "h2h_model.h"
#include "h2h_ownership.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define H2H_POCKET_RECORD_SIZE 384u
#define H2H_POCKET_MAX_INTEGER UINT64_C(9007199254740991)
typedef struct {
    h2h_ownership_t ownership;
    h2h_save_t save, remote_save, recovery_save;
    uint64_t revision, remote_revision;
    bool dirty, conflict, restore_ack, recovery;
} h2h_pocket_record_t;
typedef struct {
    h2h_pocket_record_t record;
    uint64_t model_generation;
    bool healthy;
    unsigned tracks, songs;
} h2h_pocket_t;
typedef bool (*h2h_pocket_write_fn)(const unsigned char *bytes, size_t length, void *context);
typedef struct {
    uint64_t epoch, revision;
    uint32_t account_id;
    bool activated, has_save, conflict, restore_required;
    const char *unbind_ack, *unbind_rejected;
    h2h_save_t save;
} h2h_pocket_reply_t;

void h2h_pocket_init(h2h_pocket_t *p, unsigned tracks, unsigned songs);
bool h2h_pocket_decode(h2h_pocket_t *p, const void *bytes, size_t length);
void h2h_pocket_encode(const h2h_pocket_record_t *record, unsigned char out[H2H_POCKET_RECORD_SIZE]);
bool h2h_pocket_can_play(const h2h_pocket_t *p);
bool h2h_pocket_local_save(h2h_pocket_t *p, uint64_t epoch, const h2h_save_t *save, h2h_pocket_write_fn write, void *ctx);
bool h2h_pocket_unbind(h2h_pocket_t *p, const char operation[33], h2h_pocket_write_fn write, void *ctx);
/* sent is the immutable request snapshot. Current local progress may be newer. */
bool h2h_pocket_reply(h2h_pocket_t *p, const h2h_pocket_record_t *sent,
                      const h2h_pocket_reply_t *reply, h2h_pocket_write_fn write, void *ctx);
/* Explicit card action after displaying cloud/local conflict. Preserve old local save. */
bool h2h_pocket_restore_cloud(h2h_pocket_t *p, h2h_pocket_write_fn write, void *ctx);
