#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POCKET_PROFILE_BYTES 8192u
#define POCKET_PROFILE_CHUNK 512u
#define POCKET_PROFILE_BANK 16384u
#define POCKET_PROFILE_HEADER 256u
#define POCKET_PROFILE_PARTITION 32768u
#define POCKET_PROFILE_PROTOCOL "pocket-profile-v1"

typedef struct {
    uint64_t epoch;
    uint32_t account,points;
    bool points_available;
    char username[64],uid[32],sha256[65];
} pocket_profile_meta_t;
typedef struct {
    bool (*read)(uint32_t,void *,size_t,void *);
    bool (*write)(uint32_t,const void *,size_t,void *);
    bool (*erase)(uint32_t,size_t,void *);
    bool (*sha)(uint32_t,size_t,uint8_t[32],void *);
    void *context;
} pocket_profile_io_t;
typedef struct {
    pocket_profile_io_t io;
    pocket_profile_meta_t current,pending;
    uint64_t revision,pending_revision;
    uint32_t next;
    int bank,pending_bank;
    bool enabled,uncertain;
} pocket_profile_t;
typedef struct {
    pocket_profile_meta_t meta;
    uint64_t revision;
    bool enabled,available;
    uint8_t pixels[POCKET_PROFILE_BYTES];
} pocket_profile_view_t;

bool pocket_profile_meta_valid(const pocket_profile_meta_t *meta);
bool pocket_profile_open(pocket_profile_t *store,const pocket_profile_io_t *io);
bool pocket_profile_owned(const pocket_profile_t *store,uint32_t account,uint64_t epoch);
/* Begin returns committed equality without writes, resumes an identical upload,
 * or prepares only the inactive bank. Callers fence current owner and epoch. */
bool pocket_profile_begin(pocket_profile_t *,const pocket_profile_meta_t *);
bool pocket_profile_chunk(pocket_profile_t *,const char *sha,uint32_t offset,const uint8_t *,size_t);
bool pocket_profile_commit(pocket_profile_t *,const char *sha);
bool pocket_profile_pixels(pocket_profile_t *,uint8_t[POCKET_PROFILE_BYTES]);
