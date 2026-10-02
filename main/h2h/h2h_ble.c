#include "h2h_ble.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "h2h_ble";
/* d6b7513e-5876-48a8-a9c5-82e89f59aa00 and control/status +1/+2. */
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(0x00,0xaa,0x59,0x9f,0xe8,0x82,0xc5,0xa9,0xa8,0x48,0x76,0x58,0x3e,0x51,0xb7,0xd6);
static const ble_uuid128_t control_uuid = BLE_UUID128_INIT(0x01,0xaa,0x59,0x9f,0xe8,0x82,0xc5,0xa9,0xa8,0x48,0x76,0x58,0x3e,0x51,0xb7,0xd6);
static const ble_uuid128_t status_uuid = BLE_UUID128_INIT(0x02,0xaa,0x59,0x9f,0xe8,0x82,0xc5,0xa9,0xa8,0x48,0x76,0x58,0x3e,0x51,0xb7,0xd6);

typedef struct {
    uint8_t ssid_length, password_length, ssid_received, password_received;
    h2h_ble_command_t command;
} provisioning_t;

static QueueHandle_t commands;
static SemaphoreHandle_t host_stopped;
static provisioning_t staging;
static char identity[33];
static uint8_t identity_bytes[16];
static char name[20];
static uint8_t addr_type;
static volatile bool connected, passkey_ready, host_running, host_initialized;
static volatile uint32_t passkey;

static void advertise(void);
static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            connected = true;
            memset(&staging, 0, sizeof(staging));
            int rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0) ESP_LOGW(TAG, "security initiation failed: %d", rc);
        } else advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        connected = false;
        passkey_ready = false;
        memset(&staging, 0, sizeof(staging));
        advertise();
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action != BLE_SM_IOACT_DISP) return BLE_ATT_ERR_UNLIKELY;
        passkey = esp_random() % 1000000;
        passkey_ready = true;
        struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = passkey};
        return ble_sm_inject_io(event->passkey.conn_handle, &io);
    case BLE_GAP_EVENT_ENC_CHANGE:
        passkey_ready = false;
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!connected) advertise();
        break;
    default: break;
    }
    return 0;
}

static void advertise(void) {
    if (!host_initialized) return;
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) return;
    struct ble_gap_adv_params params = {.conn_mode = BLE_GAP_CONN_MODE_UND,
                                        .disc_mode = BLE_GAP_DISC_MODE_GEN};
    (void)ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}

static void on_sync(void) {
    if (ble_hs_id_infer_auto(0, &addr_type) == 0) advertise();
}

static void on_reset(int reason) { ESP_LOGW(TAG, "NimBLE reset: %d", reason); }

