#include "h2h_cloud.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "h2h_tracks.h"
#include <math.h>
#include <inttypes.h>
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define API "https://apps.randomdance.cn/api/v1/passport"
#define IP_READY BIT0
#define REPLY_MAX 2048
static const char *TAG = "h2h_cloud";
typedef enum { CMD_OPEN, CMD_CLOSE, CMD_WIFI, CMD_CONFIRM, CMD_REFRESH, CMD_FORGET } command_type_t;
typedef struct { command_type_t type; char ssid[33], password[65], code[33]; uint64_t epoch; uint32_t account; } command_t;
typedef struct { unsigned char body[REPLY_MAX]; size_t length; bool overflow; } reply_t;
static QueueHandle_t queue;
static h2h_pocket_t pocket;
static uint64_t pair_epoch;
static uint32_t pair_account, pair_started;
static SemaphoreHandle_t state_lock;
static EventGroupHandle_t events;
static h2h_cloud_status_t state;
static char secret[65], ssid_saved[33], password_saved[65], pairing_id[33];
static bool wifi_started, account_open;
static volatile bool allow_reconnect;
static volatile unsigned reconnect_attempts;

static void update(h2h_cloud_state_t mode, const char *message) {
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        state.state = mode;
        snprintf(state.message, sizeof(state.message), "%s", message);
        xSemaphoreGive(state_lock);
    }
}
void h2h_cloud_status(h2h_cloud_status_t *out) {
    if (!out || !state_lock) return;
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        *out = state;
        out->bound=h2h_pocket_can_play(&pocket);
        out->conflict=pocket.record.conflict;
        out->pending_unbind=pocket.record.ownership.pending_unbind[0]!=0;
        out->recovery=pocket.record.recovery;
        out->ownership_epoch=pocket.record.ownership.epoch;
        out->model_generation=pocket.model_generation;
        out->save_revision=pocket.record.revision;
        out->wifi_ready = events && (xEventGroupGetBits(events) & IP_READY) != 0;
        xSemaphoreGive(state_lock);
    }
}
static void hex_random(char *out, size_t bytes) {
    static const char digits[] = "0123456789abcdef";
    uint8_t buffer[32];
    esp_fill_random(buffer, bytes);
    for (size_t i = 0; i < bytes; ++i) { out[2*i] = digits[buffer[i] >> 4]; out[2*i+1] = digits[buffer[i] & 15]; }
    out[2*bytes] = 0;
    memset(buffer, 0, sizeof(buffer));
}
static bool load_identity(void) {
    nvs_handle_t handle;
    if (nvs_open("h2h_cloud", NVS_READWRITE, &handle) != ESP_OK) return false;
    size_t id_size = sizeof(state.device_id), secret_size = sizeof(secret);
    esp_err_t id_error = nvs_get_str(handle, "id", state.device_id, &id_size);
    esp_err_t secret_error = nvs_get_str(handle, "secret", secret, &secret_size);
    if (id_error == ESP_ERR_NVS_NOT_FOUND && secret_error == ESP_ERR_NVS_NOT_FOUND) {
        hex_random(state.device_id, 16); hex_random(secret, 32);
        esp_err_t e = nvs_set_str(handle, "id", state.device_id);
        if (e == ESP_OK) e = nvs_set_str(handle, "secret", secret);
        if (e == ESP_OK) e = nvs_commit(handle);
        id_error = secret_error = e;
    }
    size_t ssid_size = sizeof(ssid_saved), pass_size = sizeof(password_saved);
    (void)nvs_get_str(handle, "ssid", ssid_saved, &ssid_size);
    (void)nvs_get_str(handle, "wifi_pass", password_saved, &pass_size);
    nvs_close(handle);
    return id_error == ESP_OK && secret_error == ESP_OK &&
           strlen(state.device_id) == 32 && strlen(secret) == 64;
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) (void)esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, IP_READY);
        if (allow_reconnect && reconnect_attempts++ < 5) (void)esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        reconnect_attempts = 0;
        xEventGroupSetBits(events, IP_READY);
    }
}
static bool wifi_init(void) {
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) return false;
    if (!esp_netif_create_default_wifi_sta()) return false;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&config) != ESP_OK) return false;
    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL) != ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
    return true;
}
static bool wifi_connect(const char *ssid, const char *password) {
    if (!ssid[0] || strlen(ssid) > 32 || strlen(password) > 64) return false;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    xEventGroupClearBits(events, IP_READY);
    allow_reconnect = false;
    if (wifi_started) (void)esp_wifi_disconnect();
    if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK) return false;
    reconnect_attempts = 0;
    allow_reconnect = true;
    if (!wifi_started) {
        if (esp_wifi_start() != ESP_OK) return false;
        wifi_started = true;
    } else (void)esp_wifi_connect();
    update(H2H_CLOUD_CONNECTING, "Connecting Wi-Fi");
    bool ready = (xEventGroupWaitBits(events, IP_READY, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000)) & IP_READY) != 0;
    if (!ready) { update(H2H_CLOUD_OFFLINE, "Wi-Fi unavailable"); return false; }
    if (xSemaphoreTake(state_lock, portMAX_DELAY) == pdTRUE) {
        state.wifi_ready = true;
        xSemaphoreGive(state_lock);
    }
    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
    }
    for (int i = 0; i < 20 && time(NULL) < 1700000000; ++i) vTaskDelay(pdMS_TO_TICKS(500));
    if (time(NULL) < 1700000000) { update(H2H_CLOUD_OFFLINE, "Time sync failed"); return false; }
    update(H2H_CLOUD_READY, "Online");
    return true;
}
static bool store_wifi(const char *ssid, const char *password) {
    nvs_handle_t handle;
    if (nvs_open("h2h_cloud", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t e = nvs_set_str(handle, "ssid", ssid);
    if (e == ESP_OK) e = nvs_set_str(handle, "wifi_pass", password);
    if (e == ESP_OK) e = nvs_commit(handle);
    nvs_close(handle);
    return e == ESP_OK;
}
static esp_err_t http_event(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_DATA) {
        reply_t *reply = event->user_data;
        if (event->data_len < 0 || (size_t)event->data_len > REPLY_MAX - reply->length) reply->overflow = true;
        else if (!reply->overflow) {
            memcpy(reply->body + reply->length, event->data, event->data_len);
            reply->length += event->data_len;
        }
    }
    return ESP_OK;
}
static bool request(const char *path, esp_http_client_method_t method, const char *payload,
                    bool auth, reply_t *reply) {
    char url[192];
    snprintf(url, sizeof(url), API "%s", path);
    esp_http_client_config_t config = {
        .url = url, .method = method, .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach, .event_handler = http_event,
        .user_data = reply, .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    if (auth) {
        (void)esp_http_client_set_header(client, "X-Passport-Device", state.device_id);
        (void)esp_http_client_set_header(client, "X-Passport-Token", secret);
    }
    if (payload) {
        (void)esp_http_client_set_header(client, "Content-Type", "application/json");
        (void)esp_http_client_set_post_field(client, payload, strlen(payload));
    }
    memset(reply, 0, sizeof(*reply));
    esp_err_t result = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (result != ESP_OK || status != 200 || reply->overflow) {
        ESP_LOGW(TAG, "API request failed: %s HTTP %d", esp_err_to_name(result), status);
        return false;
    }
    return true;
}
static cJSON *response_data(const reply_t *reply) {
    cJSON *root = cJSON_ParseWithLength((const char *)reply->body, reply->length);
    if (!root) return NULL;
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsNumber(code) || code->valueint != 0 || !cJSON_IsObject(data)) { cJSON_Delete(root); return NULL; }
    cJSON *copy = cJSON_Duplicate(data, 1);
    cJSON_Delete(root);
    return copy;
}
static bool field(const cJSON *object, const char *key, char *out, size_t size) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring) >= size) return false;
    strcpy(out, value->valuestring);
    return true;
}
static bool api_json(const char *path, esp_http_client_method_t method, const char *payload,
                     bool auth, cJSON **out) {
    reply_t reply;
    if (!request(path, method, payload, auth, &reply)) return false;
    *out = response_data(&reply);
    return *out != NULL;
}
static bool write_record(const unsigned char *bytes, size_t length, void *context) {
    (void)context;
    nvs_handle_t handle;
    if(nvs_open("h2h_cloud",NVS_READWRITE,&handle)!=ESP_OK) return false;
    esp_err_t error=nvs_set_blob(handle,"pocket_v2",bytes,length);
    if(error==ESP_OK) error=nvs_commit(handle);
    nvs_close(handle); return error==ESP_OK;
}
static bool number(const cJSON *object, const char *key, uint64_t *out) {
    const cJSON *value=cJSON_GetObjectItemCaseSensitive(object,key);
    if(!cJSON_IsNumber(value)||!isfinite(value->valuedouble)||value->valuedouble<0||
       value->valuedouble>(double)H2H_POCKET_MAX_INTEGER||floor(value->valuedouble)!=value->valuedouble) return false;
    *out=(uint64_t)value->valuedouble; return true;
}
static void digest(const h2h_save_t *save, char out[65]) {
    unsigned char bytes[32]; mbedtls_sha256((const unsigned char *)save,sizeof(*save),bytes,0);
    for(unsigned i=0;i<32;i++) snprintf(out+2*i,3,"%02x",bytes[i]);
}
static bool parse_save(const cJSON *data, h2h_pocket_reply_t *out) {
    const cJSON *save=cJSON_GetObjectItemCaseSensitive(data,"save");
    if(!save || cJSON_IsNull(save)) return true;
    char base64[80], sha[65], computed[65]; uint64_t schema;
    if(!cJSON_IsObject(save)||!field(save,"data",base64,sizeof(base64))||!field(save,"sha256",sha,sizeof(sha))||
       !number(save,"revision",&out->revision)||!number(save,"schema",&schema)||schema!=1) return false;
    size_t length=0;
    if(mbedtls_base64_decode((unsigned char *)&out->save,sizeof(out->save),&length,
                            (const unsigned char *)base64,strlen(base64))!=0 || length!=sizeof(out->save)) return false;
    digest(&out->save,computed);
    if(strcmp(sha,computed)!=0) return false;
    out->has_save=true; return true;
}
static bool sync_device(void) {
    h2h_pocket_record_t sent;
    if(xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    if(!pocket.healthy) { xSemaphoreGive(state_lock); return false; }
    sent=pocket.record; xSemaphoreGive(state_lock);
    char payload[768], save_part[320]="", unbind_part[150]="";
    if(sent.ownership.pending_unbind[0]) {
        snprintf(unbind_part,sizeof(unbind_part),",\"pending_unbind\":{\"operation_id\":\"%s\",\"expected_epoch\":%" PRIu64 "}",
                 sent.ownership.pending_unbind,sent.ownership.pending_epoch);
    } else if(sent.restore_ack) {
        char sha[65]; digest(&sent.remote_save,sha);
        snprintf(save_part,sizeof(save_part),",\"save_ack\":{\"revision\":%" PRIu64 ",\"sha256\":\"%s\"}",sent.remote_revision,sha);
    } else if(sent.dirty && !sent.conflict && h2h_ownership_can_play(&sent.ownership)) {
        char sha[65]; unsigned char base64[80]; size_t length=0; digest(&sent.save,sha);
        if(mbedtls_base64_encode(base64,sizeof(base64)-1,&length,(const unsigned char *)&sent.save,sizeof(sent.save))!=0) return false;
        base64[length]=0;
        snprintf(save_part,sizeof(save_part),",\"save\":{\"base_revision\":%" PRIu64 ",\"schema\":1,\"sha256\":\"%s\",\"data\":\"%s\"}",
                 sent.revision,sha,base64);
    }
    snprintf(payload,sizeof(payload),"{\"ownership_epoch\":%" PRIu64 "%s%s}",sent.ownership.epoch,unbind_part,save_part);
    cJSON *data=NULL;
    if(!api_json("/device/sync",HTTP_METHOD_POST,payload,true,&data)) { update(H2H_CLOUD_OFFLINE,"Sync unavailable; offline"); return false; }
    h2h_pocket_reply_t reply={0}; uint64_t account=0;
    const cJSON *owner=cJSON_GetObjectItemCaseSensitive(data,"account_id");
    const cJSON *active=cJSON_GetObjectItemCaseSensitive(data,"activated");
    const cJSON *locked=cJSON_GetObjectItemCaseSensitive(data,"locked");
    const cJSON *restore=cJSON_GetObjectItemCaseSensitive(data,"restore_required");
    char status[40]="",ack[33]="",rejected[33]="",card[96]="";
    bool ok=number(data,"ownership_epoch",&reply.epoch) && (cJSON_IsTrue(active)||cJSON_IsFalse(active)) &&
            (cJSON_IsTrue(locked)||cJSON_IsFalse(locked)) && field(data,"save_status",status,sizeof(status));
    if(owner && !cJSON_IsNull(owner)) ok=ok && number(data,"account_id",&account) && account>0 && account<=UINT32_MAX;
    reply.activated=cJSON_IsTrue(active); reply.account_id=(uint32_t)account;
    reply.conflict=strcmp(status,"revision_conflict")==0;
    reply.restore_required=cJSON_IsTrue(restore);
    reply.unbind_ack=field(data,"pending_unbind_ack",ack,sizeof(ack))?ack:NULL;
    reply.unbind_rejected=field(data,"pending_unbind_rejected",rejected,sizeof(rejected))?rejected:NULL;
    ok=ok && parse_save(data,&reply);
    if(ok && xSemaphoreTake(state_lock,portMAX_DELAY)==pdTRUE) {
        ok=h2h_pocket_reply(&pocket,&sent,&reply,write_record,NULL);
        if(ok) {
            state.bound=h2h_pocket_can_play(&pocket);
            if(field(data,"card_url",card,sizeof(card)) && strncmp(card,"https://ai-passport.randomdance.cn/card/",40)==0) strcpy(state.card_url,card);
            if(pocket.record.ownership.pending_unbind[0]) strcpy(state.message,"Unbind awaiting sync");
            else if(pocket.record.conflict) strcpy(state.message,"Save conflict; choose restore");
            else if(state.bound) { state.state=H2H_CLOUD_BOUND; strcpy(state.message,"Account connected; backed up"); }
            else if(pairing_id[0]) {
                state.state=pair_account?H2H_CLOUD_CLAIMED:H2H_CLOUD_PAIRING;
                if(pair_account) snprintf(state.message,sizeof(state.message),"Confirm #%" PRIu32,pair_account);
                else strcpy(state.message,"Scan; then confirm on card");
            } else { state.state=H2H_CLOUD_READY; strcpy(state.message,"Activation required"); }
            if(pocket.record.ownership.activated && !pocket.record.ownership.pending_unbind[0]) {
                pairing_id[0]=0; pair_account=0; state.pair_url[0]=0;
            }
        } else { state.state=H2H_CLOUD_ERROR; strcpy(state.message,"State rejected or save failed"); }
        xSemaphoreGive(state_lock);
    }
    cJSON_Delete(data); return ok;
}
static bool begin_pairing(void) {
    if(xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    bool allowed=pocket.healthy && !pocket.record.ownership.activated && !pocket.record.ownership.pending_unbind[0];
    pair_epoch=pocket.record.ownership.epoch; xSemaphoreGive(state_lock);
    if(!allowed) return false;
    cJSON *data=NULL; char code[33]="",url[96]=""; uint64_t epoch;
    if(!api_json("/device/pairings/begin",HTTP_METHOD_POST,"{}",true,&data)) return false;
    bool ok=field(data,"code",code,sizeof(code)) && field(data,"pair_url",url,sizeof(url)) &&
            number(data,"ownership_epoch",&epoch) && epoch==pair_epoch;
    char expected[96]; snprintf(expected,sizeof(expected),"https://ai-passport.randomdance.cn/pair/%s",code);
    ok=ok && strlen(code)==32 && strcmp(expected,url)==0;
    if(ok && xSemaphoreTake(state_lock,portMAX_DELAY)==pdTRUE) {
        strcpy(pairing_id,code); strcpy(state.pair_url,url); state.state=H2H_CLOUD_PAIRING;
        strcpy(state.message,"Scan; then confirm on card"); pair_account=0; pair_started=(uint32_t)xTaskGetTickCount();
        xSemaphoreGive(state_lock);
    }
    cJSON_Delete(data); return ok;
}
static void poll_pairing(void) {
    if(!pairing_id[0]) return;
    cJSON *data=NULL; uint64_t account=0,epoch=0; char status[16]="";
    if(!api_json("/device/pairings/current",HTTP_METHOD_GET,NULL,true,&data)) return;
    bool ok=number(data,"ownership_epoch",&epoch) && epoch==pair_epoch && field(data,"status",status,sizeof(status));
    if(ok && strcmp(status,"claimed")==0 && number(data,"account_id",&account) && account>0 && account<=UINT32_MAX) {
        if(xSemaphoreTake(state_lock,portMAX_DELAY)==pdTRUE) {
            pair_account=(uint32_t)account; state.state=H2H_CLOUD_CLAIMED;
            snprintf(state.message,sizeof(state.message),"Confirm #%" PRIu32,pair_account);
            xSemaphoreGive(state_lock);
        }
    } else if(!ok || strcmp(status,"expired")==0) {
        if(xSemaphoreTake(state_lock,portMAX_DELAY)==pdTRUE) {
            pairing_id[0]=0; pair_account=0; state.pair_url[0]=0; xSemaphoreGive(state_lock);
        }
        update(H2H_CLOUD_READY,"QR expired; open again");
    }
    cJSON_Delete(data);
}
static void confirm_pairing(const command_t *command) {
    if(!pairing_id[0] || !pair_account) return;
    if(xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return;
    bool allowed=pocket.healthy && pocket.record.ownership.epoch==command->epoch && pair_epoch==command->epoch &&
                 pair_account==command->account && strcmp(pairing_id,command->code)==0 && !pocket.record.ownership.pending_unbind[0];
    xSemaphoreGive(state_lock); if(!allowed) return;
    char payload[160]; snprintf(payload,sizeof(payload),"{\"code\":\"%s\",\"account_id\":%" PRIu32 ",\"expected_epoch\":%" PRIu64 "}",command->code,command->account,command->epoch);
    cJSON *data=NULL;
    if(!api_json("/device/pairings/confirm",HTTP_METHOD_POST,payload,true,&data)) { update(H2H_CLOUD_ERROR,"Confirm failed; refresh"); return; }
    cJSON_Delete(data);
    // Only sync + an atomic NVS ownership/save reset can unlock play.
    (void)sync_device();
}
static void worker(void *arg) {
    (void)arg;
    if(!wifi_init()) { update(H2H_CLOUD_ERROR,"Wi-Fi init failed"); vTaskDelete(NULL); return; }
    if(ssid_saved[0]) (void)wifi_connect(ssid_saved,password_saved);
    uint32_t next_poll=0,next_sync=0;
    for(;;) {
        command_t command;
        if(xQueueReceive(queue,&command,pdMS_TO_TICKS(500))==pdTRUE) {
            switch(command.type) {
            case CMD_OPEN:
                account_open=true;
                if(xEventGroupGetBits(events)&IP_READY) { (void)sync_device(); if(!pairing_id[0]) (void)begin_pairing(); }
                break;
            case CMD_CLOSE: account_open=false; break;
            case CMD_WIFI:
                if(wifi_connect(command.ssid,command.password)) {
                    if(!store_wifi(command.ssid,command.password)) update(H2H_CLOUD_ERROR,"Wi-Fi save failed");
                    (void)sync_device(); if(account_open && !pairing_id[0]) (void)begin_pairing();
                }
                memset(&command,0,sizeof(command)); break;
            case CMD_CONFIRM: confirm_pairing(&command); break;
            case CMD_REFRESH: if(xEventGroupGetBits(events)&IP_READY) (void)sync_device(); else update(H2H_CLOUD_OFFLINE,"Connect Wi-Fi first"); break;
            case CMD_FORGET:
                allow_reconnect=false; (void)esp_wifi_disconnect(); (void)esp_wifi_stop(); wifi_started=false;
                {
                    nvs_handle_t handle;
                    if(nvs_open("h2h_cloud",NVS_READWRITE,&handle)==ESP_OK) {
                        esp_err_t error=nvs_erase_key(handle,"ssid");
                        if(error==ESP_OK || error==ESP_ERR_NVS_NOT_FOUND) error=nvs_erase_key(handle,"wifi_pass");
                        if(error==ESP_OK || error==ESP_ERR_NVS_NOT_FOUND) error=nvs_commit(handle);
                        nvs_close(handle); if(error!=ESP_OK) update(H2H_CLOUD_ERROR,"Wi-Fi clear failed");
                    }
                }
                ssid_saved[0]=password_saved[0]=0; update(H2H_CLOUD_SETUP,"Wi-Fi cleared"); break;
            }
        }
        uint32_t now=(uint32_t)xTaskGetTickCount();
        if((xEventGroupGetBits(events)&IP_READY) && (uint32_t)(now-next_sync)>=pdMS_TO_TICKS(15000)) { (void)sync_device(); next_sync=now; }
        if(account_open && pairing_id[0] && (uint32_t)(now-next_poll)>=pdMS_TO_TICKS(3000)) {
            if((uint32_t)(now-pair_started)>=pdMS_TO_TICKS(300000)) {
                if(xSemaphoreTake(state_lock,portMAX_DELAY)==pdTRUE) {
                    pairing_id[0]=0; pair_account=0; state.pair_url[0]=0; xSemaphoreGive(state_lock);
                }
                update(H2H_CLOUD_READY,"QR expired; open again");
            } else poll_pairing();
            next_poll=now;
        }
    }
}
static bool enqueue(command_type_t type, const char *ssid, const char *password) {
    if(!queue) return false;
    command_t command={.type=type};
    if(ssid) { if(strlen(ssid)>32) return false; strcpy(command.ssid,ssid); }
    if(password) { if(strlen(password)>64) return false; strcpy(command.password,password); }
    bool result=xQueueSend(queue,&command,0)==pdTRUE; memset(&command,0,sizeof(command)); return result;
}
esp_err_t h2h_cloud_start(void) {
    state_lock=xSemaphoreCreateMutex(); events=xEventGroupCreate(); queue=xQueueCreate(4,sizeof(command_t));
    if(!state_lock || !events || !queue) return ESP_ERR_NO_MEM;
    memset(&state,0,sizeof(state)); state.state=H2H_CLOUD_SETUP; strcpy(state.message,"Activation required; use phone");
    h2h_pocket_init(&pocket,h2h_track_count,h2h_song_count);
    if(!load_identity()) { pocket.healthy=false; return ESP_FAIL; }
    nvs_handle_t handle;
    if(nvs_open("h2h_cloud",NVS_READONLY,&handle)!=ESP_OK) { pocket.healthy=false; return ESP_FAIL; }
    unsigned char bytes[H2H_POCKET_RECORD_SIZE]; size_t length=sizeof(bytes);
    esp_err_t error=nvs_get_blob(handle,"pocket_v2",bytes,&length); nvs_close(handle);
    if(error==ESP_OK) {
        if(!h2h_pocket_decode(&pocket,bytes,length)) strcpy(state.message,"Stored state invalid; locked");
    } else if(error==ESP_ERR_NVS_NOT_FOUND) {
        // Legacy bound/state records are preserved but never unlock or auto-import.
        h2h_pocket_encode(&pocket.record,bytes);
        if(!write_record(bytes,sizeof(bytes),NULL)) pocket.healthy=false;
    } else pocket.healthy=false;
    if(!pocket.healthy) { update(H2H_CLOUD_ERROR,"Storage invalid; locked"); return ESP_FAIL; }
    return xTaskCreate(worker,"h2h_cloud",8192,NULL,5,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
bool h2h_cloud_model(h2h_save_t *save, uint64_t *epoch, uint64_t *generation) {
    if(!save || !epoch || !generation || !state_lock || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    bool ok=pocket.healthy;
    if(ok) { *save=pocket.record.save; *epoch=pocket.record.ownership.epoch; *generation=pocket.model_generation; }
    xSemaphoreGive(state_lock); return ok;
}
bool h2h_cloud_local_save(uint64_t epoch, uint64_t generation, const h2h_save_t *save) {
    if(!state_lock || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    bool ok=generation==pocket.model_generation && h2h_pocket_local_save(&pocket,epoch,save,write_record,NULL);
    xSemaphoreGive(state_lock); return ok;
}
bool h2h_cloud_can_play(void) {
    if(!state_lock || xSemaphoreTake(state_lock,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    bool allowed=h2h_pocket_can_play(&pocket); xSemaphoreGive(state_lock); return allowed;
}
bool h2h_cloud_can_play_at(uint64_t epoch, uint64_t generation) {
    if(!state_lock || xSemaphoreTake(state_lock,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    bool allowed=epoch==pocket.record.ownership.epoch && generation==pocket.model_generation && h2h_pocket_can_play(&pocket);
    xSemaphoreGive(state_lock); return allowed;
}
bool h2h_cloud_unbind(uint64_t epoch, uint64_t generation) {
    if(!state_lock || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    char operation[33];
    if(pocket.record.ownership.pending_unbind[0]) strcpy(operation,pocket.record.ownership.pending_unbind); else hex_random(operation,16);
    bool ok=epoch==pocket.record.ownership.epoch && generation==pocket.model_generation && h2h_pocket_unbind(&pocket,operation,write_record,NULL);
    state.bound=false; state.pair_url[0]=0;
    strcpy(state.message,ok?"Unbound locally; sync pending":"Unbind save failed; locked");
    xSemaphoreGive(state_lock); if(ok) (void)enqueue(CMD_REFRESH,NULL,NULL); return ok;
}
bool h2h_cloud_restore(uint64_t epoch, uint64_t generation) {
    if(!state_lock || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    bool ok=epoch==pocket.record.ownership.epoch && generation==pocket.model_generation && h2h_pocket_restore_cloud(&pocket,write_record,NULL);
    xSemaphoreGive(state_lock); return ok;
}
bool h2h_cloud_wifi(const char *ssid, const char *password) { return ssid && password && enqueue(CMD_WIFI,ssid,password); }
bool h2h_cloud_open(void) { return enqueue(CMD_OPEN,NULL,NULL); }
bool h2h_cloud_close(void) { return enqueue(CMD_CLOSE,NULL,NULL); }
bool h2h_cloud_confirm(void) {
    if(!queue || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    command_t command={.type=CMD_CONFIRM,.epoch=pair_epoch,.account=pair_account};
    strcpy(command.code,pairing_id);
    bool allowed=state.state==H2H_CLOUD_CLAIMED && command.account && command.code[0];
    xSemaphoreGive(state_lock);
    return allowed && xQueueSend(queue,&command,0)==pdTRUE;
}
bool h2h_cloud_refresh(void) { return enqueue(CMD_REFRESH,NULL,NULL); }
bool h2h_cloud_forget_wifi(void) { return enqueue(CMD_FORGET,NULL,NULL); }
