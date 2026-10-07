#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define H2H_RESOURCE_SLOT_BYTES 262144u
#define H2H_RESOURCE_PAYLOAD_OFFSET 4096u
#define H2H_RESOURCE_BYTES (512u+15u*15360u+4u*768u)
#define H2H_RESOURCE_STATE_BYTES 1536u
#define H2H_RESOURCE_FIRMWARE 1u
#define H2H_RESOURCE_HIGHWATERS 16u
#define H2H_RESOURCE_RESUME_BYTES 160u
typedef struct {
    char id[33], key_id[33];
    uint32_t kind, version, min_firmware, max_firmware;
    unsigned char sha256[32], signature[64];
} h2h_resource_manifest_t;
typedef struct {
    uint64_t epoch;
    uint32_t revision,generation,validated_offset;
    int slot;
    unsigned char sha256[32],manifest_sha256[32],prefix_sha256[32];
} h2h_resource_resume_t;
typedef struct { char id[33]; uint32_t version; unsigned char sha256[32]; } h2h_resource_highwater_t;
typedef struct {
    int active, previous;
    uint64_t epoch;
    uint32_t revision, generation, highwater_count;
    h2h_resource_highwater_t highwaters[H2H_RESOURCE_HIGHWATERS];
} h2h_resource_record_t;
typedef struct {
    h2h_resource_record_t record;
    h2h_resource_manifest_t staged;
    int staged_slot;
    uint64_t staged_epoch;
    uint32_t staged_revision;
    bool pending;
    h2h_resource_resume_t resume;
    uint32_t cursor;
    bool transferring, restart_required;
} h2h_resource_t;
typedef struct {
    size_t capacity[2];
    bool (*key)(const char *id, unsigned char out[32], void *ctx);
    bool (*read)(int slot, size_t offset, void *out, size_t length, void *ctx);
    bool (*write)(int slot, size_t offset, const void *bytes, size_t length, void *ctx);
    bool (*erase)(int slot, void *ctx);
    bool (*hash)(int slot, size_t offset, size_t length, unsigned char out[32], void *ctx);
    bool (*fetch)(size_t offset, void *out, size_t length, void *ctx);
    bool (*alive)(void *ctx);
    bool (*commit)(const void *bytes, size_t length, void *ctx);
    void *ctx;
    bool (*digest)(const void *bytes,size_t length,unsigned char out[32],void *ctx);
    bool (*erase_range)(int slot,size_t offset,size_t length,void *ctx);
    int (*read_resume)(void *bytes,size_t length,void *ctx);
    bool (*commit_resume)(const void *bytes,size_t length,void *ctx);
} h2h_resource_io_t;
typedef struct { char id[33], group[33], member[33], title[65]; uint32_t room_colors[3]; } h2h_resource_labels_t;
void h2h_resource_init(h2h_resource_t *r);
void h2h_resource_encode(const h2h_resource_record_t *record, unsigned char out[H2H_RESOURCE_STATE_BYTES]);
bool h2h_resource_decode(h2h_resource_t *r, const void *bytes, size_t length);
bool h2h_resource_manifest_valid(const h2h_resource_manifest_t *m, const h2h_resource_io_t *io);
/* Manifest digest is SHA256 of the exact existing compact signature message. */
size_t h2h_resource_manifest_message(const h2h_resource_manifest_t *m,char out[512]);
void h2h_resource_resume_encode(const h2h_resource_resume_t *r,unsigned char out[160]);
bool h2h_resource_resume_decode(h2h_resource_resume_t *r,const void *bytes,size_t length);
/* Restart must be explicit on scope/hash mismatch; no active bank is written. */
bool h2h_resource_transfer_begin(h2h_resource_t *r,const h2h_resource_manifest_t *m,uint64_t epoch,
                                 uint32_t revision,bool restart,const h2h_resource_io_t *io);
bool h2h_resource_transfer_chunk(h2h_resource_t *r,uint64_t epoch,uint32_t revision,const unsigned char sha[32],
                                 uint32_t offset,const void *bytes,size_t length,const unsigned char chunk_sha[32],
                                 const h2h_resource_io_t *io);
bool h2h_resource_transfer_finish(h2h_resource_t *r,const h2h_resource_io_t *io);
bool h2h_resource_labels(const void *header, size_t length, const h2h_resource_manifest_t *m, h2h_resource_labels_t *out);
bool h2h_resource_slot_valid(int slot, const h2h_resource_io_t *io, h2h_resource_manifest_t *out);
/* Stage only in the inactive slot. A failed/interrupted download safely restarts at zero. */
bool h2h_resource_stage(h2h_resource_t *r, const h2h_resource_manifest_t *m, uint64_t epoch, uint32_t revision, const h2h_resource_io_t *io);
/* Caller serializes with renderer and guards epoch/model generation during commit. */
bool h2h_resource_publish(h2h_resource_t *r, uint64_t epoch, const h2h_resource_io_t *io);
bool h2h_resource_builtin(h2h_resource_t *r, uint64_t epoch, uint32_t revision, const h2h_resource_io_t *io);
bool h2h_resource_recover(h2h_resource_t *r, uint64_t epoch, const h2h_resource_io_t *io);
bool h2h_resource_rollback(h2h_resource_t *r, uint64_t epoch, const h2h_resource_io_t *io);
int h2h_resource_active(const h2h_resource_t *r, uint64_t epoch);
