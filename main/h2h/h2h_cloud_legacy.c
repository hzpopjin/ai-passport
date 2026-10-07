#include "h2h_cloud.h"
#include "h2h_ble.h"
#include "h2h_relay.h"
#include "mbedtls/md.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "h2h_tracks.h"
#include "h2h_resource_runtime.h"
#include "h2h_resource_tls.h"
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <strings.h>

#define API "https://apps.randomdance.cn/api/v1/passport"
#define IP_READY BIT0
#define REPLY_MAX 2048
static const char *TAG = "h2h_cloud";
typedef enum { CMD_OPEN, CMD_CLOSE, CMD_WIFI, CMD_CONFIRM, CMD_REFRESH, CMD_FORGET } command_type_t;
typedef struct { command_type_t type; char ssid[33], password[65], code[33]; uint64_t epoch; uint32_t account; } command_t;
typedef struct { unsigned char body[REPLY_MAX]; size_t length; bool overflow; } reply_t;
static QueueHandle_t queue;
static h2h_pocket_t pocket;
static h2h_relay_t relay;
static h2h_fullsave_t fullsave;
static bool fullsave_ready;
static bool debug_requested, debug_paused;
static stayc_game_save_t live_game;
static uint32_t live_settled;
static uint64_t live_epoch,live_generation,live_full_generation;
static bool live_game_valid;
static const char *full_key(int bank){return bank==0?"full_a":bank==1?"full_b":"full_ptr";}
static int full_read(int bank,void *bytes,size_t length,void *context){
    (void)context;nvs_handle_t handle;
    esp_err_t e=nvs_open("h2h_cloud",NVS_READONLY,&handle);if(e!=ESP_OK)return e==ESP_ERR_NVS_NOT_FOUND?0:-1;
    size_t actual=length;e=nvs_get_blob(handle,full_key(bank),bytes,&actual);nvs_close(handle);
    return e==ESP_ERR_NVS_NOT_FOUND?0:e==ESP_OK&&actual==length?1:-1;
}
static bool full_write(int bank,const void *bytes,size_t length,void *context){
    (void)context;nvs_handle_t handle;if(nvs_open("h2h_cloud",NVS_READWRITE,&handle)!=ESP_OK)return false;
    esp_err_t e=nvs_set_blob(handle,full_key(bank),bytes,length);if(e==ESP_OK)e=nvs_commit(handle);nvs_close(handle);return e==ESP_OK;
}
static const h2h_fullsave_io_t full_io={.read=full_read,.write=full_write};
static void full_flush_live_locked(void){
    if(live_game_valid&&live_epoch==pocket.record.ownership.epoch&&live_generation==pocket.model_generation&&
       live_full_generation==fullsave.generation&&!debug_requested&&!debug_paused&&!relay.gate&&h2h_pocket_can_play(&pocket))
        (void)h2h_fullsave_local(&fullsave,&pocket,&live_game,live_settled,&full_io);
}
static void full_recover_locked(void){
    if(!fullsave_ready||fullsave.bank<0||(fullsave.healthy&&pocket.healthy))return;
    h2h_pocket_t owner=pocket;h2h_fullsave_t restored;
    if(!h2h_fullsave_load(&restored,&owner,&full_io))return;
    uint8_t old[384],next[384];h2h_pocket_encode(&pocket.record,old);h2h_pocket_encode(&owner.record,next);
    if(!memcmp(old,next,384)&&fullsave.has_local==restored.has_local&&(!restored.has_local||!memcmp(fullsave.local,restored.local,164)))owner.model_generation=pocket.model_generation;
    fullsave=restored;pocket=owner;
}
static uint64_t pair_epoch;
static uint32_t pair_account, pair_started;
static SemaphoreHandle_t state_lock;
static EventGroupHandle_t events;
static h2h_cloud_status_t state;
static char secret[65], ssid_saved[33], password_saved[65], pairing_id[33];
static bool wifi_started, wifi_initialized, account_open;
static bool netif_ready, event_loop_ready, wifi_handler, ip_handler;
static esp_netif_t *wifi_netif;
static volatile bool allow_reconnect;
static volatile unsigned reconnect_attempts;
static bool debug_blocked(void) {
    if(!state_lock || xSemaphoreTake(state_lock,pdMS_TO_TICKS(100))!=pdTRUE) return true;
    bool blocked=debug_requested || debug_paused;
    xSemaphoreGive(state_lock); return blocked;
}
bool h2h_cloud_debug_pause(bool requested) {
    if(!state_lock || xSemaphoreTake(state_lock,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    debug_requested=requested;
    bool ready=requested?debug_paused:!debug_paused;
    xSemaphoreGive(state_lock); return ready;
}

static void heap_checkpoint(const char *phase) {
    const unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA;
    ESP_LOGI(TAG,"%s heap8_free=%u heap8_min=%u heap8_largest=%u dma_free=%u dma_min=%u dma_largest=%u",
        phase,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(caps),(unsigned)heap_caps_get_minimum_free_size(caps),
        (unsigned)heap_caps_get_largest_free_block(caps));
}
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
        out->bound=h2h_pocket_can_play(&pocket)&&!relay.gate;
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
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START && allow_reconnect) (void)esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, IP_READY);
        if (allow_reconnect && reconnect_attempts++ < 5) (void)esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        reconnect_attempts = 0;
        xEventGroupSetBits(events, IP_READY);
    }
}
static void wifi_release(void) {
    allow_reconnect=false;
    if(wifi_started) { (void)esp_wifi_disconnect(); (void)esp_wifi_stop(); wifi_started=false; }
    if(ip_handler && esp_event_handler_unregister(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event)==ESP_OK) ip_handler=false;
    if(wifi_handler && esp_event_handler_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event)==ESP_OK) wifi_handler=false;
    if(wifi_initialized && esp_wifi_deinit()==ESP_OK) wifi_initialized=false;
    if(!wifi_initialized && !wifi_handler && !ip_handler && wifi_netif) {
        esp_netif_destroy_default_wifi(wifi_netif); wifi_netif=NULL;
    }
    xEventGroupClearBits(events,IP_READY);
}
static bool wifi_init(void) {
    if(wifi_initialized && wifi_handler && ip_handler) return true;
    if(wifi_initialized || wifi_handler || ip_handler || wifi_netif) {
        wifi_release();
        if(wifi_initialized || wifi_handler || ip_handler || wifi_netif) return false;
    }
    if(!netif_ready) { if(esp_netif_init()!=ESP_OK) return false; netif_ready=true; }
    if(!event_loop_ready) {
        esp_err_t e=esp_event_loop_create_default();
        if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) return false;
        event_loop_ready=true;
    }
    wifi_netif=esp_netif_create_default_wifi_sta();
    if(!wifi_netif) return false;
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
    if(esp_wifi_init(&config)!=ESP_OK) goto fail;
    wifi_initialized=true;
    if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK || esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK) goto fail;
    if(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL)!=ESP_OK) goto fail;
    wifi_handler=true;
    if(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL)!=ESP_OK) goto fail;
    ip_handler=true;
    return true;
