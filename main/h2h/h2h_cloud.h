#pragma once
#include "esp_err.h"
#include "h2h_pocket.h"
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
void h2h_cloud_status(h2h_cloud_status_t *out);
bool h2h_cloud_model(h2h_save_t *save, uint64_t *epoch, uint64_t *generation);
bool h2h_cloud_local_save(uint64_t epoch, uint64_t generation, const h2h_save_t *save);
bool h2h_cloud_can_play(void);
bool h2h_cloud_can_play_at(uint64_t epoch, uint64_t generation);
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
