#define _POSIX_C_SOURCE 200809L
#include "pocket_sdk.h"
#include "cJSON.h"
#include "h2h_tracks.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <setjmp.h>
#include "../main/h2h/h2h_cloud.c"

const h2h_track_t h2h_tracks[]={{.duration_ms=120000},{.duration_ms=120000}};
const unsigned h2h_track_count=2,h2h_song_count=1;
static unsigned char disk[H2H_POCKET_RECORD_SIZE], staged[H2H_POCKET_RECORD_SIZE];
static bool disk_exists, staged_exists, nvs_fail;
static unsigned char resource_disk[H2H_RESOURCE_STATE_BYTES],resource_staged[H2H_RESOURCE_STATE_BYTES];
static bool resource_disk_exists,resource_staged_exists,resource_layout,resource_map_fail;
static unsigned char resource_slots[2][H2H_RESOURCE_SLOT_BYTES],fixture_public_key[32];
static bool fixture_key_exists;
static unsigned mapped_references[2];
static char displayed_resource[33];
static uint32_t ticks,random_counter=1;
static bool bridge;
static bool main_mode, main_activated, main_revoked;
static unsigned audio_requests, ui_frames, writes;
static jmp_buf main_exit;
struct host_queue { size_t size; unsigned count,limit; unsigned char values[16][256]; };
struct host_http { esp_http_client_config_t config; char *url,*payload; char device[33],token[65],range[64]; int status; };
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t handle,uint32_t timeout) { (void)timeout; return handle!=NULL; }
int xSemaphoreGive(SemaphoreHandle_t handle) { return handle!=NULL; }
QueueHandle_t xQueueCreate(unsigned limit,size_t size) { struct host_queue *q=calloc(1,sizeof(*q)); assert(q && limit<=16 && size<=256);q->limit=limit;q->size=size;return q; }
int xQueueSend(QueueHandle_t handle,const void *value,uint32_t timeout) { (void)timeout;struct host_queue *q=handle;if(q->count>=q->limit)return 0;memcpy(q->values[q->count++],value,q->size);return 1; }
int xQueueReceive(QueueHandle_t handle,void *value,uint32_t timeout) {
 (void)timeout;struct host_queue *q=handle;
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
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out) { (void)mode;*out=!strcmp(name,"h2h_resources")?2:1;return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size) {
 (void)h;const char *value=!strcmp(key,"id")?"11111111111111111111111111111111":!strcmp(key,"secret")?"abababababababababababababababababababababababababababababababab":NULL;
 if(!value)return ESP_ERR_NVS_NOT_FOUND;
 if(*size<strlen(value)+1)return ESP_FAIL;
 strcpy(out,value);*size=strlen(value)+1;return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *v) { (void)h;(void)key;(void)v;return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *length) {
 if(h==2){assert(!strcmp(key,"visual_v2"));if(!resource_disk_exists)return ESP_ERR_NVS_NOT_FOUND;if(*length<sizeof(resource_disk))return ESP_FAIL;memcpy(out,resource_disk,sizeof(resource_disk));*length=sizeof(resource_disk);return ESP_OK;}
 assert(!strcmp(key,"pocket_v2"));if(!disk_exists)return ESP_ERR_NVS_NOT_FOUND;if(*length<sizeof(disk))return ESP_FAIL;memcpy(out,disk,sizeof(disk));*length=sizeof(disk);return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *value,size_t length) {
 if(h==2){assert(!strcmp(key,"visual_v2")&&length==sizeof(resource_staged));memcpy(resource_staged,value,length);resource_staged_exists=true;return ESP_OK;}
 assert(!strcmp(key,"pocket_v2")&&length==sizeof(staged));memcpy(staged,value,length);staged_exists=true;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
 if(nvs_fail){staged_exists=resource_staged_exists=false;return ESP_FAIL;}
 if(h==2){if(resource_staged_exists){memcpy(resource_disk,resource_staged,sizeof(resource_disk));resource_disk_exists=true;resource_staged_exists=false;}return ESP_OK;}
 if(staged_exists){memcpy(disk,staged,sizeof(disk));disk_exists=true;staged_exists=false;writes++;}return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key) { (void)h;(void)key;return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t esp_netif_init(void) { return ESP_OK; }
void *esp_netif_create_default_wifi_sta(void) { return (void *)1; }
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_err_t esp_event_handler_register(esp_event_base_t b,int id,void (*fn)(void *,esp_event_base_t,int32_t,void *),void *arg) { (void)b;(void)id;(void)fn;(void)arg;return ESP_OK; }
esp_err_t esp_wifi_init(const wifi_init_config_t *c) { (void)c;return ESP_OK; }
esp_err_t esp_wifi_set_mode(int mode) { (void)mode;return ESP_OK; }
esp_err_t esp_wifi_connect(void) { return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }
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
#include "../main/main.c"
#undef TAG
int64_t esp_timer_get_time(void) { return (int64_t)ticks*1000; }
size_t esp_get_free_heap_size(void) { return 100000; }
size_t heap_caps_get_largest_free_block(unsigned flags) { (void)flags;return 100000; }
esp_err_t bsp_display_init(void) { return ESP_OK; }
bool bsp_lvgl_init(void) { return true; }
void bsp_display_backlight(uint8_t percent) { (void)percent; }
bool bsp_lvgl_lock(unsigned timeout) { (void)timeout;return true; }
void bsp_lvgl_unlock(void) { }
esp_err_t bsp_battery_init(void) { return ESP_OK; }
int bsp_battery_soc(void) { return 80; }
esp_err_t bsp_button_init(bsp_btn_cb_t callback,void *context) {
 // Try all event variants; only the main loop handles them.
 for(unsigned key=0;key<3;key++) for(unsigned event=0;event<5;event++) callback((bsp_btn_t)key,(bsp_btn_ev_t)event,context);
 return ESP_OK;
}
esp_err_t h2h_audio_start(void) { return ESP_OK; }
void h2h_audio_request(const h2h_audio_request_t *request) { audio_requests++;if(!h2h_cloud_can_play())assert(!request->play); }
h2h_audio_status_t h2h_audio_status(void) { return (h2h_audio_status_t){.generation=1,.played_samples=(uint64_t)ticks*480,.ended=true}; }
void h2h_ui_render(const h2h_model_t *model,unsigned position,bool playing,int battery,bool storage,bool input,unsigned time,const h2h_cloud_status_t *cloud,bool ble,int passkey) {
 (void)position;(void)playing;(void)battery;(void)storage;(void)input;(void)time;(void)cloud;(void)ble;(void)passkey;ui_frames++;
 if(!h2h_cloud_can_play()){assert(model->page==H2H_ACCOUNT);assert(!model->want_play && !model->countdown_ms);}
 // This adapter reports stale-generation PCM; it must never reward the new model.
 assert(model->save.listen_ms==0 && model->save.hearts==0);
}
void h2h_ui_resources(const h2h_resource_view_t *view){if(view){assert(view->bytes&&!memcmp(view->bytes,"PKVIS02\0",8));strcpy(displayed_resource,view->labels.id);}else displayed_resource[0]=0;}
esp_err_t h2h_ble_start(const char id[33]) { (void)id;return ESP_OK; }
void h2h_ble_stop(void) { }
bool h2h_ble_poll(h2h_ble_command_t *command) { (void)command;return false; }
bool h2h_ble_passkey(uint32_t *pin) { (void)pin;return false; }
bool h2h_ble_connected(void) { return false; }
int main(int argc,char **argv) {
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
 if(argc>1 && !strcmp(argv[1],"--activated-main")) {
  main_activated=true;h2h_pocket_init(&pocket,2,1);h2h_pocket_record_t sent=pocket.record;
  h2h_pocket_reply_t reply={.epoch=1,.account_id=7,.activated=true};assert(h2h_pocket_reply(&pocket,&sent,&reply,write_record,NULL));
 }
 if(!setjmp(main_exit))app_main();
 assert(audio_requests>20 && ui_frames>5);
 if(!main_activated)assert(pocket.record.save.hearts==0 && !h2h_cloud_can_play());
 else assert(main_revoked && !h2h_cloud_can_play() && pocket.record.ownership.epoch==2);
 puts("Actual main/cloud activation and audio gate host tests: PASS");return 0;
}
