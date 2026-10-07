#define _POSIX_C_SOURCE 200809L
#include "pocket_sdk.h"
#include "cJSON.h"
#include "h2h_tracks.h"
#include "stayc_game_store.h"
#include "h2h_audio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <setjmp.h>
#include "../main/h2h/h2h_cloud.c"

const h2h_track_t h2h_tracks[]={{.duration_ms=120000},{.duration_ms=120000}};
const unsigned h2h_track_count=2,h2h_song_count=1;
static unsigned char disk[H2H_POCKET_RECORD_SIZE], staged[H2H_POCKET_RECORD_SIZE];
static bool disk_exists, staged_exists, nvs_fail;
static uint8_t full_disk[3][1024],full_staged[3][1024];
static bool full_exists[3],full_staged_exists[3];
static int full_index(const char *key){return !strcmp(key,"full_a")?0:!strcmp(key,"full_b")?1:!strcmp(key,"full_ptr")?2:-1;}
static unsigned char stayc_preferences_disk[24],stayc_preferences_staged[24];
static bool stayc_preferences_exists,stayc_preferences_staged_exists;
static unsigned stayc_preference_writes;
static unsigned char gameplay_disk[STAYC_GAME_RECORD_BYTES],gameplay_staged[STAYC_GAME_RECORD_BYTES],legacy_gameplay_disk[STAYC_GAME_RECORD_BYTES];
static bool legacy_gameplay_exists;
static bool gameplay_disk_exists,gameplay_staged_exists;
static unsigned gameplay_writes;
static unsigned main_scenario;
static unsigned local_audio_requests,debug_real_writes;
static bool debug_reboot, cancel_on_disconnect, waiting_ui;
static bool corrupt_after_gameplay_commit,audio_override;
static h2h_audio_status_t injected_audio;
static void main_scenario_tick(void);
static void gameplay_post_commit_hook(void);
static bool legacy_gameplay_fixture(const void *bytes,size_t size,void *ctx){(void)ctx;nvs_handle_t h;assert(nvs_open("stayc_game",NVS_READWRITE,&h)==ESP_OK);return nvs_set_blob(h,"progress",bytes,size)==ESP_OK&&nvs_commit(h)==ESP_OK;}
static unsigned char resource_disk[H2H_RESOURCE_STATE_BYTES],resource_staged[H2H_RESOURCE_STATE_BYTES];
static bool resource_disk_exists,resource_staged_exists,resource_layout,resource_map_fail;
static uint8_t resume_disk[160],resume_staged[160];static bool resume_exists,resume_staged_exists;
static unsigned char resource_slots[2][H2H_RESOURCE_SLOT_BYTES],fixture_public_key[32];
static bool fixture_key_exists;
static unsigned mapped_references[2];
static char displayed_resource[33];
static uint32_t ticks,random_counter=1;
static bool bridge;
static bool main_mode, main_activated, main_revoked;
static unsigned audio_requests, ui_frames, writes;
static jmp_buf main_exit, worker_idle;
static bool observe_worker;
struct host_queue { size_t size; unsigned count,limit; unsigned char values[16][256]; };
struct host_http { esp_http_client_config_t config; char *url,*payload; char device[33],token[65],range[64]; int status; };
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t handle,uint32_t timeout) { (void)timeout; return handle!=NULL; }
int xSemaphoreGive(SemaphoreHandle_t handle) { return handle!=NULL; }
QueueHandle_t xQueueCreate(unsigned limit,size_t size) { struct host_queue *q=calloc(1,sizeof(*q)); assert(q && limit<=16 && size<=256);q->limit=limit;q->size=size;return q; }
int xQueueSend(QueueHandle_t handle,const void *value,uint32_t timeout) { (void)timeout;struct host_queue *q=handle;if(q->count>=q->limit)return 0;memcpy(q->values[q->count++],value,q->size);return 1; }
int xQueueReceive(QueueHandle_t handle,void *value,uint32_t timeout) {
 struct host_queue *q=handle;
 if(observe_worker && timeout>0 && !q->count)longjmp(worker_idle,1);
 if(q->count) { memcpy(value,q->values[0],q->size);memmove(q->values,q->values+1,--q->count*256);return 1; }
 return 0;
}
EventGroupHandle_t xEventGroupCreate(void) { return calloc(1,sizeof(uint32_t)); }
EventBits_t xEventGroupGetBits(EventGroupHandle_t e) { return *e; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t e,EventBits_t b) { return *e|=b; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t e,EventBits_t b) { return *e&=~b; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t e,EventBits_t b,int clear,int all,uint32_t timeout) { (void)all;(void)timeout;uint32_t v=*e&b;if(clear)*e&=~b;return v; }
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *out) { (void)task;(void)name;(void)stack;(void)arg;(void)priority;(void)out;return pdPASS; }
void vTaskDelete(void *task) { (void)task; }
void vTaskDelay(uint32_t delay) {
 ticks+=delay;
 if(observe_worker && debug_paused)longjmp(worker_idle,1);
 if(main_mode && main_scenario){main_scenario_tick();if(ticks>=(main_scenario==8?37000u:main_scenario>=5?7000u:1000u))longjmp(main_exit,1);return;}
 if(main_mode && ticks>=2000) {
  if(main_activated && !main_revoked) {
   h2h_pocket_record_t sent=pocket.record;h2h_pocket_reply_t reply={.epoch=2};
   assert(h2h_pocket_reply(&pocket,&sent,&reply,write_record,NULL));main_revoked=true;
  }
  if(!main_activated || ticks>=3000) longjmp(main_exit,1);
 }
}
TickType_t xTaskGetTickCount(void) { return ticks; }
void esp_fill_random(void *out,size_t length) { unsigned char *p=out;for(size_t i=0;i<length;i++)p[i]=(unsigned char)(random_counter++); }
uint32_t esp_random(void) { return random_counter++; }
const char *esp_err_to_name(esp_err_t e) { (void)e;return "test_error"; }
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out) { (void)mode;*out=!strcmp(name,"h2h_resources")?2:!strcmp(name,"stayc_ui")?3:!strcmp(name,"stayc_game")?4:1;return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size) {
 (void)h;const char *value=!strcmp(key,"id")?"11111111111111111111111111111111":!strcmp(key,"secret")?"abababababababababababababababababababababababababababababababab":NULL;
 if(!value)return ESP_ERR_NVS_NOT_FOUND;
 if(*size<strlen(value)+1)return ESP_FAIL;
 strcpy(out,value);*size=strlen(value)+1;return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *v) { (void)h;(void)key;(void)v;return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *length) {
 if(h==4){assert(!strcmp(key,"progress"));if(!legacy_gameplay_exists)return ESP_ERR_NVS_NOT_FOUND;if(*length<sizeof(gameplay_disk))return ESP_FAIL;memcpy(out,legacy_gameplay_disk,sizeof(gameplay_disk));*length=sizeof(gameplay_disk);return ESP_OK;}
 if(h==3){assert(!strcmp(key,"selection"));if(!stayc_preferences_exists)return ESP_ERR_NVS_NOT_FOUND;if(*length<sizeof(stayc_preferences_disk))return ESP_FAIL;memcpy(out,stayc_preferences_disk,sizeof(stayc_preferences_disk));*length=sizeof(stayc_preferences_disk);return ESP_OK;}
 if(h==2){if(!strcmp(key,"resume_v2")){if(!resume_exists)return ESP_ERR_NVS_NOT_FOUND;if(*length<160)return ESP_FAIL;memcpy(out,resume_disk,160);*length=160;return ESP_OK;}assert(!strcmp(key,"visual_v2"));if(!resource_disk_exists)return ESP_ERR_NVS_NOT_FOUND;if(*length<sizeof(resource_disk))return ESP_FAIL;memcpy(out,resource_disk,sizeof(resource_disk));*length=sizeof(resource_disk);return ESP_OK;}
 int fi=full_index(key);if(fi>=0){size_t n=fi==2?32:1024;if(!full_exists[fi])return ESP_ERR_NVS_NOT_FOUND;if(*length<n)return ESP_FAIL;memcpy(out,full_disk[fi],n);*length=n;return ESP_OK;}
 if(!strcmp(key,"relay_key"))return ESP_ERR_NVS_NOT_FOUND;
 assert(!strcmp(key,"pocket_v2"));if(!disk_exists)return ESP_ERR_NVS_NOT_FOUND;if(*length<sizeof(disk))return ESP_FAIL;memcpy(out,disk,sizeof(disk));*length=sizeof(disk);return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *value,size_t length) {
 if(h==4){assert(!strcmp(key,"progress")&&length==sizeof(gameplay_staged));memcpy(gameplay_staged,value,length);gameplay_staged_exists=true;return ESP_OK;}
 if(h==3){assert(!strcmp(key,"selection")&&length==sizeof(stayc_preferences_staged));memcpy(stayc_preferences_staged,value,length);stayc_preferences_staged_exists=true;return ESP_OK;}
 if(h==2){if(!strcmp(key,"resume_v2")){assert(length==160);memcpy(resume_staged,value,160);resume_staged_exists=true;return ESP_OK;}assert(!strcmp(key,"visual_v2")&&length==sizeof(resource_staged));memcpy(resource_staged,value,length);resource_staged_exists=true;return ESP_OK;}
 int fi=full_index(key);if(fi>=0){assert(length==(fi==2?32u:1024u));memcpy(full_staged[fi],value,length);full_staged_exists[fi]=true;return ESP_OK;}
 assert(!strcmp(key,"pocket_v2")&&length==sizeof(staged));memcpy(staged,value,length);staged_exists=true;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
 if(nvs_fail){memset(full_staged_exists,0,sizeof(full_staged_exists));resume_staged_exists=false;staged_exists=resource_staged_exists=stayc_preferences_staged_exists=gameplay_staged_exists=false;return ESP_FAIL;}
 if(h==4){if(gameplay_staged_exists){memcpy(gameplay_disk,gameplay_staged,sizeof(gameplay_disk));memcpy(legacy_gameplay_disk,gameplay_staged,sizeof(gameplay_disk));legacy_gameplay_exists=gameplay_disk_exists=true;gameplay_staged_exists=false;gameplay_writes++;if(corrupt_after_gameplay_commit){corrupt_after_gameplay_commit=false;gameplay_post_commit_hook();}}return ESP_OK;}
 if(h==3){if(stayc_preferences_staged_exists){memcpy(stayc_preferences_disk,stayc_preferences_staged,sizeof(stayc_preferences_disk));stayc_preferences_exists=true;stayc_preferences_staged_exists=false;stayc_preference_writes++;}return ESP_OK;}
 if(h==2){if(resume_staged_exists){memcpy(resume_disk,resume_staged,160);resume_exists=true;resume_staged_exists=false;}if(resource_staged_exists){memcpy(resource_disk,resource_staged,sizeof(resource_disk));resource_disk_exists=true;resource_staged_exists=false;}return ESP_OK;}
 if(full_staged_exists[2]){
  unsigned bank=full_staged[2][8];assert(bank<2);
  if((full_disk[bank][32]&1u)&&(!gameplay_disk_exists||memcmp(gameplay_disk,full_disk[bank]+424,164))){
   /* Observed current gameplay for existing app assertions; journal banks are
    * the authoritative durable bytes. This is not a legacy namespace write. */
   memcpy(gameplay_disk,full_disk[bank]+424,164);gameplay_disk_exists=true;gameplay_writes++;
  }
 }
 for(unsigned i=0;i<3;i++)if(full_staged_exists[i]){memcpy(full_disk[i],full_staged[i],i==2?32:1024);full_exists[i]=true;full_staged_exists[i]=false;if(i==2){writes++;if(corrupt_after_gameplay_commit){corrupt_after_gameplay_commit=false;gameplay_post_commit_hook();}}}
 if(staged_exists){memcpy(disk,staged,sizeof(disk));disk_exists=true;staged_exists=false;writes++;}return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key) { (void)h;(void)key;return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
static unsigned wifi_inits, netif_creates, netif_deletes, handler_count;
static bool fail_wifi_init, fail_ip_handler, fail_storage;
esp_err_t esp_netif_init(void) { return ESP_OK; }
void *esp_netif_create_default_wifi_sta(void) { ++netif_creates;return (void *)1; }
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_err_t esp_event_handler_register(esp_event_base_t b,int id,void (*fn)(void *,esp_event_base_t,int32_t,void *),void *arg) {
 (void)id;(void)fn;(void)arg;if(b==IP_EVENT && fail_ip_handler)return ESP_FAIL;++handler_count;return ESP_OK;
}
esp_err_t esp_wifi_init(const wifi_init_config_t *c) { (void)c;++wifi_inits;return fail_wifi_init?ESP_ERR_NO_MEM:ESP_OK; }
esp_err_t esp_wifi_set_mode(int mode) { (void)mode;return ESP_OK; }
esp_err_t esp_wifi_deinit(void) { return ESP_OK; }
esp_err_t esp_wifi_set_storage(int storage) { assert(storage==WIFI_STORAGE_RAM);return fail_storage?ESP_FAIL:ESP_OK; }
esp_err_t esp_event_handler_unregister(esp_event_base_t b,int id,void (*fn)(void *,esp_event_base_t,int32_t,void *)) { (void)b;(void)id;(void)fn;assert(handler_count);--handler_count;return ESP_OK; }
void esp_netif_destroy_default_wifi(void *netif) { assert(netif==(void *)1 && !wifi_initialized && !handler_count);++netif_deletes; }
esp_err_t esp_wifi_connect(void) { return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) {
 if(cancel_on_disconnect){cancel_on_disconnect=false;(void)h2h_cloud_debug_pause(false);}
 return ESP_OK;
}
esp_err_t esp_wifi_stop(void) { return ESP_OK; }
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_wifi_set_config(int mode,const wifi_config_t *c) { (void)mode;(void)c;return ESP_OK; }
bool esp_sntp_enabled(void) { return true; }
void esp_sntp_setoperatingmode(int mode) { (void)mode; }
void esp_sntp_setservername(int n,const char *s) { (void)n;(void)s; }
void esp_sntp_init(void) { }
esp_err_t esp_crt_bundle_attach(void *config) { (void)config;return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) { struct host_http *h=calloc(1,sizeof(*h));h->config=*config;h->url=strdup(config->url);return h; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *key,const char *value) {
 if(!strcmp(key,"X-Passport-Device"))snprintf(h->device,sizeof(h->device),"%s",value);
 if(!strcmp(key,"X-Passport-Token"))snprintf(h->token,sizeof(h->token),"%s",value);
 if(!strcmp(key,"Range"))snprintf(h->range,sizeof(h->range),"%s",value);
 assert(strcmp(key,"Authorization")!=0);return ESP_OK;
}
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t h,const char *payload,int length) { h->payload=strndup(payload,(size_t)length);return ESP_OK; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t h) {
 assert(bridge);
 cJSON *request=cJSON_CreateObject();cJSON_AddStringToObject(request,"http",h->url+strlen(API));
 cJSON_AddStringToObject(request,"method",h->config.method==HTTP_METHOD_GET?"GET":"POST");
 cJSON_AddBoolToObject(request,"auth_valid",!strcmp(h->device,"11111111111111111111111111111111") &&
   !strcmp(h->token,"abababababababababababababababababababababababababababababababab"));
 cJSON_AddItemToObject(request,"body",h->payload?cJSON_Parse(h->payload):cJSON_CreateObject());
 if(h->range[0]){cJSON_AddBoolToObject(request,"binary",true);cJSON_AddStringToObject(request,"range",h->range);}
 char *encoded=cJSON_PrintUnformatted(request);puts(encoded);fflush(stdout);free(encoded);cJSON_Delete(request);
 char *line=NULL;size_t cap=0;assert(getline(&line,&cap,stdin)>0);cJSON *response=cJSON_Parse(line);free(line);assert(response);
 h->status=cJSON_GetObjectItem(response,"status")->valueint;
 cJSON *body=cJSON_GetObjectItem(response,"body");
 if(h->range[0]&&h->status==206){
  const char *range=cJSON_GetObjectItem(body,"content_range")->valuestring,*base64=cJSON_GetObjectItem(body,"base64")->valuestring;
  esp_http_client_event_t header={.event_id=HTTP_EVENT_ON_HEADER,.header_key="Content-Range",.header_value=range,.user_data=h->config.user_data};h->config.event_handler(&header);
  unsigned char bytes[2048];size_t n=0;assert(mbedtls_base64_decode(bytes,sizeof(bytes),&n,(const unsigned char *)base64,strlen(base64))==0);
  esp_http_client_event_t data={.event_id=HTTP_EVENT_ON_DATA,.data_len=(int)n,.data=(const char *)bytes,.user_data=h->config.user_data};h->config.event_handler(&data);cJSON_Delete(response);return ESP_OK;
 }
 encoded=cJSON_PrintUnformatted(body);
 esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.data_len=(int)strlen(encoded),.data=encoded,.user_data=h->config.user_data};
 h->config.event_handler(&event);free(encoded);cJSON_Delete(response);return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h) { return h->status; }