fail:
    wifi_release(); return false;
}
static bool wifi_connect(const char *ssid, const char *password) {
    if (!ssid[0] || strlen(ssid) > 32 || strlen(password) > 64) return false;
    if(!wifi_init()) { update(H2H_CLOUD_ERROR,"Wi-Fi init failed; retry setup"); return false; }
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
        if (esp_wifi_start() != ESP_OK) { allow_reconnect=false; wifi_release(); return false; }
        wifi_started = true;
    } else if(esp_wifi_connect()!=ESP_OK) { allow_reconnect=false; return false; }
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
    if(debug_blocked()) return false;
    char url[192];
    snprintf(url, sizeof(url), API "%s", path);
    esp_http_client_config_t config = {
        .url = url, .method = method, .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach, .event_handler = http_event,
        .user_data = reply, .disable_auto_redirect = true,
    };
    heap_checkpoint("before_tls");
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
    heap_checkpoint("after_tls");
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
    if(fullsave_ready&&(fullsave.bank>=0||fullsave.has_local))return h2h_fullsave_pocket_write(&fullsave,&pocket,bytes,length,&full_io);
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
typedef struct { uint64_t epoch,generation; uint32_t revision; char sha[65]; } resource_request_t;
typedef struct { reply_t reply; char range[80]; } resource_chunk_t;
static esp_err_t resource_http_event(esp_http_client_event_t *event) {
    resource_chunk_t *chunk=event->user_data;
    if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key && event->header_value && !strcasecmp(event->header_key,"Content-Range"))
        snprintf(chunk->range,sizeof(chunk->range),"%s",event->header_value);
    if(event->event_id==HTTP_EVENT_ON_DATA) {
        esp_http_client_event_t data=*event;data.user_data=&chunk->reply;return http_event(&data);
    }
    return ESP_OK;
}
static bool resource_fetch(size_t offset,void *out,size_t length,void *ctx) {
    resource_request_t *r=ctx;
    if(!h2h_cloud_can_play_at(r->epoch,r->generation)||!length||length>1024)return false;
    char url[256],range[64],expected[80];
    snprintf(url,sizeof(url),API "/device/resources/%s/blob?expected_epoch=%" PRIu64 "&resource_revision=%" PRIu32,r->sha,r->epoch,r->revision);
    snprintf(range,sizeof(range),"bytes=%u-%u",(unsigned)offset,(unsigned)(offset+length-1));
    snprintf(expected,sizeof(expected),"bytes %u-%u/%u",(unsigned)offset,(unsigned)(offset+length-1),H2H_RESOURCE_BYTES);
    resource_chunk_t chunk={0};esp_http_client_config_t config={.url=url,.method=HTTP_METHOD_GET,.timeout_ms=8000,
        .crt_bundle_attach=esp_crt_bundle_attach,.event_handler=resource_http_event,.user_data=&chunk,.disable_auto_redirect=true};
    esp_http_client_handle_t client=esp_http_client_init(&config);if(!client)return false;
    (void)esp_http_client_set_header(client,"X-Passport-Device",state.device_id);(void)esp_http_client_set_header(client,"X-Passport-Token",secret);
    (void)esp_http_client_set_header(client,"Range",range);(void)esp_http_client_set_header(client,"Accept-Encoding","identity");
    esp_err_t error=esp_http_client_perform(client);int status=esp_http_client_get_status_code(client);esp_http_client_cleanup(client);
    bool ok=error==ESP_OK&&status==206&&!chunk.reply.overflow&&chunk.reply.length==length&&!strcmp(expected,chunk.range)&&h2h_cloud_can_play_at(r->epoch,r->generation);
    if(ok)memcpy(out,chunk.reply.body,length);
    return ok;
}
static bool hex_bytes(const char *hex,unsigned char *out,size_t length) {
    if(strlen(hex)!=2*length)return false;
    for(size_t i=0;i<length;i++){unsigned a,b;char x=hex[2*i],y=hex[2*i+1];
        if(x>='0'&&x<='9')a=(unsigned)(x-'0');else if(x>='a'&&x<='f')a=(unsigned)(x-'a'+10);else return false;
        if(y>='0'&&y<='9')b=(unsigned)(y-'0');else if(y>='a'&&y<='f')b=(unsigned)(y-'a'+10);else return false;out[i]=(unsigned char)((a<<4)|b);
    }return true;
}
static bool resource_ack(const resource_request_t *request,const char *status) {
    if(!h2h_cloud_can_play_at(request->epoch,request->generation))return false;
    char payload[256];snprintf(payload,sizeof(payload),"{\"expected_epoch\":%" PRIu64 ",\"resource_revision\":%" PRIu32 ",\"sha256\":\"%s\",\"status\":\"%s\"}",request->epoch,request->revision,request->sha,status);
    cJSON *data=NULL;bool ok=api_json("/device/resources/ack",HTTP_METHOD_POST,payload,true,&data);cJSON_Delete(data);return ok;
}
static void process_resource(const cJSON *data) {
    if(!h2h_resource_runtime_available())return;
    resource_request_t request={0};uint64_t revision=0;h2h_save_t ignored;
    if(!number(data,"ownership_epoch",&request.epoch)||!number(data,"resource_revision",&revision)||revision>UINT32_MAX)return;
    uint64_t current_epoch;
    if(!h2h_cloud_model(&ignored,&current_epoch,&request.generation)||current_epoch!=request.epoch||!h2h_cloud_can_play_at(request.epoch,request.generation))return;
    request.revision=(uint32_t)revision;
    const cJSON *selection=cJSON_GetObjectItemCaseSensitive(data,"resource_selection");
    if(cJSON_IsNull(selection)) {
        strcpy(request.sha,"builtin");
        if(revision&&h2h_resource_runtime_failed(request.epoch,request.revision)){(void)resource_ack(&request,"failed");return;}
        h2h_resource_runtime_builtin(request.epoch,request.generation,request.revision);
        if(revision&&h2h_resource_runtime_installed(request.epoch,request.revision,NULL))(void)resource_ack(&request,"installed");
        return;
    }
    char id[33],signature[89],kind[24],model[32];uint64_t value=0;unsigned char sha[32];
    if(!cJSON_IsObject(selection)||!revision||!field(selection,"id",id,sizeof(id))||!field(selection,"sha256",request.sha,sizeof(request.sha))||!hex_bytes(request.sha,sha,32))return;
    if(h2h_resource_runtime_installed(request.epoch,request.revision,sha)){(void)resource_ack(&request,"installed");return;}
    if(h2h_resource_runtime_failed(request.epoch,request.revision)){(void)resource_ack(&request,"failed");return;}
    char path[192];snprintf(path,sizeof(path),"/device/resources/%s/manifest?expected_epoch=%" PRIu64 "&resource_revision=%" PRIu32,request.sha,request.epoch,request.revision);
    cJSON *manifest=NULL;if(!api_json(path,HTTP_METHOD_GET,NULL,true,&manifest))return;
    h2h_resource_manifest_t m={0};char digest_hex[65];size_t decoded=0;
    bool ok=field(manifest,"resource_id",m.id,sizeof(m.id))&&!strcmp(m.id,id)&&field(manifest,"model",model,sizeof(model))&&!strcmp(model,"esp32c3-pocket-8m")&&
        number(manifest,"format_version",&value)&&value==2&&number(manifest,"size",&value)&&value==H2H_RESOURCE_BYTES&&
        number(manifest,"decoded_bytes",&value)&&value==15360&&field(manifest,"kind",kind,sizeof(kind));
    if(ok){if(!strcmp(kind,"member_visual"))m.kind=1;else if(!strcmp(kind,"group_visual"))m.kind=2;else ok=false;}
    if(ok&&number(manifest,"version",&value)&&value>0&&value<=UINT32_MAX)m.version=(uint32_t)value;else ok=false;
    if(ok&&number(manifest,"min_firmware",&value)&&value>0&&value<=UINT32_MAX)m.min_firmware=(uint32_t)value;else ok=false;
    if(ok&&number(manifest,"max_firmware",&value)&&value>0&&value<=UINT32_MAX)m.max_firmware=(uint32_t)value;else ok=false;
    ok=ok&&field(manifest,"key_id",m.key_id,sizeof(m.key_id))&&field(manifest,"sha256",digest_hex,sizeof(digest_hex))&&!strcmp(digest_hex,request.sha)&&hex_bytes(digest_hex,m.sha256,32)&&
       field(manifest,"signature",signature,sizeof(signature))&&mbedtls_base64_decode(m.signature,sizeof(m.signature),&decoded,(const unsigned char *)signature,strlen(signature))==0&&decoded==64;
    cJSON_Delete(manifest);
    if(ok)ok=h2h_resource_runtime_prepare(&m,request.epoch,request.generation,request.revision,resource_fetch,&request);
    if(ok){bool accepted=resource_ack(&request,"verified");h2h_resource_runtime_approve(request.epoch,request.revision,accepted);}
    else {(void)resource_ack(&request,"failed");update(H2H_CLOUD_BOUND,"Resource failed; previous kept");}
}
static bool sync_device(void) {
    h2h_pocket_record_t sent;
    if(xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    if(!pocket.healthy||(fullsave.server_full&&!pocket.record.ownership.pending_unbind[0])) { xSemaphoreGive(state_lock); return false; }
    sent=pocket.record; xSemaphoreGive(state_lock);
    char payload[1280], save_part[320]="", unbind_part[150]="";
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
    cJSON *capabilities=cJSON_CreateObject(),*keys=cJSON_CreateArray();char key_ids[8][33];unsigned key_count=h2h_resource_runtime_keys(key_ids);
    for(unsigned i=0;i<key_count;i++)cJSON_AddItemToArray(keys,cJSON_CreateString(key_ids[i]));
    cJSON_AddNumberToObject(capabilities,"firmware",H2H_RESOURCE_FIRMWARE);cJSON_AddNumberToObject(capabilities,"resource_format",2);
    cJSON_AddNumberToObject(capabilities,"resource_capacity",h2h_resource_runtime_available()?H2H_RESOURCE_SLOT_BYTES:0);cJSON_AddItemToObject(capabilities,"resource_keys",keys);
    char *encoded_capabilities=cJSON_PrintUnformatted(capabilities);cJSON_Delete(capabilities);if(!encoded_capabilities)return false;
    int payload_size=snprintf(payload,sizeof(payload),"{\"ownership_epoch\":%" PRIu64 "%s%s,\"capabilities\":%s}",sent.ownership.epoch,unbind_part,save_part,encoded_capabilities);
    cJSON_free(encoded_capabilities);if(payload_size<0||(size_t)payload_size>=sizeof(payload))return false;
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
            else if(pocket.record.conflict) strcpy(state.message,"Save conflict; both protected");
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
    if(ok)process_resource(data);
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
static bool relay_hmac(const uint8_t key[32],const char *bytes,size_t length,uint8_t out[32]) {
    return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),key,32,(const unsigned char *)bytes,length,out)==0;
}
static bool relay_sha(const void *bytes,size_t length,uint8_t out[32]) {
    return mbedtls_sha256(bytes,length,out,0)==0;
}
static uint32_t relay_now(void) { return (uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS; }
static uint32_t resource_clock(void *ctx){(void)ctx;return relay_now();}
static bool resource_tls_ready(void){return events&&(xEventGroupGetBits(events)&IP_READY)&&time(NULL)>=1735689600;}
static bool resource_tls_scope(const h2h_resource_job_t *job,void *ctx){
    (void)ctx;return resource_tls_ready()&&h2h_ble_connected()&&job->connection_generation&&
        h2h_ble_relay_connection_generation()==job->connection_generation&&h2h_cloud_can_play_at(job->epoch,job->model_generation);
}
static bool relay_tls_fetch(const h2h_resource_job_t *job,void *out,void *ctx){
    return h2h_resource_tls_get(job,out,resource_clock,resource_tls_scope,ctx);
}
static h2h_resource_work_status_t relay_resource_work(const h2h_resource_job_t *job,h2h_resource_progress_t *progress,void *ctx){
    return h2h_resource_runtime_execute(job,progress,relay_tls_fetch,ctx);
}
static void relay_status_locked(void){
    state.bound=h2h_pocket_can_play(&pocket)&&!relay.gate;
    if(relay.stage==H2H_RELAY_CARD_CONFIRM){
        pair_account=relay.account;pair_epoch=relay.pair_epoch;strcpy(pairing_id,relay.pair_code);
        state.state=H2H_CLOUD_CLAIMED;snprintf(state.message,sizeof(state.message),"Confirm #%" PRIu32,relay.account);
    }else if(relay.stage==H2H_RELAY_ERROR){state.state=H2H_CLOUD_ERROR;snprintf(state.message,sizeof(state.message),"%s",relay.error);}
    else if(relay.stage==H2H_RELAY_COMPLETE){state.state=H2H_CLOUD_BOUND;strcpy(state.message,"Phone sync complete");pairing_id[0]=0;pair_account=0;state.pair_url[0]=0;}
    else {state.state=H2H_CLOUD_WORKING;strcpy(state.message,"Phone sync in progress");}
}
static void relay_worker(void) {
    bool disconnected=h2h_ble_relay_disconnected();uint32_t lease=0;size_t length=0;
    char *bytes=h2h_ble_relay_take(&lease,&length);
    if(!disconnected&&!bytes)return;
    size_t reply_length=0;h2h_resource_job_t *job=NULL;
    uint32_t connection=h2h_ble_relay_connection_generation();
    h2h_resource_capabilities_t capabilities;h2h_resource_runtime_capabilities(&capabilities,resource_tls_ready());
    if(xSemaphoreTake(state_lock,portMAX_DELAY)==pdTRUE) {
        if(disconnected)h2h_relay_disconnect(&relay);
        if(bytes) {
            /* Flush the real published gameplay before forming/applying a
             * relay snapshot. Never copy an old model across owner recovery. */
            full_flush_live_locked();
            relay.ops.resources=capabilities;
            reply_length=h2h_relay_rpc(&relay,bytes,length,bytes,relay_now());
            /* The queued RPC came through authenticated GATT. Only a valid
             * server-signed claim for this live account/epoch may reach this
             * stage. Keep the device-signed confirm; the phone never signs it. */
            if(relay.stage==H2H_RELAY_CARD_CONFIRM &&
               h2h_ble_authenticated_for_connection(connection) &&
               h2h_relay_confirm(&relay,relay_now())) {
                const char poll[]="{\"action\":\"poll\"}";
                reply_length=h2h_relay_rpc(&relay,poll,sizeof(poll)-1,bytes,relay_now());
            }
            if(!reply_length){strcpy(bytes,"{\"state\":\"error\",\"code\":\"relay_memory\"}");reply_length=strlen(bytes);}
            if(relay.stage==H2H_RELAY_RESOURCE_WORK){job=malloc(sizeof(*job));if(job&&!h2h_relay_resource_take(&relay,job)){free(job);job=NULL;}}
            relay_status_locked();
        }
        xSemaphoreGive(state_lock);
    }
    if(job){
        job->connection_generation=connection;h2h_resource_progress_t progress;
        h2h_resource_work_status_t status=relay_resource_work(job,&progress,NULL);
        bool retired=h2h_ble_relay_disconnected()||h2h_ble_relay_connection_generation()!=connection;
        if(xSemaphoreTake(state_lock,portMAX_DELAY)==pdTRUE){
            if(retired)h2h_relay_disconnect(&relay);
            else {full_flush_live_locked();(void)h2h_relay_resource_done(&relay,job,status,&progress,relay_now());}
            const char poll[]="{\"action\":\"poll\"}";
            reply_length=h2h_relay_rpc(&relay,poll,sizeof(poll)-1,bytes,relay_now());
            if(!reply_length){strcpy(bytes,"{\"state\":\"error\",\"code\":\"relay_memory\"}");reply_length=strlen(bytes);}
            relay_status_locked();xSemaphoreGive(state_lock);
        }
        free(job);
    }
    if(bytes)h2h_ble_relay_finish(lease,reply_length);
}
void h2h_cloud_game_stars(uint64_t epoch,uint64_t generation,uint32_t stars) {
    if(state_lock&&xSemaphoreTake(state_lock,pdMS_TO_TICKS(10))==pdTRUE) {
        if(!debug_requested&&!debug_paused&&epoch==pocket.record.ownership.epoch&&generation==pocket.model_generation)
            h2h_relay_stars(&relay,epoch,generation,stars);
        xSemaphoreGive(state_lock);
    }
}
static bool debug_boundary(void) {
    if(xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return true;
    bool requested=debug_requested;
    bool resume=debug_paused;
    if(!requested) debug_paused=false;
    xSemaphoreGive(state_lock);
    if(!requested) {
        if(resume && ssid_saved[0]) (void)wifi_connect(ssid_saved,password_saved);
        return false;
    }
    /* Acknowledge only between complete worker operations, after any in-flight
     * reply has finished. The local game cannot start before this boundary. */
    wifi_release();
    command_t discarded;
    while(xQueueReceive(queue,&discarded,0)==pdTRUE) memset(&discarded,0,sizeof(discarded));
    bool released=!wifi_initialized && !wifi_handler && !ip_handler && !wifi_netif && !wifi_started;
    if(xSemaphoreTake(state_lock,portMAX_DELAY)==pdTRUE) {
        /* Also retain the resume boundary if cancellation raced with release. */
        if(released) debug_paused=true;
        xSemaphoreGive(state_lock);
    }
    return true;
}
static void worker(void *arg) {
    (void)arg;
    if(ssid_saved[0] && h2h_pocket_can_play(&pocket)) (void)wifi_connect(ssid_saved,password_saved);
    uint32_t next_poll=0;
    for(;;) {
        if(debug_boundary()) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        relay_worker();
        command_t command;
        if(xQueueReceive(queue,&command,pdMS_TO_TICKS(50))==pdTRUE) {
            switch(command.type) {
            case CMD_OPEN:
                account_open=true;
                /* BLE phone relay is the normal activation path; no Wi-Fi/SNTP prerequisite. */
                break;
            case CMD_CLOSE: account_open=false; break;
            case CMD_WIFI:
                if(wifi_connect(command.ssid,command.password)) {
                    if(!store_wifi(command.ssid,command.password)) update(H2H_CLOUD_ERROR,"Wi-Fi save failed");
                    else { strcpy(ssid_saved,command.ssid); strcpy(password_saved,command.password); }
                    /* Wi-Fi is a resource transport. Phone BLE owns activation
                     * and save synchronization; never start legacy pairing. */
                }
                memset(&command,0,sizeof(command)); break;
            case CMD_CONFIRM:
                if(xSemaphoreTake(state_lock,portMAX_DELAY)==pdTRUE) {
                    bool relayed=relay.active;
                    if(relayed && command.account==relay.account && command.epoch==relay.pair_epoch && !strcmp(command.code,relay.pair_code))
                        (void)h2h_relay_confirm(&relay,relay_now());
                    xSemaphoreGive(state_lock);if(!relayed)confirm_pairing(&command);
                }
                break;
            case CMD_REFRESH: h2h_resource_runtime_retry(); break;
            case CMD_FORGET:
                wifi_release();
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
        if(!relay.active && account_open && pairing_id[0] && (uint32_t)(now-next_poll)>=pdMS_TO_TICKS(3000)) {
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
    if(!queue || debug_blocked()) return false;
    command_t command={.type=type};
    if(ssid) { if(strlen(ssid)>32) return false; strcpy(command.ssid,ssid); }
    if(password) { if(strlen(password)>64) return false; strcpy(command.password,password); }
    bool result=xQueueSend(queue,&command,0)==pdTRUE; memset(&command,0,sizeof(command)); return result;
}
esp_err_t h2h_cloud_start(void) {
    fullsave_ready=live_game_valid=false;h2h_fullsave_init(&fullsave);
    state_lock=xSemaphoreCreateMutex(); events=xEventGroupCreate(); queue=xQueueCreate(4,sizeof(command_t));
    if(!state_lock || !events || !queue) return ESP_ERR_NO_MEM;
    debug_requested=debug_paused=false;
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
    if(!h2h_fullsave_load(&fullsave,&pocket,&full_io)||!pocket.healthy) { update(H2H_CLOUD_ERROR,"Storage invalid; locked"); return ESP_FAIL; }
    fullsave_ready=true;
    if(fullsave.bank<0&&h2h_pocket_can_play(&pocket)){
        uint8_t legacy[164];size_t size=sizeof(legacy);esp_err_t e=nvs_open("stayc_game",NVS_READONLY,&handle);
        if(e==ESP_OK){e=nvs_get_blob(handle,"progress",legacy,&size);nvs_close(handle);
            if(e==ESP_OK){stayc_game_record_t record;
                if(!stayc_game_record_decode(&record,legacy,size)||!stayc_game_save_valid(&record.gameplay,true))fullsave.healthy=false;
                else if(record.epoch==pocket.record.ownership.epoch&&!h2h_fullsave_seed(&fullsave,&pocket,legacy,size,&full_io))fullsave.healthy=false;
            }else if(e!=ESP_ERR_NVS_NOT_FOUND)fullsave.healthy=false;
        }else if(e!=ESP_ERR_NVS_NOT_FOUND)fullsave.healthy=false;
    }
    uint8_t trusted_key[32];size_t key_length=sizeof(trusted_key);bool enrolled=false;
    if(nvs_open("h2h_cloud",NVS_READONLY,&handle)==ESP_OK) {
        enrolled=nvs_get_blob(handle,"relay_key",trusted_key,&key_length)==ESP_OK&&key_length==sizeof(trusted_key);nvs_close(handle);
    }
    h2h_relay_ops_t ops={.hmac=relay_hmac,.sha=relay_sha,.random=esp_fill_random,.write=write_record,.full=&fullsave,.full_io=&full_io,.resource_work=relay_resource_work};
    h2h_relay_init(&relay,&pocket,state.device_id,enrolled?trusted_key:NULL,&ops);memset(trusted_key,0,sizeof(trusted_key));
    return xTaskCreate(worker,"h2h_cloud",16384,NULL,5,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
bool h2h_cloud_model(h2h_save_t *save, uint64_t *epoch, uint64_t *generation) {
    if(debug_blocked()) return false;
    if(!save || !epoch || !generation || !state_lock || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    full_recover_locked();
    bool ok=pocket.healthy && !debug_requested && !debug_paused;
    if(ok) { *save=pocket.record.save; *epoch=pocket.record.ownership.epoch; *generation=pocket.model_generation; }
    xSemaphoreGive(state_lock); return ok;
}
bool h2h_cloud_local_save(uint64_t epoch, uint64_t generation, const h2h_save_t *save) {
    if(!state_lock || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    bool ok=!debug_requested && !debug_paused && generation==pocket.model_generation && h2h_pocket_local_save(&pocket,epoch,save,write_record,NULL);
    xSemaphoreGive(state_lock); return ok;
}
bool h2h_cloud_can_play(void) {
    if(!state_lock || xSemaphoreTake(state_lock,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    bool allowed=h2h_pocket_can_play(&pocket)&&!relay.gate; xSemaphoreGive(state_lock); return allowed;
}
bool h2h_cloud_can_play_at(uint64_t epoch, uint64_t generation) {
    if(!state_lock || xSemaphoreTake(state_lock,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    bool allowed=!debug_requested && !debug_paused && epoch==pocket.record.ownership.epoch && generation==pocket.model_generation && h2h_pocket_can_play(&pocket)&&!relay.gate;
    xSemaphoreGive(state_lock); return allowed;
}
bool h2h_cloud_guarded_write(uint64_t epoch,uint64_t generation,bool (*write)(const void *,size_t,void *),const void *bytes,size_t length,void *ctx) {
    if(!state_lock || !write || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE)return false;
    full_recover_locked();
    bool ok=!debug_requested && !debug_paused && epoch==pocket.record.ownership.epoch && generation==pocket.model_generation && h2h_pocket_can_play(&pocket)&&!relay.gate && write(bytes,length,ctx);
    xSemaphoreGive(state_lock);return ok;
}
bool h2h_cloud_unbind(uint64_t epoch, uint64_t generation) {
    if(debug_blocked()) return false;
    if(!state_lock || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    char operation[33];
    if(pocket.record.ownership.pending_unbind[0]) strcpy(operation,pocket.record.ownership.pending_unbind); else hex_random(operation,16);
    if(debug_requested || debug_paused) { xSemaphoreGive(state_lock); return false; }
    bool ok=epoch==pocket.record.ownership.epoch && generation==pocket.model_generation && h2h_pocket_unbind(&pocket,operation,write_record,NULL);
    state.bound=false; state.pair_url[0]=0;
    strcpy(state.message,ok?"Unbound locally; sync pending":"Unbind save failed; locked");
    xSemaphoreGive(state_lock); if(ok) (void)enqueue(CMD_REFRESH,NULL,NULL); return ok;
}
bool h2h_cloud_restore(uint64_t epoch, uint64_t generation) {
    if(debug_blocked()) return false;
    if(!state_lock || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    bool ok=!debug_requested && !debug_paused && epoch==pocket.record.ownership.epoch && generation==pocket.model_generation &&
        (fullsave.conflict?h2h_fullsave_restore(&fullsave,&pocket,&full_io):h2h_pocket_restore_cloud(&pocket,write_record,NULL));
    xSemaphoreGive(state_lock); return ok;
}
int h2h_cloud_game_record(uint64_t epoch,uint8_t bytes[164]){
    if(!state_lock||xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE)return -1;
    int result=fullsave_ready&&!fullsave.healthy?-1:0;
    if(fullsave_ready&&fullsave.healthy&&fullsave.has_local&&epoch==pocket.record.ownership.epoch){memcpy(bytes,fullsave.local,164);result=1;}
    xSemaphoreGive(state_lock);return result;
}
bool h2h_cloud_game_record_write_locked(const void *bytes,size_t length){
    stayc_game_record_t record;
    if(!fullsave_ready||!stayc_game_record_decode(&record,bytes,length)||record.epoch!=pocket.record.ownership.epoch)return false;
    return h2h_fullsave_local(&fullsave,&pocket,&record.gameplay,record.settled_claims,&full_io);
}
void h2h_cloud_game_snapshot(uint64_t epoch,uint64_t generation,const stayc_game_save_t *game,uint32_t settled){
    if(!game||!state_lock||xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE)return;
    if(!debug_requested&&!debug_paused&&!relay.gate&&h2h_pocket_can_play(&pocket)&&epoch==pocket.record.ownership.epoch&&generation==pocket.model_generation&&stayc_game_save_valid(game,true)){
        live_game=*game;live_settled=settled;live_epoch=epoch;live_generation=generation;live_full_generation=fullsave.generation;live_game_valid=true;
    }
    xSemaphoreGive(state_lock);
}
bool h2h_cloud_wifi(const char *ssid, const char *password) { return ssid && password && enqueue(CMD_WIFI,ssid,password); }
bool h2h_cloud_open(void) { return enqueue(CMD_OPEN,NULL,NULL); }
bool h2h_cloud_close(void) { return enqueue(CMD_CLOSE,NULL,NULL); }
bool h2h_cloud_confirm(void) {
    if(debug_blocked()) return false;
    if(!queue || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE) return false;
    command_t command={.type=CMD_CONFIRM,.epoch=pair_epoch,.account=pair_account};
    strcpy(command.code,pairing_id);
    bool allowed=!debug_requested && !debug_paused && state.state==H2H_CLOUD_CLAIMED && command.account && command.code[0];
    xSemaphoreGive(state_lock);
    return allowed && xQueueSend(queue,&command,0)==pdTRUE;
}
bool h2h_cloud_refresh(void) { return enqueue(CMD_REFRESH,NULL,NULL); }
bool h2h_cloud_forget_wifi(void) { return enqueue(CMD_FORGET,NULL,NULL); }
