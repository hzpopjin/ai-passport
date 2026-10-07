#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/types.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -4
#define ESP_ERR_TIMEOUT -5
#define ESP_ERR_NVS_NOT_FOUND -3
#define NVS_READWRITE 1
#define NVS_READONLY 0
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(v) (v)
#define portMAX_DELAY UINT32_MAX
#define BIT0 1u
#define WIFI_EVENT ((void *)1)
#define IP_EVENT ((void *)2)
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define IP_EVENT_STA_GOT_IP 3
#define ESP_EVENT_ANY_ID -1
#define WIFI_MODE_STA 1
#define WIFI_STORAGE_RAM 1
typedef void esp_netif_t;
#define WIFI_IF_STA 1
#define WIFI_AUTH_OPEN 0
#define SNTP_OPMODE_POLL 0
#define HTTP_EVENT_ON_DATA 1
#define HTTP_EVENT_ON_HEADER 2
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_DMA 4
static inline void pocket_test_log(const char *tag,const char *format,...) { (void)tag;(void)format; }
#define ESP_LOGI(...) pocket_test_log(__VA_ARGS__)
#define ESP_LOGW(...) pocket_test_log(__VA_ARGS__)
#define ESP_LOGE(...) pocket_test_log(__VA_ARGS__)
typedef uint32_t TickType_t;
typedef uint32_t EventBits_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef uint32_t *EventGroupHandle_t;
typedef void *esp_event_base_t;
typedef unsigned nvs_handle_t;
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct { struct { uint8_t ssid[32],password[64]; struct { int authmode; } threshold; struct { bool capable; } pmf_cfg; } sta; } wifi_config_t;
typedef enum { HTTP_METHOD_GET, HTTP_METHOD_POST } esp_http_client_method_t;
typedef struct { int event_id,data_len; const char *data; void *user_data; const char *header_key,*header_value; } esp_http_client_event_t;
typedef struct {
    const char *url; esp_http_client_method_t method; int timeout_ms;
    esp_err_t (*crt_bundle_attach)(void *);
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data; bool disable_auto_redirect;
} esp_http_client_config_t;
typedef struct host_http *esp_http_client_handle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,uint32_t);
int xSemaphoreGive(SemaphoreHandle_t);
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueSend(QueueHandle_t,const void *,uint32_t);
int xQueueReceive(QueueHandle_t,void *,uint32_t);
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupGetBits(EventGroupHandle_t);
EventBits_t xEventGroupSetBits(EventGroupHandle_t,EventBits_t);
EventBits_t xEventGroupClearBits(EventGroupHandle_t,EventBits_t);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t,EventBits_t,int,int,uint32_t);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);
void vTaskDelete(void *);
void vTaskDelay(uint32_t);
TickType_t xTaskGetTickCount(void);
void esp_fill_random(void *,size_t);
uint32_t esp_random(void);
const char *esp_err_to_name(esp_err_t);
esp_err_t nvs_open(const char *,int,nvs_handle_t *);
esp_err_t nvs_get_str(nvs_handle_t,const char *,char *,size_t *);
esp_err_t nvs_set_str(nvs_handle_t,const char *,const char *);
esp_err_t nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
esp_err_t nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_erase_key(nvs_handle_t,const char *);
void nvs_close(nvs_handle_t);
esp_err_t nvs_flash_init(void);
esp_err_t esp_netif_init(void);
void *esp_netif_create_default_wifi_sta(void);
void esp_netif_destroy_default_wifi(void *);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t,int,void (*)(void *,esp_event_base_t,int32_t,void *),void *);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_event_handler_unregister(esp_event_base_t,int,void (*)(void *,esp_event_base_t,int32_t,void *));
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_set_config(int,const wifi_config_t *);
bool esp_sntp_enabled(void);
void esp_sntp_setoperatingmode(int);
void esp_sntp_setservername(int,const char *);
void esp_sntp_init(void);
esp_err_t esp_crt_bundle_attach(void *);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t,const char *,int);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
void esp_http_client_cleanup(esp_http_client_handle_t);
int mbedtls_sha256(const unsigned char *,size_t,unsigned char [32],int);
int mbedtls_base64_encode(unsigned char *,size_t,size_t *,const unsigned char *,size_t);
int mbedtls_base64_decode(unsigned char *,size_t,size_t *,const unsigned char *,size_t);
int64_t esp_timer_get_time(void);
size_t esp_get_free_heap_size(void);
size_t heap_caps_get_largest_free_block(unsigned);
size_t heap_caps_get_free_size(unsigned);
size_t heap_caps_get_minimum_free_size(unsigned);
typedef enum { BSP_BTN_UP,BSP_BTN_DOWN,BSP_BTN_OK } bsp_btn_t;
typedef enum { BSP_BTN_PRESS,BSP_BTN_CLICK,BSP_BTN_DOUBLE,BSP_BTN_LONG,BSP_BTN_TRIPLE } bsp_btn_ev_t;
typedef void (*bsp_btn_cb_t)(bsp_btn_t,bsp_btn_ev_t,void *);
esp_err_t bsp_button_init(bsp_btn_cb_t,void *);
int bsp_button_read_key(void);
typedef struct {int key;int64_t sampled_us,changed_us;uint64_t sequence;} bsp_button_snapshot_t;
bool bsp_button_snapshot(bsp_button_snapshot_t *);
esp_err_t bsp_display_init(void);
bool bsp_lvgl_init(void);
void bsp_display_backlight(uint8_t);
bool bsp_lvgl_lock(unsigned);
void bsp_lvgl_unlock(void);
esp_err_t bsp_battery_init(void);
int bsp_battery_soc(void);