void esp_http_client_cleanup(esp_http_client_handle_t h) { free(h->url);free(h->payload);free(h); }
int mbedtls_sha256(const unsigned char *in,size_t length,unsigned char out[32],int sha224) { assert(!sha224);return SHA256(in,length,out)?0:-1; }
int mbedtls_base64_encode(unsigned char *out,size_t capacity,size_t *length,const unsigned char *in,size_t size) { if(capacity<4*((size+2)/3)+1)return -1;*length=(size_t)EVP_EncodeBlock(out,in,(int)size);return 0; }
int mbedtls_base64_decode(unsigned char *out,size_t capacity,size_t *length,const unsigned char *in,size_t size) { unsigned char tmp[4096];if(size>sizeof(tmp))return -1;int n=EVP_DecodeBlock(tmp,in,(int)size);if(n<0)return -1;if(size&&in[size-1]=='=')n--;if(size>1&&in[size-2]=='=')n--;if((size_t)n>capacity)return -1;memcpy(out,tmp,(size_t)n);*length=(size_t)n;return 0; }
void mbedtls_sha256_init(mbedtls_sha256_context *ctx){ctx->ctx=EVP_MD_CTX_new();assert(ctx->ctx);}
void mbedtls_sha256_free(mbedtls_sha256_context *ctx){EVP_MD_CTX_free(ctx->ctx);ctx->ctx=NULL;}
int mbedtls_sha256_starts(mbedtls_sha256_context *ctx,int sha224){assert(!sha224);return EVP_DigestInit_ex(ctx->ctx,EVP_sha256(),NULL)==1?0:-1;}
int mbedtls_sha256_update(mbedtls_sha256_context *ctx,const unsigned char *data,size_t length){return EVP_DigestUpdate(ctx->ctx,data,length)==1?0:-1;}
int mbedtls_sha256_finish(mbedtls_sha256_context *ctx,unsigned char out[32]){unsigned n;return EVP_DigestFinal_ex(ctx->ctx,out,&n)==1&&n==32?0:-1;}
bool h2h_resource_key_at(unsigned index,char id[33],unsigned char out[32]){if(index||!fixture_key_exists)return false;strcpy(id,"fixture");memcpy(out,fixture_public_key,32);return true;}
static const esp_partition_t host_partitions[]={{.address=0x10000,.size=0x770000},{.address=0x780000,.size=0x40000,.type=1,.subtype=0x40},{.address=0x7c0000,.size=0x40000,.type=1,.subtype=0x40}};
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *name){(void)type;(void)subtype;if(!resource_layout)return NULL;return !strcmp(name,"factory")?&host_partitions[0]:!strcmp(name,"pocket_a")?&host_partitions[1]:!strcmp(name,"pocket_b")?&host_partitions[2]:NULL;}
static int slot_index(const esp_partition_t *p){assert(p==&host_partitions[1]||p==&host_partitions[2]);return p==&host_partitions[1]?0:1;}
esp_err_t esp_partition_read(const esp_partition_t *p,size_t offset,void *out,size_t length){int slot=slot_index(p);assert(offset+length<=p->size);memcpy(out,resource_slots[slot]+offset,length);return ESP_OK;}
esp_err_t esp_partition_write(const esp_partition_t *p,size_t offset,const void *bytes,size_t length){int slot=slot_index(p);assert(!mapped_references[slot]&&offset+length<=p->size);memcpy(resource_slots[slot]+offset,bytes,length);return ESP_OK;}
esp_err_t esp_partition_erase_range(const esp_partition_t *p,size_t offset,size_t length){int slot=slot_index(p);assert(!mapped_references[slot]&&offset+length<=p->size);memset(resource_slots[slot]+offset,0xff,length);return ESP_OK;}
esp_err_t esp_partition_mmap(const esp_partition_t *p,size_t offset,size_t length,int memory,const void **out,esp_partition_mmap_handle_t *handle){(void)memory;if(resource_map_fail)return ESP_FAIL;int slot=slot_index(p);assert(offset+length<=p->size);*out=resource_slots[slot]+offset;*handle=(unsigned)slot+1;mapped_references[slot]++;return ESP_OK;}
void esp_partition_munmap(esp_partition_mmap_handle_t handle){assert(handle>=1&&handle<=2&&mapped_references[handle-1]);mapped_references[handle-1]--;}
static void print_state(bool ok) {
 printf("{\"done\":true,\"ok\":%s,\"play\":%s,\"epoch\":%llu,\"revision\":%llu,\"hearts\":%u,\"pending\":%s,\"healthy\":%s,\"conflict\":%s,\"restore_ack\":%s,\"recovery\":%s,\"recovery_hearts\":%u,\"resource\":\"%s\"}\n",ok?"true":"false",h2h_cloud_can_play()?"true":"false",(unsigned long long)pocket.record.ownership.epoch,(unsigned long long)pocket.record.revision,pocket.record.save.hearts,pocket.record.ownership.pending_unbind[0]?"true":"false",pocket.healthy?"true":"false",pocket.record.conflict?"true":"false",pocket.record.restore_ack?"true":"false",pocket.record.recovery?"true":"false",pocket.record.recovery_save.hearts,displayed_resource);fflush(stdout);
}
#define TAG main_TAG
size_t esp_get_minimum_free_heap_size(void){return 38000;}
unsigned uxTaskGetStackHighWaterMark(void *task){(void)task;return 4096;}
TaskHandle_t xTaskGetHandle(const char *name){(void)name;return NULL;}
#include "../main/main.c"
#undef TAG
int64_t esp_timer_get_time(void) { return (int64_t)ticks*1000; }
size_t esp_get_free_heap_size(void) { return 100000; }
size_t heap_caps_get_largest_free_block(unsigned flags) { (void)flags;return 100000; }
size_t heap_caps_get_free_size(unsigned flags) { (void)flags;return 100000; }
size_t heap_caps_get_minimum_free_size(unsigned flags) { (void)flags;return 38000; }
esp_err_t bsp_display_init(void) { return ESP_OK; }
bool bsp_lvgl_init(void) { return true; }
void bsp_display_backlight(uint8_t percent) { (void)percent; }
bool bsp_lvgl_lock(unsigned timeout) { (void)timeout;return true; }
void bsp_lvgl_unlock(void) { }
esp_err_t bsp_battery_init(void) { return ESP_OK; }
int bsp_battery_soc(void) { return 80; }
esp_err_t bsp_button_init(bsp_btn_cb_t callback,void *context) {
 if(main_scenario)return ESP_OK;
 // Try all event variants; only the main loop handles them.
 for(unsigned key=0;key<3;key++) for(unsigned event=0;event<5;event++) callback((bsp_btn_t)key,(bsp_btn_ev_t)event,context);
 return ESP_OK;
}
esp_err_t h2h_audio_start(void) { return ESP_OK; }
esp_err_t h2h_audio_stop(void) { return ESP_OK; }
int bsp_button_read_key(void) {
 if(debug_reboot || (main_scenario!=5 && main_scenario!=7 && main_scenario!=8))return -1;
 if(ticks>=200 && ticks<340)return 0;
 if(ticks>=500 && ticks<640)return 1;
 if(ticks>=800 && ticks<940)return 0;
 if(ticks>=1100 && ticks<1240)return 1;
 if(ticks>=1400 && ticks<4640)return 2;
 if(ticks>=4800 && ticks<4940)return 2;
 return -1;
}
bool bsp_button_snapshot(bsp_button_snapshot_t *out){
 static int old=-2;static uint64_t seq;static int64_t changed;
 int key=bsp_button_read_key();if(key!=old){old=key;seq++;changed=(int64_t)ticks*1000;}
 *out=(bsp_button_snapshot_t){.key=key,.sequence=seq,.changed_us=changed,.sampled_us=(int64_t)ticks*1000};return key>=-1;
}
void h2h_audio_request(const h2h_audio_request_t *request) {
 audio_requests++;
 if(!h2h_cloud_can_play())assert(!request->play || (gameplay.debug_mode && debug_paused));
 if(request->play && gameplay.debug_mode){assert(debug_paused);++local_audio_requests;}
}
h2h_audio_status_t h2h_audio_status(void) { return audio_override?injected_audio:(h2h_audio_status_t){.generation=1,.played_samples=(uint64_t)ticks*480,.ended=true}; }
void h2h_ui_render(const h2h_model_t *model,unsigned position,bool playing,int battery,bool storage,bool input,unsigned time,const h2h_cloud_status_t *cloud,bool ble,int passkey,const stayc_control_t *visuals) {
 (void)position;(void)playing;(void)battery;(void)storage;(void)input;(void)time;(void)cloud;(void)ble;(void)passkey;(void)visuals;ui_frames++;
 if(waiting_ui)assert(model->page==H2H_ACCOUNT && !model->play_allowed);
 if(!h2h_cloud_can_play()){assert(model->page==H2H_ACCOUNT);assert(!model->want_play && !model->countdown_ms);}
 // This adapter reports stale-generation PCM; it must never reward the new model.
 assert(model->save.listen_ms==0 && model->save.hearts==0);
}
void h2h_ui_reset(void){}
void h2h_ui_setup_state(bool diagnostics,bool confirm,int error,bool waiting){(void)diagnostics;(void)confirm;(void)error;waiting_ui=waiting;}
void stayc_game_ui_reset(void){}
void stayc_game_ui_phone_connection(bool connected){assert(connected==h2h_ble_connected());}
bool stayc_game_ui_input(stayc_game_t *m,stayc_control_t *s,h2h_key_t key,h2h_event_t event){(void)m;(void)s;(void)key;(void)event;return false;}
void stayc_game_ui_tick(stayc_game_t *m,stayc_control_t *s,uint32_t ms){
 (void)ms;
 if(main_scenario>=5 && m->debug_mode && ticks>=(main_scenario==6?800u:6000u)) {
  s->member=5;s->decor=255;s->preferences_dirty=true;
 }
}
void stayc_game_ui_render(const stayc_game_t *model,unsigned position,bool playing,int battery,bool storage,bool blocked,bool input,unsigned ticks,const stayc_control_t *visuals){
 (void)position;(void)playing;(void)battery;(void)storage;(void)blocked;(void)input;(void)ticks;(void)visuals;ui_frames++;
 if(model->debug_mode){assert(debug_paused);assert(!displayed_resource[0]);}
 else {assert(h2h_cloud_can_play());assert(!model->save.listen_ms&&!model->save.hearts);assert(!visuals->member&&!visuals->decor);}
}
static void gameplay_post_commit_hook(void){stayc_game_input(&gameplay,STAYC_GAME_OK,STAYC_GAME_PRESS);stayc_game_input(&gameplay,STAYC_GAME_OK,STAYC_GAME_LONG);}
static void queued_click(bsp_btn_t key){key_callback(key,BSP_BTN_PRESS,NULL);key_callback(key,BSP_BTN_CLICK,NULL);}
static void start_purchase(void){
 gameplay.page=STAYC_GAME_WARDROBE;gameplay.selection=0;gameplay.shop_kind=STAYC_GAME_SHOP_OUTFIT;gameplay.shop_item=0;
 queued_click(BSP_BTN_DOWN);queued_click(BSP_BTN_OK);
}
static void confirm_purchase(void){queued_click(BSP_BTN_DOWN);queued_click(BSP_BTN_OK);}
static void main_scenario_tick(void){
 if(main_scenario>=5){
  /* Execute the real worker boundary; RTOS scheduling itself is a host stub. */
  if(main_scenario!=7 && (debug_requested || debug_paused))(void)debug_boundary();
  if(main_scenario==6){
   if(ticks==200){debug_real_writes=writes;gameplay.page=STAYC_GAME_SETTINGS;gameplay.selection=4;queued_click(BSP_BTN_OK);}
   if(ticks==400){assert(gameplay.debug_mode && debug_paused);gameplay.page=STAYC_GAME_PLAYER;gameplay.want_play=true;}
   if(ticks==800){gameplay.save.hearts=99999;gameplay.save.star_balance=999;gameplay.dirty=true;}
   if(ticks==1000){assert(writes==debug_real_writes && !gameplay_writes && !stayc_preference_writes);assert(!h2h_cloud_unbind(1,pocket.model_generation));}
   if(ticks==1200){gameplay.page=STAYC_GAME_SETTINGS;gameplay.selection=4;queued_click(BSP_BTN_OK);}
   if(ticks==1400){assert(!gameplay.debug_mode && !debug_paused);h2h_pocket_record_t sent=pocket.record;h2h_pocket_reply_t replace={.epoch=2,.account_id=8,.activated=true};assert(h2h_pocket_reply(&pocket,&sent,&replace,write_record,NULL));}
   if(ticks==1600){assert(pocket.record.ownership.account_id==8 && !gameplay.debug_mode && !gameplay.save.hearts && !gameplay.save.star_balance);}
  } else {
   if(ticks==5540){debug_real_writes=writes;for(unsigned i=0;i<5;i++)queued_click(BSP_BTN_DOWN);}
   if(ticks==5560)queued_click(BSP_BTN_OK);
   if(main_scenario==8){
    if(ticks==5620)assert(debug_requested && debug_paused && !gameplay.debug_mode && !local_audio_requests);
    if(ticks==35600)assert(!debug_requested && !debug_paused && !gameplay.debug_mode && !local_audio_requests);
   } else if(main_scenario==7){
    if(ticks==5580){assert(debug_requested && !debug_paused && !gameplay.debug_mode);queued_click(BSP_BTN_UP);}
    if(ticks==5620)assert(!debug_requested && !gameplay.debug_mode && !local_audio_requests);
   } else {
    if(ticks==5620){assert(gameplay.debug_mode && debug_paused && !h2h_cloud_can_play());gameplay.page=STAYC_GAME_PLAYER;gameplay.want_play=true;}
    if(ticks==6000){gameplay.save.hearts=99999;gameplay.save.star_balance=999;gameplay.dirty=true;}
    if(ticks==6180)assert(writes==debug_real_writes && !gameplay_writes && !stayc_preference_writes);
    if(ticks==6200){gameplay.page=STAYC_GAME_SETTINGS;gameplay.selection=4;queued_click(BSP_BTN_OK);}
    if(ticks==6240)assert(!gameplay.debug_mode && !debug_paused && !h2h_cloud_can_play() && !gameplay.save.hearts && !gameplay.save.star_balance);
   }
  }
  return;
 }
 if(main_scenario==1||main_scenario==2){
  if(ticks==200)start_purchase();
  if(ticks==220){if(main_scenario==1)nvs_fail=true;else corrupt_after_gameplay_commit=true;confirm_purchase();}
  if(ticks==300){
   if(main_scenario==1){assert(gameplay.shop_status==STAYC_GAME_SHOP_FAILED&&gameplay.save.star_balance==100&&gameplay.save.owned_outfits==1&&!gameplay.save.outfit&&gameplay_writes==1);nvs_fail=false;queued_click(BSP_BTN_OK);}
   else assert(gameplay_store.blocked&&gameplay_writes==2&&gameplay.save.star_balance==100&&gameplay.save.owned_outfits==1);
  }
  if(ticks==320&&main_scenario==1)confirm_purchase();
  if(ticks==400&&main_scenario==1)assert(gameplay.shop_status==STAYC_GAME_SHOP_SUCCESS&&gameplay.save.star_balance==76&&gameplay.save.owned_outfits==3&&gameplay.save.outfit==1&&gameplay_writes==2);
 } else if(main_scenario==3){
  if(ticks==200){assert(gameplay_store.blocked);gameplay.page=STAYC_GAME_WARDROBE;gameplay.selection=gameplay.shop_item=0;queued_click(BSP_BTN_OK);}
  if(ticks==300)assert(gameplay_store.blocked&&gameplay.shop_status==STAYC_GAME_SHOP_UNAVAILABLE&&!gameplay_writes);
 } else if(main_scenario==4){
  if(ticks==200){gameplay.page=STAYC_GAME_PLAYER;gameplay.generation=10;gameplay.want_play=true;gameplay.duration_ms=120000;audio_override=true;injected_audio=(h2h_audio_status_t){.generation=10};}
  if(ticks==220){queued_click(BSP_BTN_OK);injected_audio.ended=true;}
  if(ticks==240){assert(!gameplay.want_play&&gameplay.generation==10);queued_click(BSP_BTN_OK);}
  if(ticks==260){assert(gameplay.want_play&&gameplay.generation==11);injected_audio=(h2h_audio_status_t){.generation=11,.failed=true};}
  if(ticks==280){assert(gameplay.audio_error&&!gameplay.want_play);queued_click(BSP_BTN_OK);}
  if(ticks==300){assert(gameplay.want_play&&!gameplay.audio_error&&gameplay.generation==12);injected_audio=(h2h_audio_status_t){.generation=11,.ended=true,.played_samples=UINT64_C(96000000)};}
  if(ticks==320){assert(gameplay.generation==12&&!gameplay.save.hearts&&!gameplay.save.listen_ms);h2h_pocket_record_t sent=pocket.record;h2h_pocket_reply_t revoke={.epoch=2};assert(h2h_pocket_reply(&pocket,&sent,&revoke,write_record,NULL));}
  if(ticks==340)assert(!h2h_cloud_can_play()&&!gameplay.want_play&&!gameplay.save.hearts&&!gameplay.save.listen_ms);
 }
}
void h2h_ui_resources(const h2h_resource_view_t *view){if(view){assert(view->bytes&&!memcmp(view->bytes,"PKVIS02\0",8));strcpy(displayed_resource,view->labels.id);}else displayed_resource[0]=0;}
esp_err_t h2h_ble_start(const char id[33]) { (void)id;return ESP_OK; }
esp_err_t h2h_ble_stop(void) { return main_scenario==8 && debug_requested?ESP_FAIL:ESP_OK; }
bool h2h_ble_poll(h2h_ble_command_t *command) { (void)command;return false; }
bool h2h_ble_passkey(uint32_t *pin) { (void)pin;return false; }
bool h2h_ble_connected(void) { return false; }
bool h2h_ble_authenticated_for_connection(uint32_t generation) { (void)generation; return false; }
uint32_t h2h_ble_relay_connection_generation(void){return 1;}
esp_tls_t *esp_tls_init(void){return NULL;}
int esp_tls_conn_new_async(const char *host,int len,int port,const esp_tls_cfg_t *cfg,esp_tls_t *tls){(void)host;(void)len;(void)port;(void)cfg;(void)tls;return -1;}
ssize_t esp_tls_conn_read(esp_tls_t *tls,void *out,size_t size){(void)tls;(void)out;(void)size;return -1;}
ssize_t esp_tls_conn_write(esp_tls_t *tls,const void *out,size_t size){(void)tls;(void)out;(void)size;return -1;}
int esp_tls_conn_destroy(esp_tls_t *tls){(void)tls;return 0;}
static void test_legacy_bytes_and_guarded_preferences(void) {
 static const unsigned char save_v1[48]={
  0x50,0x48,0x32,0x48,1,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,35,0,0,0,
  0,0,0,0,0,0,0,0,8,0,0,0,0x8d,0xb2,0xba,0xf1
 };
 static const unsigned char pocket_v2[384]={
  [0]=0x50,[1]=0x4b,[2]=0x52,[3]=0x32,[4]=2,[8]=1,[32]=7,[36]=1,
  [88]=0x50,[89]=0x48,[90]=0x32,[91]=0x48,[92]=1,[116]=35,[128]=8,
  [132]=0x8d,[133]=0xb2,[134]=0xba,[135]=0xf1,
  [380]=0x97,[381]=0xcc,[382]=0x54,[383]=0x17
 };
 h2h_model_t legacy;h2h_save_t saved;h2h_init(&legacy,2,1,1);h2h_save_snapshot(&legacy,&saved);
 assert(sizeof(saved)==sizeof(save_v1) && !memcmp(&saved,save_v1,sizeof(save_v1)));
 assert(h2h_cloud_start()==ESP_OK);
 h2h_pocket_record_t sent=pocket.record;h2h_pocket_reply_t activated={.epoch=1,.account_id=7,.activated=true};
 assert(h2h_pocket_reply(&pocket,&sent,&activated,write_record,NULL));
 assert(!memcmp(disk,pocket_v2,sizeof(pocket_v2)));
 unsigned char ownership_before[sizeof(disk)];memcpy(ownership_before,disk,sizeof(disk));
 uint64_t generation=pocket.model_generation;
 stayc_preference_t preference={.magic=0x53545932,.member=5,.epoch=1,.decor=0xaa};
 assert(sizeof(preference)==sizeof(stayc_preferences_disk));
 assert(h2h_cloud_guarded_write(1,generation,stayc_member_write,&preference,sizeof(preference),NULL));
 assert(stayc_preference_writes==1 && !memcmp(disk,ownership_before,sizeof(disk)));
 unsigned char preference_before[sizeof(stayc_preferences_disk)];memcpy(preference_before,stayc_preferences_disk,sizeof(preference_before));
 preference.member=4;
 assert(!h2h_cloud_guarded_write(2,generation,stayc_member_write,&preference,sizeof(preference),NULL));
 assert(!h2h_cloud_guarded_write(1,generation-1,stayc_member_write,&preference,sizeof(preference),NULL));
 assert(stayc_preference_writes==1 && !memcmp(stayc_preferences_disk,preference_before,sizeof(preference_before)));
 nvs_fail=true;
 assert(!h2h_cloud_guarded_write(1,generation,stayc_member_write,&preference,sizeof(preference),NULL));
 assert(stayc_preference_writes==1 && !memcmp(stayc_preferences_disk,preference_before,sizeof(preference_before)));
 assert(!memcmp(disk,ownership_before,sizeof(disk)));nvs_fail=false;
 stayc_control_t visuals={0};stayc_preferences_load(1,&visuals);assert(visuals.member==5 && visuals.decor==0xaa);
 memset(&visuals,0,sizeof(visuals));stayc_preferences_load(2,&visuals);assert(!visuals.member && !visuals.decor);
 stayc_preferences_disk[0]^=1;unsigned char invalid[sizeof(stayc_preferences_disk)];memcpy(invalid,stayc_preferences_disk,sizeof(invalid));
 stayc_preferences_load(1,&visuals);assert(!visuals.member && !visuals.decor && !memcmp(invalid,stayc_preferences_disk,sizeof(invalid)));
 memcpy(stayc_preferences_disk,preference_before,sizeof(preference_before));
 sent=pocket.record;h2h_save_t remote=saved;remote.volume=45;legacy.save=remote;h2h_save_snapshot(&legacy,&remote);
 h2h_pocket_reply_t restore={.epoch=1,.account_id=7,.activated=true,.has_save=true,.revision=1,.restore_required=true,.save=remote};
 assert(h2h_pocket_reply(&pocket,&sent,&restore,write_record,NULL));
 assert(pocket.model_generation!=generation && !h2h_cloud_guarded_write(1,generation,stayc_member_write,&preference,sizeof(preference),NULL));
 assert(!memcmp(stayc_preferences_disk,preference_before,sizeof(preference_before)));
 generation=pocket.model_generation;sent=pocket.record;h2h_pocket_reply_t replacement={.epoch=2,.account_id=8,.activated=true};
 assert(h2h_pocket_reply(&pocket,&sent,&replacement,write_record,NULL));
 assert(!h2h_cloud_guarded_write(1,generation,stayc_member_write,&preference,sizeof(preference),NULL));
 memset(&visuals,0,sizeof(visuals));stayc_preferences_load(2,&visuals);assert(!visuals.member && !visuals.decor);
 assert(!memcmp(stayc_preferences_disk,preference_before,sizeof(preference_before)));
 stayc_game_t game;stayc_game_init(&game,2,1,1);stayc_game_save_t extension;stayc_game_save_snapshot(&game,&extension);
 memset(&gameplay_store,0,sizeof(gameplay_store));h2h_save_t game_base=pocket.record.save;
 assert(!gameplay_persist(&extension,1,generation,&game_base)&&!gameplay_writes);
 generation=pocket.model_generation;
 assert(!gameplay_persist(&extension,2,generation-1,&game_base)&&!gameplay_writes);
 assert(gameplay_persist(&extension,2,generation,&game_base)&&gameplay_writes==1);
 uint8_t saved_extension[STAYC_GAME_RECORD_BYTES];memcpy(saved_extension,gameplay_disk,sizeof(saved_extension));
 assert(gameplay_persist(&extension,2,generation,&game_base)&&gameplay_writes==1); /* durable duplicate */
 sent=pocket.record;restore.epoch=2;restore.account_id=8;
 assert(h2h_pocket_reply(&pocket,&sent,&restore,write_record,NULL));
 assert(pocket.model_generation!=generation);
 /* Base restoration now atomically updates the complete local journal. */
 assert(gameplay_writes==2);stayc_game_record_t restored;
 assert(stayc_game_record_decode(&restored,gameplay_disk,164)&&restored.gameplay.volume==45&&restored.gameplay.star_balance==extension.star_balance);
 memcpy(saved_extension,gameplay_disk,164);
 /* Dedup must still authorize under the cloud lock before reporting success. */
 assert(!gameplay_persist(&extension,2,generation,&game_base)&&gameplay_writes==2&&!memcmp(saved_extension,gameplay_disk,sizeof(saved_extension)));
 assert(!gameplay_persist(&extension,1,pocket.model_generation,&game_base)&&gameplay_writes==2);
 game.save.star_balance=1;stayc_game_save_snapshot(&game,&extension);nvs_fail=true;
 assert(!gameplay_persist(&extension,2,pocket.model_generation,&pocket.record.save)&&gameplay_writes==2&&!memcmp(saved_extension,gameplay_disk,sizeof(saved_extension)));nvs_fail=false;
 puts("Legacy 48B/384B byte fixtures and preference epoch/generation/write-failure guards: PASS");
}
static void test_live_snapshot(void){
 assert(h2h_cloud_start()==ESP_OK);h2h_pocket_record_t sent=pocket.record;h2h_pocket_reply_t active={.epoch=1,.account_id=7,.activated=true};
 assert(h2h_pocket_reply(&pocket,&sent,&active,write_record,NULL));
 stayc_game_t model;stayc_game_init(&model,2,1,1);model.save.star_balance=100;stayc_game_save_t save;stayc_game_save_snapshot(&model,&save);
 assert(h2h_fullsave_local(&fullsave,&pocket,&save,0,&full_io));
 h2h_cloud_game_snapshot(1,pocket.model_generation,&save,0);uint64_t old_generation=fullsave.generation;
 model.save.star_balance=76;stayc_game_save_snapshot(&model,&save);assert(h2h_fullsave_local(&fullsave,&pocket,&save,0,&full_io));
 full_flush_live_locked();stayc_game_record_t record;assert(stayc_game_record_decode(&record,fullsave.local,164)&&record.gameplay.star_balance==76&&fullsave.generation>old_generation);
 model.save.star_balance=75;stayc_game_save_snapshot(&model,&save);h2h_cloud_game_snapshot(1,pocket.model_generation,&save,0);full_flush_live_locked();
 assert(stayc_game_record_decode(&record,fullsave.local,164)&&record.gameplay.star_balance==75&&fullsave.dirty);
 /* A valid remote record may carry a historical base different from its
  * current gameplay. Cache dedup must still normalize via the full journal. */
 stayc_game_record_decode(&record,fullsave.local,164);h2h_model_t basic;h2h_init(&basic,2,1,1);basic.save=record.cloud_base;
 basic.save.volume=record.gameplay.volume==45?46:45;h2h_save_snapshot(&basic,&record.cloud_base);
 uint8_t remote[164];stayc_game_record_encode(&record,remote);fullsave.dirty=false;
 assert(h2h_fullsave_reply(&fullsave,&pocket,fullsave.local,fullsave.revision,fullsave.revision+1,remote,false,true,&full_io));
 gameplay_load(1,&pocket.record.save);stayc_game_save_t duplicate;stayc_game_save_snapshot(&gameplay,&duplicate);
 h2h_save_t historical=pocket.record.save;assert(gameplay_persist(&duplicate,1,pocket.model_generation,&historical));
 assert(stayc_game_record_decode(&record,fullsave.local,164)&&record.cloud_base.volume==duplicate.volume&&record.gameplay.star_balance==75&&pocket.healthy&&fullsave.dirty);
 puts("Actual cloud live snapshot: durable generation fences stale RAM publication and flushes current normal gameplay: PASS");
}
static void test_wifi_lifecycle(void) {
 assert(h2h_cloud_start()==ESP_OK);observe_worker=true;
 if(!setjmp(worker_idle))worker(NULL);
 observe_worker=false;
 assert(!wifi_inits && !netif_creates && !wifi_initialized);
 assert(!wifi_connect("","") && !wifi_inits);
 fail_wifi_init=true;assert(!wifi_connect("Fixture","") && !wifi_initialized && !wifi_netif && !handler_count);
 fail_wifi_init=false;fail_ip_handler=true;
 assert(!wifi_connect("Fixture","") && !wifi_initialized && !wifi_netif && !handler_count);
 fail_ip_handler=false;fail_storage=true;
 assert(!wifi_connect("Fixture","") && !wifi_initialized && !wifi_netif && !handler_count);
 fail_storage=false;assert(wifi_init() && wifi_initialized && handler_count==2);
 unsigned initialized=wifi_inits;assert(wifi_init() && wifi_inits==initialized);
 wifi_release();assert(!wifi_initialized && !wifi_netif && !handler_count && netif_creates==netif_deletes);
 assert(wifi_init());wifi_release();assert(netif_creates==netif_deletes);
 puts("Actual cloud worker: no-credential deferral, init/storage/handler rollback, idempotence and retry PASS");
}
static void test_debug_cloud_barrier(void) {
 assert(h2h_cloud_start()==ESP_OK);
 h2h_pocket_record_t sent=pocket.record;
 h2h_pocket_reply_t owner={.epoch=1,.account_id=7,.activated=true};
 assert(h2h_pocket_reply(&pocket,&sent,&owner,write_record,NULL));
 unsigned char before[sizeof(disk)];memcpy(before,disk,sizeof(before));unsigned before_writes=writes;
 uint64_t generation=pocket.model_generation;
 assert(wifi_init() && h2h_cloud_open());
 assert(!h2h_cloud_debug_pause(true)); /* Request is not an acknowledgement. */
 observe_worker=true;if(!setjmp(worker_idle))worker(NULL);observe_worker=false;
 assert(h2h_cloud_debug_pause(true) && !wifi_initialized && !wifi_netif && !handler_count);
 assert(!((struct host_queue *)queue)->count && pocket.record.ownership.epoch==1 && pocket.record.ownership.account_id==7);
 h2h_save_t save=pocket.record.save;uint64_t epoch=0,model_generation=0;reply_t reply;
 assert(!h2h_cloud_model(&save,&epoch,&model_generation));
 assert(!request("/device/sync",HTTP_METHOD_POST,"{}",true,&reply));
 assert(!h2h_cloud_local_save(1,generation,&save));
 assert(!h2h_cloud_guarded_write(1,generation,stayc_member_write,&save,sizeof(save),NULL));
 assert(!h2h_cloud_unbind(1,generation) && !h2h_cloud_restore(1,generation));
 assert(!h2h_cloud_wifi("Fixture","") && !h2h_cloud_confirm() && !h2h_cloud_refresh());
 assert(!h2h_cloud_can_play_at(1,generation));
 assert(writes==before_writes && !memcmp(before,disk,sizeof(before)) && !gameplay_writes && !stayc_preference_writes);
 assert(!h2h_cloud_debug_pause(false));
 observe_worker=true;if(!setjmp(worker_idle))worker(NULL);observe_worker=false;
 assert(h2h_cloud_debug_pause(false) && h2h_cloud_can_play_at(1,generation));
 assert(h2h_cloud_model(&save,&epoch,&model_generation) && epoch==1 && model_generation==generation);
 /* Cancel exactly while the worker releases a running driver. Resume must
  * still pass the real boundary instead of losing the release acknowledgement. */
 assert(wifi_init());wifi_started=true;cancel_on_disconnect=true;
 assert(!h2h_cloud_debug_pause(true));
 assert(debug_boundary() && !debug_requested && debug_paused && !wifi_initialized);
 assert(!h2h_cloud_can_play_at(1,generation));
 assert(!debug_boundary() && !debug_paused && h2h_cloud_can_play_at(1,generation));
 assert(h2h_cloud_start()==ESP_OK && !debug_requested && !debug_paused); /* Reboot has no persisted bypass. */
 assert(!memcmp(before,disk,sizeof(before)));
 puts("Actual cloud debug barrier: worker acknowledgement, radio release, identity/save/network rejection, owner bytes and reboot PASS");
}
int main(int argc,char **argv) {
 if(argc>1&&!strcmp(argv[1],"--relay-live-snapshot")){test_live_snapshot();return 0;}
 if(argc>1 && !strcmp(argv[1],"--debug-cloud-barrier")){test_debug_cloud_barrier();return 0;}
 if(argc>1 && !strcmp(argv[1],"--wifi-lifecycle")){test_wifi_lifecycle();return 0;}
 if(argc>1 && !strcmp(argv[1],"--guarded-preferences")){test_legacy_bytes_and_guarded_preferences();return 0;}
 if(argc>1 && !strcmp(argv[1],"--bridge")) {
  bridge=true;assert(h2h_cloud_start()==ESP_OK);
  char *line=NULL;size_t capacity=0;
  while(getline(&line,&capacity,stdin)>0) {
   bool ok=false;
   if(!strncmp(line,"BEGIN",5))ok=begin_pairing();
   else if(!strncmp(line,"POLL",4)){poll_pairing();ok=true;}
   else if(!strncmp(line,"CONFIRM",7)) { command_t cmd;if(h2h_cloud_confirm())while(xQueueReceive(queue,&cmd,0))if(cmd.type==CMD_CONFIRM){confirm_pairing(&cmd);ok=true;} }
   else if(!strncmp(line,"SYNC",4))ok=sync_device();
   else if(!strncmp(line,"LAYOUT",6)){resource_layout=true;memset(resource_slots,0xff,sizeof(resource_slots));ok=h2h_resource_runtime_start()==ESP_OK;}
   else if(!strncmp(line,"KEY ",4)){line[strcspn(line,"\r\n")]=0;fixture_key_exists=hex_bytes(line+4,fixture_public_key,32);ok=fixture_key_exists;}
   else if(!strncmp(line,"APPLY",5)){const h2h_resource_view_t *view=h2h_resource_runtime_view_begin(pocket.record.ownership.epoch,pocket.model_generation);h2h_ui_resources(view);ok=view!=NULL;h2h_resource_runtime_view_end();}
   else if(!strncmp(line,"MAP_FAIL",8)){resource_map_fail=true;ok=true;}
   else if(!strncmp(line,"CLEAR_FAIL",10)){resource_map_fail=nvs_fail=false;ok=true;}
   else if(!strncmp(line,"RETRY",5)){h2h_resource_runtime_retry();ok=true;}
   else if(!strncmp(line,"UNBIND",6))ok=h2h_cloud_unbind(pocket.record.ownership.epoch,pocket.model_generation);
   else if(!strncmp(line,"RESTORE",7))ok=h2h_cloud_restore(pocket.record.ownership.epoch,pocket.model_generation);
   else if(!strncmp(line,"REBOOT",6))ok=h2h_cloud_start()==ESP_OK;
   // Test-only simulated record loss: keep the per-device identity intact.
   else if(!strncmp(line,"LOSE_RECORD",11)){disk_exists=false;ok=h2h_cloud_start()==ESP_OK;}
   else if(!strncmp(line,"FAIL",4)){nvs_fail=true;ok=true;}
   else if(!strncmp(line,"SAVE ",5)) {
    unsigned hearts=(unsigned)strtoul(line+5,NULL,10);h2h_model_t model;h2h_save_t save;
    h2h_init(&model,2,1,1);h2h_listened(&model,hearts*120000);h2h_save_snapshot(&model,&save);
    ok=h2h_cloud_local_save(pocket.record.ownership.epoch,pocket.model_generation,&save);
   }
   print_state(ok);
  }
  free(line);return 0;
 }
 main_mode=true;
 if(argc>1 && !strncmp(argv[1],"--gameplay-",11)){
  main_scenario=!strcmp(argv[1],"--gameplay-shop-fail")?1:!strcmp(argv[1],"--gameplay-postwrite-fail")?2:!strcmp(argv[1],"--gameplay-protected")?3:!strcmp(argv[1],"--gameplay-audio-boundaries")?4:!strcmp(argv[1],"--gameplay-debug-local")?5:!strcmp(argv[1],"--gameplay-debug-bound")?6:!strcmp(argv[1],"--gameplay-debug-cancel")?7:!strcmp(argv[1],"--gameplay-debug-stop-fail")?8:0;assert(main_scenario);
 }
 if(argc>1 && (!strcmp(argv[1],"--activated-main")||(main_scenario && main_scenario<=6 && main_scenario!=5))) {
  main_activated=true;h2h_pocket_init(&pocket,2,1);h2h_pocket_record_t sent=pocket.record;
  h2h_pocket_reply_t reply={.epoch=1,.account_id=7,.activated=true};assert(h2h_pocket_reply(&pocket,&sent,&reply,write_record,NULL));
 }
 if(main_scenario==1||main_scenario==2){
  stayc_game_init(&gameplay,2,1,1);gameplay.save.star_balance=100;stayc_game_save_t save;stayc_game_save_snapshot(&gameplay,&save);stayc_game_store_t store={0};assert(stayc_game_store_write(&store,&save,1,&pocket.record.save,legacy_gameplay_fixture,NULL));
 }
 if(main_scenario==3){
  stayc_game_record_t record={.magic=0x53544734,.version=99,.epoch=1};record.cloud_base=pocket.record.save;stayc_game_init(&gameplay,2,1,1);stayc_game_save_snapshot(&gameplay,&record.gameplay);stayc_game_record_encode(&record,gameplay_disk);memcpy(legacy_gameplay_disk,gameplay_disk,164);legacy_gameplay_exists=gameplay_disk_exists=true;
 }
 if(!setjmp(main_exit))app_main();
 assert(audio_requests>20 && ui_frames>5);
 if(main_scenario){
  if(main_scenario>=5){
   unsigned debug_scenario=main_scenario;
   assert(!gameplay.debug_mode && !stayc_preference_writes);
   if(main_scenario==6 && gameplay_disk_exists) {
    stayc_game_record_t saved;assert(stayc_game_record_decode(&saved,gameplay_disk,sizeof(gameplay_disk)));
    assert(saved.epoch==2 && !saved.gameplay.hearts && !saved.gameplay.star_balance);
   } else assert(!gameplay_writes);
   assert(main_scenario>=7?!local_audio_requests:local_audio_requests>0);
   if(main_scenario!=6) {
    assert(writes==debug_real_writes && !h2h_cloud_can_play());
    unsigned char before[sizeof(disk)];memcpy(before,disk,sizeof(before));
    debug_reboot=true;main_scenario=0;ticks=0;
    if(!setjmp(main_exit))app_main();
    assert(!gameplay.debug_mode && !debug_requested && !h2h_cloud_can_play() && !memcmp(before,disk,sizeof(before)));
   }
   printf("Actual main debug scenario %u: PASS (host SDK scheduling/UI/audio adapters)\n",debug_scenario);return 0;
  }
  if(main_scenario==3){stayc_game_record_t rejected;assert(!stayc_game_record_decode(&rejected,gameplay_disk,sizeof(gameplay_disk))&&!gameplay_writes);}
  if(main_scenario==2){stayc_game_record_t durable;assert(stayc_game_record_decode(&durable,gameplay_disk,sizeof(gameplay_disk)));assert(durable.gameplay.star_balance==76&&durable.gameplay.outfit==1&&durable.gameplay.owned_outfits==3&&gameplay_store.blocked);}
  puts("Actual main gameplay transaction/protection/audio boundary fault tests: PASS");return 0;
 }
 if(!main_activated)assert(pocket.record.save.hearts==0 && !h2h_cloud_can_play());
 else assert(main_revoked && !h2h_cloud_can_play() && pocket.record.ownership.epoch==2);
 puts("Actual main/cloud activation and audio gate host tests: PASS");return 0;
}

const mbedtls_md_info_t *mbedtls_md_info_from_type(int type){static const int info=1;assert(type==MBEDTLS_MD_SHA256);return &info;}
int mbedtls_md_hmac(const mbedtls_md_info_t *info,const unsigned char *key,size_t keylen,const unsigned char *input,size_t n,unsigned char *out){(void)info;unsigned size=0;return HMAC(EVP_sha256(),key,(int)keylen,input,n,out,&size)&&size==32?0:-1;}
char *h2h_ble_relay_take(uint32_t *generation,size_t *length){(void)generation;(void)length;return NULL;}
void h2h_ble_relay_finish(uint32_t generation,size_t length){(void)generation;(void)length;}
bool h2h_ble_relay_disconnected(void){return false;}
