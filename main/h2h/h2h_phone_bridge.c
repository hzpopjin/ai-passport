#include "h2h_cloud.h"
#include "h2h_ble.h"
#include "h2h_phone.h"
#include "pocket_profile_esp.h"
#include "h2h_tracks.h"
#include "mbedtls/sha256.h"
#include "esp_random.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
static h2h_pocket_t pocket;
static h2h_fullsave_t fullsave;
static h2h_phone_t phone;
static pocket_profile_t profile;
static bool fullsave_ready,debug_requested,debug_paused;
static stayc_game_save_t live_game;
static uint32_t live_settled;
static uint64_t live_epoch,live_generation,live_full_generation;
static bool live_game_valid;
static SemaphoreHandle_t state_lock;
static h2h_cloud_status_t state;
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
       live_full_generation==fullsave.generation&&!debug_requested&&!debug_paused&&h2h_pocket_can_play(&pocket))
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
static bool write_record(const unsigned char *bytes, size_t length, void *context) {
    (void)context;
    if(fullsave_ready&&(fullsave.bank>=0||fullsave.has_local))return h2h_fullsave_pocket_write(&fullsave,&pocket,bytes,length,&full_io);
    nvs_handle_t handle;
    if(nvs_open("h2h_cloud",NVS_READWRITE,&handle)!=ESP_OK) return false;
    esp_err_t error=nvs_set_blob(handle,"pocket_v2",bytes,length);
    if(error==ESP_OK) error=nvs_commit(handle);
    nvs_close(handle); return error==ESP_OK;
}
static bool debug_blocked(void){return debug_requested||debug_paused;}
bool h2h_cloud_debug_pause(bool requested){if(!state_lock||xSemaphoreTake(state_lock,pdMS_TO_TICKS(100))!=pdTRUE)return false;debug_requested=debug_paused=requested;xSemaphoreGive(state_lock);return true;}
static void update(h2h_cloud_state_t mode,const char *message){state.state=mode;snprintf(state.message,sizeof(state.message),"%s",message);}
static void hex_random(char *out,size_t bytes){uint8_t b[32];esp_fill_random(b,bytes);for(size_t i=0;i<bytes;i++){out[2*i]="0123456789abcdef"[b[i]>>4];out[2*i+1]="0123456789abcdef"[b[i]&15];}out[2*bytes]=0;}
static bool load_identity(void){nvs_handle_t h;if(nvs_open("h2h_cloud",NVS_READWRITE,&h)!=ESP_OK)return false;size_t size=33;esp_err_t e=nvs_get_str(h,"id",state.device_id,&size);
    if(e==ESP_ERR_NVS_NOT_FOUND){hex_random(state.device_id,16);e=nvs_set_str(h,"id",state.device_id);if(e==ESP_OK)e=nvs_commit(h);}nvs_close(h);return e==ESP_OK&&strlen(state.device_id)==32;}
static bool phone_sha(const void *b,size_t n,uint8_t out[32]){return mbedtls_sha256(b,n,out,0)==0;}
void h2h_cloud_status(h2h_cloud_status_t *out){if(!out||!state_lock||xSemaphoreTake(state_lock,pdMS_TO_TICKS(100))!=pdTRUE)return;
    *out=state;out->bound=h2h_pocket_can_play(&pocket);out->conflict=pocket.record.conflict;out->pending_unbind=pocket.record.ownership.pending_unbind[0]!=0;
    out->recovery=pocket.record.recovery;out->ownership_epoch=pocket.record.ownership.epoch;out->model_generation=pocket.model_generation;out->save_revision=pocket.record.revision;out->wifi_ready=false;xSemaphoreGive(state_lock);}
static void worker(void *arg){(void)arg;for(;;){uint32_t lease=0;size_t length=0;char *bytes=h2h_ble_relay_take(&lease,&length);
    (void)h2h_ble_relay_disconnected();
    if(xSemaphoreTake(state_lock,portMAX_DELAY)==pdTRUE){full_recover_locked();if(!debug_blocked()){full_flush_live_locked();
        if(bytes)length=h2h_phone_rpc(&phone,bytes,length,bytes);
        state.state=h2h_pocket_can_play(&pocket)?H2H_CLOUD_BOUND:H2H_CLOUD_SETUP;
        snprintf(state.message,sizeof(state.message),"%s",pocket.record.conflict?"Save conflict; restore explicitly":pocket.record.ownership.activated?"Phone sync ready":"Activation required; use phone");
    }else if(bytes){snprintf(bytes,H2H_PHONE_RPC_MAX+1,"{\"state\":\"error\",\"code\":\"debug_busy\"}");length=strlen(bytes);}xSemaphoreGive(state_lock);}
    if(bytes) { h2h_ble_relay_finish(lease,length); }
    vTaskDelay(pdMS_TO_TICKS(50));}}