static int access_characteristic(uint16_t connection, uint16_t attr,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr; (void)arg;
    if (ble_uuid_cmp(ctxt->chr->uuid, &status_uuid.u) == 0) {
        if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
        return os_mbuf_append(ctxt->om, identity_bytes, sizeof(identity_bytes)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ble_uuid_cmp(ctxt->chr->uuid, &control_uuid.u) != 0 ||
        ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(connection, &desc) != 0 || !desc.sec_state.encrypted ||
        !desc.sec_state.authenticated) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
    if (length < 1 || length > 20) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint8_t frame[20];
    if (ble_hs_mbuf_to_flat(ctxt->om, frame, sizeof(frame), NULL) != 0) return BLE_ATT_ERR_UNLIKELY;
    switch (frame[0]) {
    case 0x01: /* Begin bounded Wi-Fi credential transfer. */
        if (length != 3 || frame[1] < 1 || frame[1] > 32 || frame[2] > 64) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        memset(&staging, 0, sizeof(staging));
        staging.ssid_length = frame[1];
        staging.password_length = frame[2];
        return 0;
    case 0x02: /* SSID fragment. */
        if (!staging.ssid_length || length < 2 || staging.ssid_received + length - 1 > staging.ssid_length) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        memcpy(staging.command.ssid + staging.ssid_received, frame + 1, length - 1);
        staging.ssid_received += length - 1;
        return 0;
    case 0x03: /* Password fragment. */
        if (!staging.ssid_length || length < 2 || staging.password_received + length - 1 > staging.password_length) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        memcpy(staging.command.password + staging.password_received, frame + 1, length - 1);
        staging.password_received += length - 1;
        return 0;
    case 0x04: /* Commit only a complete transfer. */
        if (length != 1 || !staging.ssid_length || staging.ssid_received != staging.ssid_length ||
            staging.password_received != staging.password_length) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        staging.command.type = H2H_BLE_WIFI_CREDENTIALS;
        if (!commands || xQueueSend(commands, &staging.command, 0) != pdTRUE) return BLE_ATT_ERR_INSUFFICIENT_RES;
        memset(&staging, 0, sizeof(staging));
        return 0;
    case 0x05: { /* Explicit manual theme refresh. */
        if (length != 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        const h2h_ble_command_t command = {.type = H2H_BLE_THEME_REFRESH};
        return commands && xQueueSend(commands, &command, 0) == pdTRUE ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    default: return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
}

static const struct ble_gatt_svc_def services[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &service_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {{
        .uuid = &control_uuid.u, .access_cb = access_characteristic,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN,
    }, {
        .uuid = &status_uuid.u, .access_cb = access_characteristic,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN,
    }, {0}},
}, {0}};

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    if (host_stopped) xSemaphoreGive(host_stopped);
    nimble_port_freertos_deinit();
}

esp_err_t h2h_ble_start(const char device_id[33]) {
    if (host_initialized || !device_id || strlen(device_id) != 32) return ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < 16; ++i) {
        char high = device_id[i * 2], low = device_id[i * 2 + 1];
        if (!((high >= '0' && high <= '9') || (high >= 'a' && high <= 'f')) ||
            !((low >= '0' && low <= '9') || (low >= 'a' && low <= 'f'))) return ESP_ERR_INVALID_ARG;
        identity_bytes[i] = (uint8_t)(((high <= '9' ? high - '0' : high - 'a' + 10) << 4) |
                                       (low <= '9' ? low - '0' : low - 'a' + 10));
    }
    memcpy(identity, device_id, 33);
    snprintf(name, sizeof(name), "RDP-%.*s", 8, identity + 24);
    commands = xQueueCreate(4, sizeof(h2h_ble_command_t));
    host_stopped = xSemaphoreCreateBinary();
    if (!commands || !host_stopped) { h2h_ble_stop(); return ESP_ERR_NO_MEM; }
    esp_err_t error = nimble_port_init();
    if (error != ESP_OK) { h2h_ble_stop(); return error; }
    host_initialized = true;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_bonding = 0;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set(name) != 0 || ble_gatts_count_cfg(services) != 0 ||
        ble_gatts_add_svcs(services) != 0) { h2h_ble_stop(); return ESP_FAIL; }
    error = esp_nimble_enable(host_task);
    if (error != ESP_OK) { h2h_ble_stop(); return error; }
    host_running = true;
    return ESP_OK;
}

void h2h_ble_stop(void) {
    if (host_running) {
        (void)ble_gap_adv_stop();
        if (nimble_port_stop() == 0 && host_stopped) (void)xSemaphoreTake(host_stopped, pdMS_TO_TICKS(3000));
        host_running = false;
    }
    if (host_initialized) { (void)nimble_port_deinit(); host_initialized = false; }
    if (host_stopped) { vSemaphoreDelete(host_stopped); host_stopped = NULL; }
    if (commands) { vQueueDelete(commands); commands = NULL; }
    connected = passkey_ready = false;
    memset(&staging, 0, sizeof(staging));
}

bool h2h_ble_poll(h2h_ble_command_t *out) {
    return out && commands && xQueueReceive(commands, out, 0) == pdTRUE;
}
bool h2h_ble_passkey(uint32_t *out) {
    if (!out || !passkey_ready) return false;
    *out = passkey;
    return true;
}
bool h2h_ble_connected(void) { return connected; }
