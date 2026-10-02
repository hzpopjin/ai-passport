#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { H2H_BLE_WIFI_CREDENTIALS, H2H_BLE_THEME_REFRESH } h2h_ble_command_type_t;
typedef struct {
    h2h_ble_command_type_t type;
    char ssid[33];
    char password[65];
} h2h_ble_command_t;

/* Start only while the account page is open. Commands are queued for a worker. */
esp_err_t h2h_ble_start(const char device_id[33]);
void h2h_ble_stop(void);
bool h2h_ble_poll(h2h_ble_command_t *out);
bool h2h_ble_passkey(uint32_t *out);
bool h2h_ble_connected(void);
