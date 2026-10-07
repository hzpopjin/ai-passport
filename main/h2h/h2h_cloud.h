#pragma once
#include "esp_err.h"
#include "h2h_pocket.h"
#include "pocket_profile.h"
#include "stayc_game.h"
#include <stdbool.h>

typedef enum {
    H2H_CLOUD_SETUP, H2H_CLOUD_CONNECTING, H2H_CLOUD_OFFLINE,
    H2H_CLOUD_READY, H2H_CLOUD_PAIRING, H2H_CLOUD_CLAIMED,
    H2H_CLOUD_BOUND, H2H_CLOUD_WORKING, H2H_CLOUD_ERROR
} h2h_cloud_state_t;
typedef struct {
    h2h_cloud_state_t state;
    bool wifi_ready, bound, conflict, pending_unbind, recovery;
    uint64_t ownership_epoch, model_generation, save_revision;
    char device_id[33], pair_url[96], card_url[96], claimant[48], message[64];
} h2h_cloud_status_t;

esp_err_t h2h_cloud_start(void);
/* Volatile local-test barrier. True only after the worker finishes its current
 * operation and releases Wi-Fi. Does not change ownership or stored state. */
bool h2h_cloud_debug_pause(bool requested);
void h2h_cloud_status(h2h_cloud_status_t *out);
bool h2h_cloud_model(h2h_save_t *save, uint64_t *epoch, uint64_t *generation);
bool h2h_cloud_local_save(uint64_t epoch, uint64_t generation, const h2h_save_t *save);
/* 1=coherent current journal record,0=no full record,-1=locked/corrupt. */
int h2h_cloud_game_record(uint64_t epoch,uint8_t bytes[164]);
/* Only as a callback inside h2h_cloud_guarded_write; state mutex is held. */
bool h2h_cloud_game_record_write_locked(const void *bytes,size_t length);
void h2h_cloud_game_snapshot(uint64_t epoch,uint64_t generation,const stayc_game_save_t *game,uint32_t settled);
bool h2h_cloud_can_play(void);
bool h2h_cloud_can_play_at(uint64_t epoch, uint64_t generation);
bool h2h_cloud_guarded_write(uint64_t epoch, uint64_t generation,
    bool (*write)(const void *, size_t, void *), const void *bytes, size_t length, void *ctx);
/* These run in the app loop, not in a button callback. Pending unbind must
 * commit before success is returned; the worker performs network I/O later. */
bool h2h_cloud_unbind(uint64_t epoch, uint64_t generation);
bool h2h_cloud_restore(uint64_t epoch, uint64_t generation);
bool h2h_cloud_wifi(const char *ssid, const char *password);
bool h2h_cloud_open(void);
bool h2h_cloud_close(void);
bool h2h_cloud_confirm(void);
bool h2h_cloud_refresh(void);
bool h2h_cloud_forget_wifi(void);

/* Publish the real normal V4 wallet from the main loop, never infer it from hearts. */
void h2h_cloud_game_stars(uint64_t epoch,uint64_t generation,uint32_t stars);

/* Public account view only, copied under the owner mutex; never credentials. */
void h2h_cloud_profile(pocket_profile_view_t *out);
