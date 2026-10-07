#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POCKET_LOG_BYTES 131072u
#define POCKET_LOG_SECTOR_BYTES 4096u
#define POCKET_LOG_SECTORS 32u
#define POCKET_LOG_RECORD_BYTES 64u
#define POCKET_LOG_SLOTS 63u
enum { POCKET_LOG_BOOT=1, POCKET_LOG_CHECKPOINT, POCKET_LOG_ERROR };
enum { POCKET_LOG_ERROR_NONE, POCKET_LOG_ERROR_STORAGE, POCKET_LOG_ERROR_INPUT,
       POCKET_LOG_ERROR_DISPLAY, POCKET_LOG_ERROR_RESOURCE, POCKET_LOG_ERROR_AUDIO,
       POCKET_LOG_ERROR_SYNC, POCKET_LOG_ERROR_INTERNAL };

/* Fixed public fields only: no strings, identifiers, tokens, opaque payloads,
 * save bytes, radio addresses, PINs or key material. Metrics are quantized. */
typedef struct {
    uint32_t uptime_seconds;
    uint8_t firmware_elf_prefix[8];
    uint8_t reset_reason,page,game,key,event,cloud_state,result,error;
    uint16_t heap_kib,minimum_kib,largest_kib,main_stack_64b,log_stack_64b,errors;
} pocket_log_snapshot_t;
typedef struct {
    uint64_t sequence;
    uint32_t boot;
    uint8_t kind;
    pocket_log_snapshot_t snapshot;
} pocket_log_record_t;
typedef struct {
    bool (*read)(uint32_t offset,void *bytes,size_t length,void *context);
    bool (*write)(uint32_t offset,const void *bytes,size_t length,void *context);
    bool (*erase)(uint32_t offset,size_t length,void *context);
    void *context;
    uint32_t bytes;
} pocket_log_io_t;
typedef struct {
    pocket_log_io_t io;
    uint64_t sector_sequence,last_sequence;
    pocket_log_record_t last;
    int active;
    unsigned slot;
    bool healthy,blocked,has_last;
} pocket_log_ring_t;

bool pocket_log_snapshot_valid(const pocket_log_snapshot_t *snapshot);
bool pocket_log_record_encode(const pocket_log_record_t *record,uint8_t bytes[64]);
bool pocket_log_record_decode(pocket_log_record_t *record,const uint8_t bytes[64]);
/* Open scans only. No erase/write occurs until an explicit append. */
bool pocket_log_ring_open(pocket_log_ring_t *ring,const pocket_log_io_t *io);
/* Unknown/torn write outcomes disable this instance; recovery needs a fresh
 * open. Never retry programming the same partially written record. */
bool pocket_log_ring_append(pocket_log_ring_t *ring,uint8_t kind,uint32_t boot,
                            const pocket_log_snapshot_t *snapshot);
typedef bool (*pocket_log_visit_fn)(const pocket_log_record_t *record,void *context);
/* Ordered, validated public records only; no raw sector export. */
bool pocket_log_ring_visit(const pocket_log_ring_t *ring,pocket_log_visit_fn visit,void *context);
