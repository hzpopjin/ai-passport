#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;typedef void *QueueHandle_t;typedef void *SemaphoreHandle_t;typedef void *TaskHandle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_INVALID_ARG -4
#define ESP_ERR_TIMEOUT -5
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY UINT32_MAX
#define configMAX_PRIORITIES 25
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 4096
#define CONFIG_BT_NIMBLE_PINNED_TO_CORE 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
#define MALLOC_CAP_8BIT 4
static inline void relay_test_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
#define ESP_LOGI(...) relay_test_log(__VA_ARGS__)
#define ESP_LOGW(...) relay_test_log(__VA_ARGS__)
#define ESP_LOGE(...) relay_test_log(__VA_ARGS__)
static inline size_t heap_caps_get_free_size(unsigned x){(void)x;return 40000;}
static inline size_t heap_caps_get_minimum_free_size(unsigned x){return heap_caps_get_free_size(x);}
static inline size_t heap_caps_get_largest_free_block(unsigned x){return heap_caps_get_free_size(x);}
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void *)1;}
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void){return (void *)1;}
static inline int xSemaphoreTake(void *x,unsigned y){(void)x;(void)y;return 1;}
static inline int xSemaphoreGive(void *x){(void)x;return 1;}
static inline void vSemaphoreDelete(void *x){(void)x;}
static inline void *xQueueCreate(unsigned n,size_t size){(void)n;(void)size;return (void *)1;}
static inline int xQueueSend(void *q,const void *p,unsigned n){(void)q;(void)p;(void)n;return 1;}
static inline int xQueueReceive(void *q,void *p,unsigned n){(void)q;(void)p;(void)n;return 0;}
static inline void vQueueDelete(void *x){(void)x;}
static inline void vTaskDelete(void *x){(void)x;}
static inline void vTaskSuspend(void *x){(void)x;}
static inline int xTaskCreatePinnedToCore(void (*f)(void *),const char *n,unsigned s,void *a,unsigned p,void **out,unsigned c){(void)f;(void)n;(void)s;(void)a;(void)p;(void)c;*out=(void *)1;return 1;}
static inline int xTaskCreate(void (*f)(void *),const char *n,unsigned s,void *a,unsigned p,void **out){return xTaskCreatePinnedToCore(f,n,s,a,p,out,0);}
static inline uint32_t esp_random(void){return 123456;}
typedef struct {uint8_t bytes[16];} ble_uuid_t;typedef struct {ble_uuid_t u;} ble_uuid128_t;
#define BLE_UUID128_INIT(...) {{ {__VA_ARGS__} }}
static inline int ble_uuid_cmp(const ble_uuid_t *a,const ble_uuid_t *b){return memcmp(a,b,sizeof(*a));}
#define BLE_GAP_EVENT_CONNECT 1
#define BLE_GAP_EVENT_DISCONNECT 2
#define BLE_GAP_EVENT_PASSKEY_ACTION 3
#define BLE_GAP_EVENT_ENC_CHANGE 4
#define BLE_GAP_EVENT_ADV_COMPLETE 5
#define BLE_GAP_EVENT_REPEAT_PAIRING 6
#define BLE_GAP_REPEAT_PAIRING_IGNORE 2
#define BLE_SM_IOACT_DISP 1
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2
#define BLE_HS_IO_DISPLAY_ONLY 1
#define BLE_HS_ADV_F_DISC_GEN 1
#define BLE_HS_ADV_F_BREDR_UNSUP 2
#define BLE_GAP_CONN_MODE_UND 1
#define BLE_GAP_DISC_MODE_GEN 1
#define BLE_HS_FOREVER 0
#define BLE_ATT_ERR_UNLIKELY 1
#define BLE_ATT_ERR_READ_NOT_PERMITTED 2
#define BLE_ATT_ERR_INSUFFICIENT_RES 3
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED 4
#define BLE_ATT_ERR_INSUFFICIENT_AUTHEN 5
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 6
#define BLE_ATT_ERR_VALUE_NOT_ALLOWED 7
#define BLE_GATT_ACCESS_OP_READ_CHR 1
#define BLE_GATT_ACCESS_OP_WRITE_CHR 2
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GATT_CHR_F_WRITE 1
#define BLE_GATT_CHR_F_WRITE_AUTHEN 2
#define BLE_GATT_CHR_F_READ 4
#define BLE_GATT_CHR_F_READ_AUTHEN 8
struct ble_gap_event {int type;struct {int status;uint16_t conn_handle;} connect;struct {int status;uint16_t conn_handle;} enc_change;struct {struct {int action;} params;uint16_t conn_handle;} passkey;};
struct ble_sm_io {int action;uint32_t passkey;};
struct ble_gap_conn_desc {struct {bool encrypted,authenticated;} sec_state;};
struct ble_hs_adv_fields {int flags;const ble_uuid128_t *uuids128;int num_uuids128,uuids128_is_complete;uint8_t *name;size_t name_len;int name_is_complete;};
struct ble_gap_adv_params {int conn_mode,disc_mode;};
struct os_mbuf {uint8_t bytes[5000];size_t length;};
struct ble_gatt_chr_def;struct ble_gatt_access_ctxt {int op;const struct ble_gatt_chr_def *chr;struct os_mbuf *om;};
struct ble_gatt_chr_def {const ble_uuid_t *uuid;int (*access_cb)(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);int flags;};
struct ble_gatt_svc_def {int type;const ble_uuid_t *uuid;struct ble_gatt_chr_def *characteristics;};
struct ble_store_status_event {int event_code;};
static struct {void (*reset_cb)(int);void (*sync_cb)(void);int sm_io_cap,sm_mitm,sm_sc,sm_bonding,sm_our_key_dist,sm_their_key_dist;int (*store_status_cb)(struct ble_store_status_event *,void *);} ble_hs_cfg;
static unsigned test_store_initializations,test_passkey_injections,test_security_requests;
void ble_store_config_init(void){++test_store_initializations;}
static inline int ble_store_util_status_rr(struct ble_store_status_event *e,void *arg){(void)e;(void)arg;return 0;}
static bool test_authenticated=true;
static inline int ble_gap_conn_find(uint16_t h,struct ble_gap_conn_desc *d){(void)h;d->sec_state.encrypted=test_authenticated;d->sec_state.authenticated=test_authenticated;return 0;}
static inline int ble_gap_security_initiate(uint16_t h){(void)h;++test_security_requests;return 0;}
static inline int ble_sm_inject_io(uint16_t h,struct ble_sm_io *io){(void)h;(void)io;++test_passkey_injections;return 0;}
static inline int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *f){(void)f;return 0;}
static inline int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *f){(void)f;return 0;}
static inline int ble_gap_adv_start(uint8_t a,void *b,unsigned c,const struct ble_gap_adv_params *d,int (*e)(struct ble_gap_event *,void *),void *f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 0;}
static inline int ble_gap_adv_stop(void){return 0;}
static inline int ble_hs_id_infer_auto(int a,uint8_t *b){(void)a;*b=0;return 0;}
static inline int os_mbuf_append(struct os_mbuf *o,const void *p,size_t n){if(n+o->length>sizeof(o->bytes))return 1;memcpy(o->bytes+o->length,p,n);o->length+=n;return 0;}
#define OS_MBUF_PKTLEN(o) ((o)->length)
static inline int ble_hs_mbuf_to_flat(struct os_mbuf *o,void *p,size_t n,void *l){(void)l;if(n<o->length)return 1;memcpy(p,o->bytes,o->length);return 0;}
static inline void ble_svc_gap_init(void){}
static inline void ble_svc_gatt_init(void){}
static inline int ble_svc_gap_device_name_set(const char *n){(void)n;return 0;}
static inline int ble_gatts_count_cfg(const struct ble_gatt_svc_def *s){(void)s;return 0;}
static inline int ble_gatts_add_svcs(const struct ble_gatt_svc_def *s){(void)s;return 0;}
static inline int nimble_port_init(void){return 0;}
static inline int nimble_port_deinit(void){return 0;}
static inline int nimble_port_stop(void){return 0;}
static inline void nimble_port_run(void){}
