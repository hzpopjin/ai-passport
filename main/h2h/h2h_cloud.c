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
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define API "https://apps.randomdance.cn/api/v1/passport"
#define IP_READY BIT0
#define THEME_DONE BIT1
#define THEME_SAVED BIT2
#define REPLY_MAX 2048
static const char *TAG = "h2h_cloud";
typedef enum { CMD_OPEN, CMD_CLOSE, CMD_WIFI, CMD_CONFIRM, CMD_REFRESH, CMD_FORGET } command_type_t;
typedef struct { command_type_t type; char ssid[33], password[65]; } command_t;
typedef struct { unsigned char body[REPLY_MAX]; size_t length; bool overflow; } reply_t;
static QueueHandle_t queue, themes;
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
    char url[192], authorization[110];
    snprintf(url, sizeof(url), API "%s", path);
    esp_http_client_config_t config = {
        .url = url, .method = method, .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach, .event_handler = http_event,
        .user_data = reply, .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    if (auth) {
        snprintf(authorization, sizeof(authorization), "Passport %s.%s", state.device_id, secret);
        (void)esp_http_client_set_header(client, "Authorization", authorization);
        memset(authorization, 0, sizeof(authorization));
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
static bool register_device(void) {
    char payload[160];
    snprintf(payload, sizeof(payload), "{\"device_id\":\"%s\",\"device_secret\":\"%s\"}", state.device_id, secret);
    cJSON *data = NULL;
    if (!api_json("/device/register", HTTP_METHOD_POST, payload, false, &data)) return false;
    char card_url[96];
    const cJSON *bound = cJSON_GetObjectItemCaseSensitive(data, "bound");
    bool ok = field(data, "card_url", card_url, sizeof(card_url)) &&
              strncmp(card_url, "https://ai-passport.randomdance.cn/card/", 40) == 0 &&
              (cJSON_IsTrue(bound) || cJSON_IsFalse(bound));
    if (ok && xSemaphoreTake(state_lock, portMAX_DELAY) == pdTRUE) {
        strcpy(state.card_url, card_url);
        state.bound = cJSON_IsTrue(bound);
        if (state.bound) { state.state = H2H_CLOUD_BOUND; strcpy(state.message, "Account connected"); }
        else { state.state = H2H_CLOUD_READY; strcpy(state.message, "Scan to sign in"); }
        xSemaphoreGive(state_lock);
    }
    if (ok) {
        nvs_handle_t handle;
        if (nvs_open("h2h_cloud", NVS_READWRITE, &handle) == ESP_OK) {
            (void)nvs_set_u8(handle, "bound", cJSON_IsTrue(bound) ? 1 : 0);
            (void)nvs_commit(handle); nvs_close(handle);
        }
    }
    cJSON_Delete(data);
    return ok;
}
static bool begin_pairing(void) {
    cJSON *data = NULL;
    if (!api_json("/device/pairings", HTTP_METHOD_POST, "{}", true, &data)) return false;
    char url[96], id[33];
    bool ok = field(data, "pair_url", url, sizeof(url)) && field(data, "pairing_id", id, sizeof(id)) &&
              strncmp(url, "https://ai-passport.randomdance.cn/pair/", 40) == 0;
    if (ok && xSemaphoreTake(state_lock, portMAX_DELAY) == pdTRUE) {
        strcpy(pairing_id, id);
        strcpy(state.pair_url, url);
        state.state = H2H_CLOUD_PAIRING;
        strcpy(state.message, "Scan to sign in");
        xSemaphoreGive(state_lock);
    }
    cJSON_Delete(data);
    return ok;
}
static void poll_pairing(void) {
    if (!pairing_id[0]) return;
    char path[80]; snprintf(path, sizeof(path), "/device/pairings/%s", pairing_id);
    cJSON *data = NULL;
    if (!api_json(path, HTTP_METHOD_GET, NULL, true, &data)) return;
    char status[32];
    if (field(data, "status", status, sizeof(status))) {
        if (strcmp(status, "claimed") == 0) {
            const cJSON *account = cJSON_GetObjectItemCaseSensitive(data, "account_id");
            if (cJSON_IsNumber(account) && account->valuedouble > 0 && account->valuedouble < 1000000000) {
                char message[64];
                snprintf(message, sizeof(message), "Confirm account #%d", account->valueint);
                update(H2H_CLOUD_CLAIMED, message);
            }
        }
        else if (strcmp(status, "expired") == 0) {
            pairing_id[0] = 0;
            if (xSemaphoreTake(state_lock, portMAX_DELAY) == pdTRUE) { state.pair_url[0] = 0; xSemaphoreGive(state_lock); }
            update(H2H_CLOUD_READY, "QR expired");
        }
    }
    cJSON_Delete(data);
}
static void confirm_pairing(void) {
    if (!pairing_id[0]) return;
    char path[96]; snprintf(path, sizeof(path), "/device/pairings/%s/confirm", pairing_id);
    cJSON *data = NULL;
    if (!api_json(path, HTTP_METHOD_POST, "{}", true, &data)) { update(H2H_CLOUD_ERROR, "Confirm failed"); return; }
    pairing_id[0] = 0;
    if (xSemaphoreTake(state_lock, portMAX_DELAY) == pdTRUE) {
        state.bound = true; state.pair_url[0] = 0;
        state.state = H2H_CLOUD_BOUND; strcpy(state.message, "Account connected");
        xSemaphoreGive(state_lock);
    }
    nvs_handle_t handle;
    if (nvs_open("h2h_cloud", NVS_READWRITE, &handle) == ESP_OK) {
        (void)nvs_set_u8(handle, "bound", 1); (void)nvs_commit(handle); nvs_close(handle);
    }
    cJSON_Delete(data);
}
static void refresh_theme(void) {
    update(H2H_CLOUD_WORKING, "Downloading theme");
    cJSON *data = NULL;
    if (!api_json("/device/desired-theme", HTTP_METHOD_GET, NULL, true, &data)) {
        update(H2H_CLOUD_ERROR, "Theme request failed"); return;
    }
    char id[65], digest[65], path[128];
    h2h_theme_t theme = {0};
    bool ok = field(data, "id", id, sizeof(id));
    if (ok) for (size_t i=0; id[i]; ++i) {
        if (!((id[i]>='a' && id[i]<='z') || (id[i]>='0' && id[i]<='9') ||
              id[i]=='_' || id[i]=='-')) { ok=false; break; }
    }
    if (ok && strcmp(id, "classic") != 0) {
        const cJSON *size = cJSON_GetObjectItemCaseSensitive(data, "size");
        ok = field(data, "sha256", digest, sizeof(digest)) && cJSON_IsNumber(size) &&
             size->valueint == H2H_THEME_PACK_SIZE && strlen(id) <= 63;
        if (ok) {
            snprintf(path, sizeof(path), "/device/themes/%s/pack", id);
            reply_t pack;
            ok = request(path, HTTP_METHOD_GET, NULL, true, &pack) && pack.length == H2H_THEME_PACK_SIZE;
            if (ok) {
                unsigned char hash[32]; char computed[65];
                mbedtls_sha256(pack.body, pack.length, hash, 0);
                for (unsigned i=0;i<32;i++) snprintf(computed+i*2, 3, "%02x", hash[i]);
                ok = strcmp(computed, digest) == 0 && h2h_theme_parse(pack.body, pack.length, &theme);
            }
        }
    }
    cJSON_Delete(data);
    xEventGroupClearBits(events, THEME_DONE | THEME_SAVED);
    if (!ok || xQueueSend(themes, &theme, 0) != pdTRUE) { update(H2H_CLOUD_ERROR, "Theme verify failed"); return; }
    EventBits_t result = xEventGroupWaitBits(events, THEME_DONE, pdTRUE, pdFALSE, pdMS_TO_TICKS(8000));
    if (!(result & THEME_DONE) || !(result & THEME_SAVED)) { update(H2H_CLOUD_ERROR, "Theme save failed"); return; }
    snprintf(path, sizeof(path), "/device/applied");
    char payload[180];
    snprintf(payload, sizeof(payload), "{\"id\":\"%s\",\"sha256\":\"%s\"}", id,
             strcmp(id, "classic") == 0 ? "" : digest);
    cJSON *reported = NULL;
    if (api_json(path, HTTP_METHOD_POST, payload, true, &reported)) {
        cJSON_Delete(reported);
        update(H2H_CLOUD_BOUND, "Theme applied");
    } else update(H2H_CLOUD_ERROR, "Theme report failed");
}
static void worker(void *arg) {
    (void)arg;
    if (!wifi_init()) { update(H2H_CLOUD_ERROR, "Wi-Fi init failed"); vTaskDelete(NULL); }
    bool registered = false;
    if (ssid_saved[0] && wifi_connect(ssid_saved, password_saved)) registered = register_device();
    if (account_open && registered && !state.bound) begin_pairing();
    uint32_t next_poll = 0;
    for (;;) {
        command_t command;
        if (xQueueReceive(queue, &command, pdMS_TO_TICKS(500)) == pdTRUE) {
            switch (command.type) {
            case CMD_OPEN:
                account_open = true;
                if (xEventGroupGetBits(events) & IP_READY) registered = register_device();
                if (registered && !state.bound && !pairing_id[0]) begin_pairing();
                break;
            case CMD_CLOSE: account_open = false; break;
            case CMD_WIFI:
                if (wifi_connect(command.ssid, command.password)) {
                    if (!store_wifi(command.ssid, command.password)) update(H2H_CLOUD_ERROR, "Wi-Fi save failed");
                    registered = register_device();
                    if (registered && account_open && !state.bound) begin_pairing();
                }
                memset(&command, 0, sizeof(command));
                break;
            case CMD_CONFIRM: confirm_pairing(); break;
            case CMD_REFRESH:
                if (registered && (xEventGroupGetBits(events) & IP_READY)) refresh_theme();
                else update(H2H_CLOUD_OFFLINE, "Connect Wi-Fi first");
                break;
            case CMD_FORGET:
                allow_reconnect = false;
                (void)esp_wifi_disconnect();
                (void)esp_wifi_stop();
                wifi_started = false;
                {
                    nvs_handle_t handle;
                    if (nvs_open("h2h_cloud", NVS_READWRITE, &handle) == ESP_OK) {
                        (void)nvs_erase_key(handle, "ssid"); (void)nvs_erase_key(handle, "wifi_pass");
                        (void)nvs_commit(handle); nvs_close(handle);
                    }
                }
                ssid_saved[0] = password_saved[0] = 0;
                if (xSemaphoreTake(state_lock, portMAX_DELAY) == pdTRUE) {
                    state.wifi_ready = false; xSemaphoreGive(state_lock);
                }
                update(H2H_CLOUD_SETUP, "Wi-Fi cleared");
                break;
            }
        }
        if (account_open && pairing_id[0] && (uint32_t)(xTaskGetTickCount() - next_poll) >= pdMS_TO_TICKS(3000)) {
            poll_pairing(); next_poll = xTaskGetTickCount();
        }
    }
}
static bool enqueue(command_type_t type, const char *ssid, const char *password) {
    if (!queue) return false;
    command_t command = {.type = type};
    if (ssid) { if (strlen(ssid)>32) return false; strcpy(command.ssid, ssid); }
    if (password) { if (strlen(password)>64) return false; strcpy(command.password, password); }
    bool result = xQueueSend(queue, &command, 0) == pdTRUE;
    memset(&command, 0, sizeof(command));
    return result;
}
esp_err_t h2h_cloud_start(void) {
    state_lock = xSemaphoreCreateMutex();
    events = xEventGroupCreate();
    queue = xQueueCreate(4, sizeof(command_t));
    themes = xQueueCreate(1, sizeof(h2h_theme_t));
    if (!state_lock || !events || !queue || !themes) return ESP_ERR_NO_MEM;
    memset(&state, 0, sizeof(state));
    state.state = H2H_CLOUD_SETUP;
    strcpy(state.message, "Set up Wi-Fi with phone");
    if (!load_identity()) return ESP_FAIL;
    nvs_handle_t handle;
    if (nvs_open("h2h_cloud", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t bound = 0; (void)nvs_get_u8(handle, "bound", &bound);
        state.bound = bound != 0; nvs_close(handle);
    }
    return xTaskCreate(worker, "h2h_cloud", 8192, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
bool h2h_cloud_wifi(const char *ssid, const char *password) { return ssid && password && enqueue(CMD_WIFI, ssid, password); }
bool h2h_cloud_open(void) { return enqueue(CMD_OPEN, NULL, NULL); }
bool h2h_cloud_close(void) { return enqueue(CMD_CLOSE, NULL, NULL); }
bool h2h_cloud_confirm(void) { return enqueue(CMD_CONFIRM, NULL, NULL); }
bool h2h_cloud_refresh(void) { return enqueue(CMD_REFRESH, NULL, NULL); }
bool h2h_cloud_forget_wifi(void) { return enqueue(CMD_FORGET, NULL, NULL); }
bool h2h_cloud_poll_theme(h2h_theme_t *out) { return out && themes && xQueueReceive(themes, out, 0) == pdTRUE; }
void h2h_cloud_theme_applied(bool persisted) {
    if (events) xEventGroupSetBits(events, THEME_DONE | (persisted ? THEME_SAVED : 0));
}