typedef struct { unsigned address,size; int type,subtype; bool encrypted,readonly; } esp_partition_t;
typedef unsigned esp_partition_mmap_handle_t;
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_APP_FACTORY 0
#define ESP_PARTITION_MMAP_DATA 0
const esp_partition_t *esp_partition_find_first(int,int,const char *);
esp_err_t esp_partition_read(const esp_partition_t *,size_t,void *,size_t);
esp_err_t esp_partition_write(const esp_partition_t *,size_t,const void *,size_t);
esp_err_t esp_partition_erase_range(const esp_partition_t *,size_t,size_t);
esp_err_t esp_partition_mmap(const esp_partition_t *,size_t,size_t,int,const void **,esp_partition_mmap_handle_t *);
void esp_partition_munmap(esp_partition_mmap_handle_t);
typedef struct { void *ctx; } mbedtls_sha256_context;
void mbedtls_sha256_init(mbedtls_sha256_context *);
void mbedtls_sha256_free(mbedtls_sha256_context *);
int mbedtls_sha256_starts(mbedtls_sha256_context *,int);
int mbedtls_sha256_update(mbedtls_sha256_context *,const unsigned char *,size_t);
int mbedtls_sha256_finish(mbedtls_sha256_context *,unsigned char [32]);

#define portTICK_PERIOD_MS 1
#define MBEDTLS_MD_SHA256 1
typedef int mbedtls_md_info_t;
const mbedtls_md_info_t *mbedtls_md_info_from_type(int);
int mbedtls_md_hmac(const mbedtls_md_info_t *,const unsigned char *,size_t,const unsigned char *,size_t,unsigned char *);
typedef struct esp_tls esp_tls_t;
typedef struct {bool non_block;int timeout_ms;bool skip_common_name;esp_err_t (*crt_bundle_attach)(void *);} esp_tls_cfg_t;
#define ESP_TLS_ERR_SSL_WANT_READ (-0x6900)
#define ESP_TLS_ERR_SSL_WANT_WRITE (-0x6880)
esp_tls_t *esp_tls_init(void);
int esp_tls_conn_new_async(const char *,int,int,const esp_tls_cfg_t *,esp_tls_t *);
ssize_t esp_tls_conn_read(esp_tls_t *,void *,size_t);
ssize_t esp_tls_conn_write(esp_tls_t *,const void *,size_t);
int esp_tls_conn_destroy(esp_tls_t *);