esp_err_t h2h_cloud_start(void) {
    fullsave_ready=live_game_valid=false;h2h_fullsave_init(&fullsave);
    state_lock=xSemaphoreCreateMutex();
    if(!state_lock) return ESP_ERR_NO_MEM;
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
    pocket_profile_io_t profile_io;
    (void)pocket_profile_open(&profile,pocket_profile_esp_io(&profile_io)?&profile_io:NULL);
    phone=(h2h_phone_t){.pocket=&pocket,.full=&fullsave,.io=&full_io,.device=state.device_id,.sha=phone_sha,.profile=&profile};
    return xTaskCreate(worker,"phone_bridge",8192,NULL,5,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
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
    bool allowed=h2h_pocket_can_play(&pocket); xSemaphoreGive(state_lock); return allowed;
}
bool h2h_cloud_can_play_at(uint64_t epoch, uint64_t generation) {
    if(!state_lock || xSemaphoreTake(state_lock,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    bool allowed=!debug_requested && !debug_paused && epoch==pocket.record.ownership.epoch && generation==pocket.model_generation && h2h_pocket_can_play(&pocket);
    xSemaphoreGive(state_lock); return allowed;
}
bool h2h_cloud_guarded_write(uint64_t epoch,uint64_t generation,bool (*write)(const void *,size_t,void *),const void *bytes,size_t length,void *ctx) {
    if(!state_lock || !write || xSemaphoreTake(state_lock,portMAX_DELAY)!=pdTRUE)return false;
    full_recover_locked();
    bool ok=!debug_requested && !debug_paused && epoch==pocket.record.ownership.epoch && generation==pocket.model_generation && h2h_pocket_can_play(&pocket) && write(bytes,length,ctx);
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
    xSemaphoreGive(state_lock);  return ok;
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
    if(!debug_requested&&!debug_paused&&h2h_pocket_can_play(&pocket)&&epoch==pocket.record.ownership.epoch&&generation==pocket.model_generation&&stayc_game_save_valid(game,true)){
        live_game=*game;live_settled=settled;live_epoch=epoch;live_generation=generation;live_full_generation=fullsave.generation;live_game_valid=true;
    }
    xSemaphoreGive(state_lock);
}
void h2h_cloud_game_stars(uint64_t epoch,uint64_t generation,uint32_t stars){(void)epoch;(void)generation;(void)stars;}
bool h2h_cloud_wifi(const char *ssid,const char *password){(void)ssid;(void)password;return false;}
bool h2h_cloud_open(void){return true;}
bool h2h_cloud_close(void){return true;}
bool h2h_cloud_confirm(void){return false;}
bool h2h_cloud_refresh(void){return true;}
bool h2h_cloud_forget_wifi(void){return false;}

void h2h_cloud_profile(pocket_profile_view_t *out){
    if(!out||!state_lock||xSemaphoreTake(state_lock,pdMS_TO_TICKS(100))!=pdTRUE){if(out)out->available=false;return;}
    out->enabled=profile.enabled;
    bool owned=h2h_pocket_can_play(&pocket)&&!pocket.record.ownership.pending_unbind[0]&&
        pocket_profile_owned(&profile,pocket.record.ownership.account_id,pocket.record.ownership.epoch);
    if(!owned){out->available=false;out->revision=0;memset(&out->meta,0,sizeof(out->meta));memset(out->pixels,0,sizeof(out->pixels));}
    else if(!out->available||out->revision!=profile.revision||out->meta.epoch!=profile.current.epoch||out->meta.account!=profile.current.account){
        out->available=pocket_profile_pixels(&profile,out->pixels);out->revision=out->available?profile.revision:0;
        if(out->available)out->meta=profile.current;else {memset(&out->meta,0,sizeof(out->meta));memset(out->pixels,0,sizeof(out->pixels));}
    }
    xSemaphoreGive(state_lock);
}
