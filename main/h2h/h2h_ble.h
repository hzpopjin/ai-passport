#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum { H2H_BLE_WIFI_CREDENTIALS, H2H_BLE_THEME_REFRESH } h2h_ble_command_type_t;
typedef struct {
    h2h_ble_command_type_t type;
    char ssid[33];
    char password[65];
} h2h_ble_command_t;

/* Start only while the account page is open. Commands are queued for a worker. */
esp_err_t h2h_ble_start(const char device_id[33]);
/* A timeout retains host/controller ownership. Retry from the main task;
 * never start a second host or free resources still in use. */
esp_err_t h2h_ble_stop(void);
bool h2h_ble_poll(h2h_ble_command_t *out);
bool h2h_ble_passkey(uint32_t *out);
bool h2h_ble_connected(void);
/* Current encrypted, MITM-authenticated link only; never a phone-supplied flag. */
bool h2h_ble_authenticated_for_connection(uint32_t generation);

/* Byte-only worker lease. Disconnect invalidates the lease, never JSON/crypto in GATT. */
char *h2h_ble_relay_take(uint32_t *generation,size_t *length);
void h2h_ble_relay_finish(uint32_t generation,size_t length);
bool h2h_ble_relay_disconnected(void);
/* Stable across RPC frames; retirement invalidates every in-flight resource job. */
uint32_t h2h_ble_relay_connection_generation(void);
