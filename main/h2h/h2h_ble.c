#include "h2h_ble.h"
#include "h2h_relay_transport.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "h2h_ble";
/* IDF's maintained bleprph example declares this store initializer publicly
 * at the application boundary; its config header omits the declaration. */
void ble_store_config_init(void);
/* d6b7513e-5876-48a8-a9c5-82e89f59aa00 and control/status +1/+2. */
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(0x00,0xaa,0x59,0x9f,0xe8,0x82,0xc5,0xa9,0xa8,0x48,0x76,0x58,0x3e,0x51,0xb7,0xd6);
static const ble_uuid128_t control_uuid = BLE_UUID128_INIT(0x01,0xaa,0x59,0x9f,0xe8,0x82,0xc5,0xa9,0xa8,0x48,0x76,0x58,0x3e,0x51,0xb7,0xd6);
static const ble_uuid128_t relay_uuid = BLE_UUID128_INIT(0x03,0xaa,0x59,0x9f,0xe8,0x82,0xc5,0xa9,0xa8,0x48,0x76,0x58,0x3e,0x51,0xb7,0xd6);
static const ble_uuid128_t status_uuid = BLE_UUID128_INIT(0x02,0xaa,0x59,0x9f,0xe8,0x82,0xc5,0xa9,0xa8,0x48,0x76,0x58,0x3e,0x51,0xb7,0xd6);

typedef struct {
    uint8_t ssid_length, password_length, ssid_received, password_received;
    h2h_ble_command_t command;
} provisioning_t;

static QueueHandle_t commands;
static SemaphoreHandle_t host_stopped, stop_done;
static TaskHandle_t host_handle, stop_handle;
static bool stopping, stop_finished;
static int stop_error;
static void heap_checkpoint(const char *phase) {
    const unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA;
    ESP_LOGI(TAG,"%s heap8_free=%u heap8_min=%u heap8_largest=%u dma_free=%u dma_min=%u dma_largest=%u",
        phase,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(caps),(unsigned)heap_caps_get_minimum_free_size(caps),
        (unsigned)heap_caps_get_largest_free_block(caps));
}
static provisioning_t staging;
static h2h_relay_transport_t relay_transport;
static SemaphoreHandle_t relay_lock;
static volatile bool relay_disconnected;
static uint32_t relay_connection_generation=1;
static void clear_relay(void) {
    uint32_t old=__atomic_load_n(&relay_connection_generation,__ATOMIC_RELAXED);
    while(old&&!__atomic_compare_exchange_n(&relay_connection_generation,&old,old==UINT32_MAX?0:old+1,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED)){}
    relay_disconnected=true;
    if(relay_lock && xSemaphoreTake(relay_lock,0)==pdTRUE) {
        h2h_relay_transport_reset(&relay_transport);relay_disconnected=true;
        xSemaphoreGive(relay_lock);
    }
}
static char identity[33];
static uint8_t identity_bytes[16];
static char name[20];
static uint8_t addr_type;
static volatile bool connected, authenticated, passkey_ready, host_running, host_initialized;
static volatile uint32_t passkey;

static void advertise(void);
static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            connected = true;
            authenticated = false;
            passkey_ready = false;
            memset(&staging, 0, sizeof(staging));
            int rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0) ESP_LOGW(TAG, "security initiation failed: %d", rc);
        } else advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        connected = false;
        authenticated = false;
        passkey_ready = false;
        clear_relay();
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
        {
            struct ble_gap_conn_desc security;
            authenticated = event->enc_change.status == 0 &&
                ble_gap_conn_find(event->enc_change.conn_handle, &security) == 0 &&
                security.sec_state.encrypted && security.sec_state.authenticated;
        }
        passkey_ready = false;
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!connected) advertise();
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Do not silently replace an existing authenticated peer record.
         * Normal bonded reconnects re-encrypt and do not request pairing. */
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default: break;
    }
    return 0;
}

