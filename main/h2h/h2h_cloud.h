#pragma once
#include "esp_err.h"
#include "h2h_theme.h"
#include <stdbool.h>

typedef enum {
    H2H_CLOUD_SETUP, H2H_CLOUD_CONNECTING, H2H_CLOUD_OFFLINE,
    H2H_CLOUD_READY, H2H_CLOUD_PAIRING, H2H_CLOUD_CLAIMED,
    H2H_CLOUD_BOUND, H2H_CLOUD_WORKING, H2H_CLOUD_ERROR
} h2h_cloud_state_t;

typedef struct {
    h2h_cloud_state_t state;
    bool wifi_ready, bound;
    char device_id[33];
    char pair_url[96];
    char card_url[96];
    char claimant[48];
    char message[64];
} h2h_cloud_status_t;

esp_err_t h2h_cloud_start(void);
void h2h_cloud_status(h2h_cloud_status_t *out);
bool h2h_cloud_wifi(const char *ssid, const char *password);
bool h2h_cloud_open(void);
bool h2h_cloud_close(void);
bool h2h_cloud_confirm(void);
bool h2h_cloud_refresh(void);
bool h2h_cloud_forget_wifi(void);
/* A verified package is handed to the app loop, then acknowledged after save. */
bool h2h_cloud_poll_theme(h2h_theme_t *out);
void h2h_cloud_theme_applied(bool persisted);
