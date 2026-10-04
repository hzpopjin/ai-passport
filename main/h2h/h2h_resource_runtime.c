#include "h2h_resource_runtime.h"
#include "h2h_cloud.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>

__attribute__((weak)) bool h2h_resource_key_at(unsigned index,char id[33],unsigned char key[32]){(void)index;(void)id;(void)key;return false;}
static h2h_resource_t engine;
static const esp_partition_t *slots[2];
static SemaphoreHandle_t resource_lock;
static bool available, approved, builtin_pending, view_locked;
static uint64_t pending_generation, builtin_epoch, builtin_generation;
static uint32_t builtin_revision;
static h2h_resource_view_t view;
static const void *mapped, *retired;
static esp_partition_mmap_handle_t map_handle, retired_handle;
static int mapped_slot=-1, retired_slot=-1;
static uint64_t visible_epoch,failed_epoch;
static uint32_t visible_revision,failed_revision;
static unsigned char mapped_sha[32];
static bool view_authorized;
typedef struct { uint64_t epoch,generation; h2h_resource_fetch_fn fetch; void *fetch_ctx; } context_t;
static bool alive(void *ctx){context_t *c=ctx;return h2h_cloud_can_play_at(c->epoch,c->generation);}
static bool key(const char *id,unsigned char out[32],void *ctx){(void)ctx;char known[33];unsigned char public_key[32];for(unsigned i=0;i<8&&h2h_resource_key_at(i,known,public_key);i++)if(!strcmp(known,id)){memcpy(out,public_key,32);return true;}return false;}
static bool read_slot(int slot,size_t offset,void *out,size_t length,void *ctx){(void)ctx;return slot>=0&&slot<2&&slots[slot]&&esp_partition_read(slots[slot],offset,out,length)==ESP_OK;}
// Rendering can recover a previous slot while a download is in flight. Never
// alter a slot that has become active or still backs an LVGL image descriptor.
static bool mutate_slot(int slot,size_t offset,const void *bytes,size_t length,bool erase){
 if(slot<0||slot>1||!slots[slot]||xSemaphoreTake(resource_lock,portMAX_DELAY)!=pdTRUE)return false;
 bool ok=slot!=engine.record.active&&slot!=mapped_slot&&slot!=retired_slot;
 if(ok)ok=(erase?esp_partition_erase_range(slots[slot],0,slots[slot]->size):esp_partition_write(slots[slot],offset,bytes,length))==ESP_OK;
 xSemaphoreGive(resource_lock);return ok;
}
static bool write_slot(int slot,size_t offset,const void *bytes,size_t length,void *ctx){(void)ctx;return mutate_slot(slot,offset,bytes,length,false);}
static bool erase_slot(int slot,void *ctx){(void)ctx;return mutate_slot(slot,0,NULL,0,true);}
static bool hash_slot(int slot,size_t offset,size_t length,unsigned char out[32],void *ctx){
 mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);bool ok=mbedtls_sha256_starts(&sha,0)==0;unsigned char bytes[1024];
 for(size_t position=0;ok&&position<length;){size_t n=length-position;if(n>sizeof(bytes))n=sizeof(bytes);ok=read_slot(slot,offset+position,bytes,n,ctx)&&mbedtls_sha256_update(&sha,bytes,n)==0;position+=n;}
 if(ok)ok=mbedtls_sha256_finish(&sha,out)==0;
 mbedtls_sha256_free(&sha);return ok;
}
static bool fetch_slot(size_t offset,void *out,size_t length,void *ctx){context_t *c=ctx;return c->fetch&&c->fetch(offset,out,length,c->fetch_ctx);}
static bool nvs_write(const void *bytes,size_t length,void *ctx){
 (void)ctx;nvs_handle_t handle;if(nvs_open("h2h_resources",NVS_READWRITE,&handle)!=ESP_OK)return false;
 esp_err_t error=nvs_set_blob(handle,"visual_v2",bytes,length);if(error==ESP_OK)error=nvs_commit(handle);nvs_close(handle);return error==ESP_OK;
}
static bool commit(const void *bytes,size_t length,void *ctx){context_t *c=ctx;return h2h_cloud_guarded_write(c->epoch,c->generation,nvs_write,bytes,length,NULL);}
static h2h_resource_io_t io(context_t *context){return (h2h_resource_io_t){.capacity={slots[0]?slots[0]->size:0,slots[1]?slots[1]->size:0},.key=key,.read=read_slot,.write=write_slot,.erase=erase_slot,.hash=hash_slot,.fetch=fetch_slot,.alive=alive,.commit=commit,.ctx=context};}
esp_err_t h2h_resource_runtime_start(void){
#if !defined(CONFIG_ESP_MAIN_TASK_STACK_SIZE) || CONFIG_ESP_MAIN_TASK_STACK_SIZE < 12288
 return ESP_ERR_NO_MEM; // Crypto, NVS commit and UI share the application task stack.
#endif
 resource_lock=xSemaphoreCreateMutex();if(!resource_lock)return ESP_ERR_NO_MEM;
 h2h_resource_init(&engine);
 const esp_partition_t *factory=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,"factory");
 slots[0]=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"pocket_a");slots[1]=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"pocket_b");
 if(!factory||factory->address!=0x10000||factory->size!=0x770000)return ESP_FAIL;
 for(unsigned i=0;i<2;i++)if(!slots[i]||slots[i]->address!=0x780000+i*0x40000||slots[i]->size!=H2H_RESOURCE_SLOT_BYTES||slots[i]->encrypted||slots[i]->readonly)return ESP_FAIL;
 nvs_handle_t handle;if(nvs_open("h2h_resources",NVS_READWRITE,&handle)!=ESP_OK)return ESP_FAIL;
 unsigned char bytes[H2H_RESOURCE_STATE_BYTES];size_t length=sizeof(bytes);esp_err_t result=nvs_get_blob(handle,"visual_v2",bytes,&length);nvs_close(handle);
 if(result!=ESP_ERR_NVS_NOT_FOUND&&(result!=ESP_OK||!h2h_resource_decode(&engine,bytes,length)))return ESP_FAIL;
 available=true;return ESP_OK;
}
bool h2h_resource_runtime_available(void){return available;}
unsigned h2h_resource_runtime_keys(char ids[8][33]){unsigned char public_key[32];unsigned n=0;for(;n<8&&h2h_resource_key_at(n,ids[n],public_key);n++){}return n;}
bool h2h_resource_runtime_prepare(const h2h_resource_manifest_t *manifest,uint64_t epoch,uint64_t generation,uint32_t revision,h2h_resource_fetch_fn fetch,void *fetch_ctx){
 if(!available||!resource_lock)return false;
 h2h_resource_t *copy=malloc(sizeof(*copy));if(!copy)return false;
 if(xSemaphoreTake(resource_lock,portMAX_DELAY)!=pdTRUE){free(copy);return false;}
 *copy=engine;uint32_t before=engine.record.generation;
 // A single worker owns downloads. The renderer may read the active slot meanwhile.
 engine.pending=false;approved=false;xSemaphoreGive(resource_lock);
 context_t context={epoch,generation,fetch,fetch_ctx};h2h_resource_io_t callbacks=io(&context);
 bool ok=h2h_resource_stage(copy,manifest,epoch,revision,&callbacks);
 if(xSemaphoreTake(resource_lock,portMAX_DELAY)==pdTRUE){
  ok=ok&&before==engine.record.generation&&alive(&context);
  if(ok){builtin_pending=false;engine.staged=copy->staged;engine.staged_slot=copy->staged_slot;engine.staged_epoch=epoch;engine.staged_revision=revision;engine.pending=true;pending_generation=generation;failed_epoch=0;}
  xSemaphoreGive(resource_lock);
 }else ok=false;
 free(copy);return ok;
}
void h2h_resource_runtime_approve(uint64_t epoch,uint32_t revision,bool accepted){
 if(resource_lock&&xSemaphoreTake(resource_lock,portMAX_DELAY)==pdTRUE){if(engine.pending&&engine.staged_epoch==epoch&&engine.staged_revision==revision){approved=accepted;if(!accepted)engine.pending=false;}xSemaphoreGive(resource_lock);}
}
void h2h_resource_runtime_builtin(uint64_t epoch,uint64_t generation,uint32_t revision){
 if(!available||!resource_lock)return;
 if(xSemaphoreTake(resource_lock,portMAX_DELAY)==pdTRUE){
  if(engine.record.epoch!=epoch||engine.record.active>=0||engine.record.revision!=revision){builtin_pending=true;builtin_epoch=epoch;builtin_generation=generation;builtin_revision=revision;}
  engine.pending=false;approved=false;xSemaphoreGive(resource_lock);
 }
}
bool h2h_resource_runtime_installed(uint64_t epoch,uint32_t revision,const unsigned char *sha){
 if(!available||!resource_lock||xSemaphoreTake(resource_lock,portMAX_DELAY)!=pdTRUE)return false;
 bool ok=engine.record.epoch==epoch&&engine.record.revision==revision&&visible_epoch==epoch&&visible_revision==revision;
 if(sha)ok=ok&&mapped&&mapped_slot==engine.record.active&&!memcmp(sha,mapped_sha,32);
 else ok=ok&&engine.record.active<0&&!mapped;
 xSemaphoreGive(resource_lock);return ok;
}
bool h2h_resource_runtime_failed(uint64_t epoch,uint32_t revision){
 if(!resource_lock||xSemaphoreTake(resource_lock,pdMS_TO_TICKS(100))!=pdTRUE)return false;
 bool failed=failed_epoch==epoch&&failed_revision==revision;xSemaphoreGive(resource_lock);return failed;
}
void h2h_resource_runtime_retry(void){if(resource_lock&&xSemaphoreTake(resource_lock,portMAX_DELAY)==pdTRUE){failed_epoch=0;xSemaphoreGive(resource_lock);}}
static bool map_slot(int slot,const h2h_resource_io_t *callbacks,const void **bytes,esp_partition_mmap_handle_t *handle,h2h_resource_labels_t *labels,h2h_resource_manifest_t *manifest){
 if(!h2h_resource_slot_valid(slot,callbacks,manifest)||esp_partition_mmap(slots[slot],H2H_RESOURCE_PAYLOAD_OFFSET,H2H_RESOURCE_BYTES,ESP_PARTITION_MMAP_DATA,bytes,handle)!=ESP_OK)return false;
 if(!h2h_resource_labels(*bytes,H2H_RESOURCE_BYTES,manifest,labels)){esp_partition_munmap(*handle);*bytes=NULL;return false;}
 return true;
}
const h2h_resource_view_t *h2h_resource_runtime_view_begin(uint64_t epoch,uint64_t generation){
 if(!available||!resource_lock||xSemaphoreTake(resource_lock,pdMS_TO_TICKS(100))!=pdTRUE)return NULL;
 view_locked=true;view_authorized=false;context_t context={.epoch=epoch,.generation=generation};h2h_resource_io_t callbacks=io(&context);
 if(!alive(&context))return NULL;
 view_authorized=true;
 // Complete persistence and rendering while holding both the resource and LVGL locks.
 if(builtin_pending&&builtin_epoch==epoch&&builtin_generation==generation){
  if(!h2h_resource_builtin(&engine,epoch,builtin_revision,&callbacks)){failed_epoch=epoch;failed_revision=builtin_revision;}
  builtin_pending=false;
 }
 if(engine.pending&&approved&&pending_generation==generation&&engine.staged_epoch==epoch){
  const void *next=NULL;esp_partition_mmap_handle_t handle=0;h2h_resource_manifest_t manifest;h2h_resource_labels_t labels;
  bool ok=map_slot(engine.staged_slot,&callbacks,&next,&handle,&labels,&manifest);
  if(ok)ok=h2h_resource_publish(&engine,epoch,&callbacks);
  if(ok){retired=mapped;retired_handle=map_handle;retired_slot=mapped?mapped_slot:-1;mapped=next;map_handle=handle;mapped_slot=engine.record.active;view.bytes=next;view.labels=labels;view.generation=engine.record.generation;memcpy(mapped_sha,manifest.sha256,32);}
  else {if(next)esp_partition_munmap(handle);failed_epoch=epoch;failed_revision=engine.staged_revision;engine.pending=false;approved=false;}
 }
 int active=h2h_resource_active(&engine,epoch);
 if(active>=0&&mapped_slot!=active){
  if(!h2h_resource_recover(&engine,epoch,&callbacks))active=-1;else active=h2h_resource_active(&engine,epoch);
 }
 if(active!=mapped_slot){
  const void *next=NULL;esp_partition_mmap_handle_t handle=0;h2h_resource_manifest_t manifest;h2h_resource_labels_t labels;
  if(active>=0&&!map_slot(active,&callbacks,&next,&handle,&labels,&manifest)){
   failed_epoch=epoch;failed_revision=engine.record.revision;
   active=h2h_resource_rollback(&engine,epoch,&callbacks)?engine.record.active:-1;
   if(active>=0&&!map_slot(active,&callbacks,&next,&handle,&labels,&manifest))active=-1;
   // Both mappings can fail: retain no unverified pointer and use built-in visuals.
  }
  retired=mapped;retired_handle=map_handle;retired_slot=mapped?mapped_slot:-1;mapped=next;map_handle=handle;mapped_slot=active;
  if(next){view.bytes=next;view.labels=labels;view.generation=engine.record.generation;memcpy(mapped_sha,manifest.sha256,32);}
 }
 return mapped?&view:NULL;
}
void h2h_resource_runtime_view_end(void){
 if(!view_locked)return;
 if(retired){esp_partition_munmap(retired_handle);retired=NULL;retired_slot=-1;}
 if(view_authorized){visible_epoch=engine.record.epoch;visible_revision=engine.record.revision;}
 view_locked=false;xSemaphoreGive(resource_lock);
}