static void advertise(void) {
    if (!host_initialized || stopping) return;
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    /* A 128-bit UUID + flags fits the 31-byte primary advertisement.
     * The complete RDP name belongs in the scan response (combined >31). */
    fields.uuids128=&service_uuid; fields.num_uuids128=1; fields.uuids128_is_complete=1;
    int rc=ble_gap_adv_set_fields(&fields);
    struct ble_hs_adv_fields response={.name=(uint8_t *)name,.name_len=strlen(name),.name_is_complete=1};
    if(!rc) rc=ble_gap_adv_rsp_set_fields(&response);
    if(rc) { ESP_LOGE(TAG,"Advertising fields failed: %d",rc); return; }
    struct ble_gap_adv_params params = {.conn_mode = BLE_GAP_CONN_MODE_UND,
                                        .disc_mode = BLE_GAP_DISC_MODE_GEN};
    rc=ble_gap_adv_start(addr_type,NULL,BLE_HS_FOREVER,&params,gap_event,NULL);
    if(rc) ESP_LOGE(TAG,"Advertising start failed: %d",rc);
    else ESP_LOGI(TAG,"Advertising started: service d6b7513e-5876-48a8-a9c5-82e89f59aa00 name %s",name);
}

static void on_sync(void) {
    if (ble_hs_id_infer_auto(0, &addr_type) == 0) advertise();
}

static void on_reset(int reason) { ESP_LOGW(TAG, "NimBLE reset: %d", reason); }

static int access_characteristic(uint16_t connection, uint16_t attr,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr; (void)arg;
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(connection, &desc) != 0 || !desc.sec_state.encrypted ||
        !desc.sec_state.authenticated) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if(ble_uuid_cmp(ctxt->chr->uuid,&relay_uuid.u)==0 && ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR) {
        if(ctxt->op!=BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
        uint8_t frame[20];
        if(!relay_lock||xSemaphoreTake(relay_lock,0)!=pdTRUE)return BLE_ATT_ERR_INSUFFICIENT_RES;
        size_t n=h2h_relay_transport_read(&relay_transport,frame);xSemaphoreGive(relay_lock);
        return os_mbuf_append(ctxt->om,frame,n)==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ble_uuid_cmp(ctxt->chr->uuid, &status_uuid.u) == 0) {
        if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
        return os_mbuf_append(ctxt->om, identity_bytes, sizeof(identity_bytes)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if ((ble_uuid_cmp(ctxt->chr->uuid, &control_uuid.u) != 0 && ble_uuid_cmp(ctxt->chr->uuid,&relay_uuid.u)!=0) ||
        ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
    if (length < 1 || length > 20) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint8_t frame[20];
    if (ble_hs_mbuf_to_flat(ctxt->om, frame, sizeof(frame), NULL) != 0) return BLE_ATT_ERR_UNLIKELY;
    if(frame[0]>=0x10 && frame[0]<=0x14) {
        if(!relay_lock||xSemaphoreTake(relay_lock,0)!=pdTRUE)return BLE_ATT_ERR_INSUFFICIENT_RES;
        bool ok=h2h_relay_transport_write(&relay_transport,frame,length);xSemaphoreGive(relay_lock);
        return ok?0:BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
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
    }, {
        .uuid = &relay_uuid.u, .access_cb = access_characteristic,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN,
    }, {0}},
}, {0}};

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    if (host_stopped) xSemaphoreGive(host_stopped);
    for(;;) vTaskSuspend(NULL);
}
static void stop_task(void *arg) {
    (void)arg;
    stop_error=nimble_port_stop();
    xSemaphoreGive(stop_done);
    for(;;) vTaskSuspend(NULL);
}

