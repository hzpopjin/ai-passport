#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* No callback reads an existing secret. key_size exposes metadata only.
 * insert performs exactly set_blob(h2h_cloud,relay_key,32)+commit, no erase. */
typedef struct {
    int (*key_size)(void *ctx); /* -1 absent, -2 storage error, >=0 present */
    bool (*insert)(const uint8_t key[32], void *ctx);
    void *ctx;
} pe_store_t;
typedef enum { PE_READY, PE_ARMED, PE_CONFIRMED, PE_WRITTEN, PE_UNCERTAIN, PE_REFUSED } pe_phase_t;
typedef struct {
    char device[33], mac[13], image[65], nonce[33], operation[33];
    pe_store_t store;
    pe_phase_t phase;
    int64_t armed_us, expires_us, last_us, pressed_us, last_sampled_us, last_changed_us;
    uint64_t sequence;
    int last_key;
    bool released;
    const char *error;
} pe_state_t;
bool pe_hex(const char *text, size_t length);
bool pe_init(pe_state_t *, const char *, const char *, const char *, const char *, pe_store_t);
bool pe_arm(pe_state_t *, const char *, const char *, const char *, const char *, int64_t);
void pe_sample(pe_state_t *, int, int64_t, int64_t, uint64_t, int64_t);
bool pe_install(pe_state_t *, const char *, uint8_t[32], int64_t);
const char *pe_phase_name(pe_phase_t);
void pe_zero(void *, size_t);