esp_err_t h2h_ble_start(const char device_id[33]) {
    if(host_initialized || stopping) return ESP_ERR_INVALID_STATE;
    if(!device_id || strlen(device_id)!=32) return ESP_ERR_INVALID_ARG;
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
    relay_lock=xSemaphoreCreateMutex();h2h_relay_transport_reset(&relay_transport);
    host_stopped = xSemaphoreCreateBinary();
    stop_done = xSemaphoreCreateBinary();
    if (!commands || !host_stopped || !stop_done || !relay_lock) { h2h_ble_stop(); return ESP_ERR_NO_MEM; }
    heap_checkpoint("before_controller");
    esp_err_t error = nimble_port_init();
    heap_checkpoint("after_controller");
    if (error != ESP_OK) { h2h_ble_stop(); return error; }
    host_initialized = true;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
    ble_store_config_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set(name) != 0 || ble_gatts_count_cfg(services) != 0 ||
        ble_gatts_add_svcs(services) != 0) { h2h_ble_stop(); return ESP_FAIL; }
    /* IDF 5.5.3 esp_nimble_enable discards xTaskCreate failure. Own the
     * handle and keep the configured stack, priority and affinity unchanged. */
    if(xTaskCreatePinnedToCore(host_task,"nimble_host",CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,
        NULL,configMAX_PRIORITIES-4,&host_handle,CONFIG_BT_NIMBLE_PINNED_TO_CORE)!=pdPASS) {
        h2h_ble_stop(); return ESP_ERR_NO_MEM;
    }
    host_running = true;
    heap_checkpoint("host_started");
    return ESP_OK;
}

esp_err_t h2h_ble_stop(void) {
    if(host_running) {
        if(!stopping) {
            stopping=true;
            (void)ble_gap_adv_stop();
            if(xTaskCreate(stop_task,"nimble_stop",3072,NULL,5,&stop_handle)!=pdPASS) {
                stopping=false; stop_handle=NULL; return ESP_ERR_NO_MEM;
            }
        }
        if(!stop_finished) {
            if(xSemaphoreTake(stop_done,pdMS_TO_TICKS(1000))!=pdTRUE) return ESP_ERR_TIMEOUT;
            stop_finished=true;
        }
        if(stop_error!=0) return ESP_FAIL; /* Retain live host ownership. */
        if(xSemaphoreTake(host_stopped,pdMS_TO_TICKS(1000))!=pdTRUE) return ESP_ERR_TIMEOUT;
        vTaskDelete(host_handle); host_handle=NULL;
        vTaskDelete(stop_handle); stop_handle=NULL;
        host_running=false;
    }
    if(host_initialized) {
        esp_err_t e=nimble_port_deinit();
        if(e!=ESP_OK) return e;
        host_initialized=false;
    }
    if(host_stopped) { vSemaphoreDelete(host_stopped); host_stopped=NULL; }
    if(stop_done) { vSemaphoreDelete(stop_done); stop_done=NULL; }
    if(commands) { vQueueDelete(commands); commands=NULL; }
    clear_relay();
    if(relay_lock) { vSemaphoreDelete(relay_lock); relay_lock=NULL; }
    stopping=stop_finished=false; connected=passkey_ready=false;
    memset(&staging,0,sizeof(staging));
    return ESP_OK;
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
bool h2h_ble_authenticated_for_connection(uint32_t generation) {
    return generation && connected && authenticated &&
        h2h_ble_relay_connection_generation() == generation;
}
uint32_t h2h_ble_relay_connection_generation(void){return __atomic_load_n(&relay_connection_generation,__ATOMIC_ACQUIRE);}

char *h2h_ble_relay_take(uint32_t *generation,size_t *length) {
    if(!generation||!length||!relay_lock||xSemaphoreTake(relay_lock,0)!=pdTRUE)return NULL;
    char *bytes=h2h_relay_transport_take(&relay_transport,generation);if(bytes)*length=relay_transport.total;
    xSemaphoreGive(relay_lock);return bytes;
}
void h2h_ble_relay_finish(uint32_t generation,size_t length) {
    if(relay_lock&&xSemaphoreTake(relay_lock,portMAX_DELAY)==pdTRUE) {
        (void)h2h_relay_transport_finish(&relay_transport,generation,length);xSemaphoreGive(relay_lock);
    }
}
bool h2h_ble_relay_disconnected(void) {
    if(!relay_lock||xSemaphoreTake(relay_lock,0)!=pdTRUE)return false;
    bool value=relay_disconnected;relay_disconnected=false;xSemaphoreGive(relay_lock);return value;
}
