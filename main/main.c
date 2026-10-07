#include "h2h_model.h"
#include "h2h_audio.h"
#include "h2h_setup.h"
#include "h2h_ui.h"
#include "stayc_game_store.h"
#include "stayc_game_player.h"
#include "stayc_game_ui.h"
static pocket_profile_view_t account_profile;
#include "h2h_tracks.h"
#include "h2h_ble.h"
#include "h2h_cloud.h"
#include "h2h_theme.h"
#include "h2h_resource_runtime.h"
#include "pocket_log.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG="h2h";
static int task_stack_minimum(const char *name) {
    TaskHandle_t handle=xTaskGetHandle(name);
    return handle?(int)uxTaskGetStackHighWaterMark(handle):-1;
}
typedef struct { bsp_btn_t key; bsp_btn_ev_t event; } input_t;
/* Measured board: physical left selects UP; physical right selects DOWN.
 * The same enum values drive left/right in both native minigames. */
_Static_assert((int)BSP_BTN_UP==(int)STAYC_GAME_UP&&(int)BSP_BTN_DOWN==(int)STAYC_GAME_DOWN&&
               (int)BSP_BTN_OK==(int)STAYC_GAME_OK,"Board/game control mapping changed");
static QueueHandle_t input_queue;
typedef struct { uint32_t magic, member; uint64_t epoch; uint32_t decor, reserved; } stayc_preference_t;
static void stayc_preferences_load(uint64_t epoch,stayc_control_t *visuals) {
    nvs_handle_t handle;stayc_preference_t record={0};size_t length=sizeof(record);
    if(nvs_open("stayc_ui",NVS_READONLY,&handle)!=ESP_OK)return;
    esp_err_t result=nvs_get_blob(handle,"selection",&record,&length);nvs_close(handle);
    if(result!=ESP_OK||length!=sizeof(record)||record.magic!=0x53545932||record.epoch!=epoch||record.member>=6||record.reserved||record.decor>255)return;
    for(unsigned i=0;i<4;i++)if(((record.decor>>(i*2))&3u)>2)return;
    visuals->member=record.member;visuals->decor=record.decor;
}
static bool stayc_member_write(const void *bytes,size_t length,void *context) {
    (void)context;nvs_handle_t handle;
    if(nvs_open("stayc_ui",NVS_READWRITE,&handle)!=ESP_OK)return false;
    esp_err_t result=nvs_set_blob(handle,"selection",bytes,length);
    if(result==ESP_OK)result=nvs_commit(handle);
    nvs_close(handle);return result==ESP_OK;
}
static stayc_game_t gameplay;
static stayc_game_store_t gameplay_store;
static stayc_player_t player_controls;
static stayc_game_listening_t listening;
static stayc_control_t preview_visuals;
typedef struct {uint64_t epoch,generation;const h2h_save_t *base;} gameplay_write_context_t;
static bool gameplay_nvs_write(const void *bytes,size_t length,void *context) {
    (void)context;return h2h_cloud_game_record_write_locked(bytes,length);
}
static bool gameplay_commit_locked(const void *bytes,size_t length,void *context) {
    const gameplay_write_context_t *guard=context;
    if(length!=sizeof(stayc_game_save_t))return false;
    if(!stayc_game_store_write(&gameplay_store,bytes,guard->epoch,guard->base,gameplay_nvs_write,NULL))return false;
    /* A restored full record may contain a historical cloud_base. Even a
     * cache duplicate must normalize its current projection in the coherent
     * journal before the later basic-save dedup. The full write itself dedups. */
    uint8_t encoded[164];stayc_game_record_encode(&gameplay_store.persisted,encoded);
    return h2h_cloud_game_record_write_locked(encoded,sizeof(encoded));
}
static bool gameplay_persist(const stayc_game_save_t *save,uint64_t epoch,uint64_t generation,const h2h_save_t *base) {
    gameplay_write_context_t guard={epoch,generation,base};
    /* Serialize even the no-change case: a durable duplicate may finish only
     * while the same owner/model generation is still authorized. */
    return h2h_cloud_guarded_write(epoch,generation,gameplay_commit_locked,save,sizeof(*save),&guard);
}
static void gameplay_load(uint64_t epoch,const h2h_save_t *base) {
    nvs_handle_t handle;uint8_t bytes[STAYC_GAME_RECORD_BYTES];size_t length=sizeof(bytes);
    stayc_game_read_t read=STAYC_GAME_READ_ERROR;
    int journal=h2h_cloud_game_record(epoch,bytes);
    esp_err_t result=journal==0?nvs_open("stayc_game",NVS_READONLY,&handle):ESP_FAIL;
    if(journal==1){read=STAYC_GAME_READ_OK;result=ESP_FAIL;}
    if(result==ESP_ERR_NVS_NOT_FOUND)read=STAYC_GAME_READ_MISSING;
    else if(result==ESP_OK){
        result=nvs_get_blob(handle,"progress",bytes,&length);nvs_close(handle);
        read=result==ESP_OK?STAYC_GAME_READ_OK:result==ESP_ERR_NVS_NOT_FOUND?STAYC_GAME_READ_MISSING:STAYC_GAME_READ_ERROR;
    }
    stayc_game_store_load(&gameplay_store,&gameplay,epoch,base,read,bytes,length);
    if(gameplay_store.blocked){
        stayc_game_init(&gameplay,h2h_track_count,h2h_song_count,esp_random());
        (void)stayc_game_import_base(&gameplay,base,false);
        ESP_LOGW(TAG,"Local gameplay record is read-only; preserving its bytes");
    }
}
static void gameplay_project_model(h2h_model_t *model) {
    stayc_game_project(&gameplay,&model->save);
    model->save.volume=gameplay.save.volume;
    model->track=gameplay.track;model->generation=gameplay.generation;model->want_play=gameplay.want_play;
    model->start_ms=gameplay.start_ms;model->position_ms=gameplay.position_ms;model->duration_ms=gameplay.duration_ms;
    model->countdown_ms=gameplay.countdown_ms;model->audio_error=gameplay.audio_error;
}
static void key_callback(bsp_btn_t key,bsp_btn_ev_t event,void *user) {
    (void)user;
    input_t input={key,event};
    if(input_queue) (void)xQueueSend(input_queue,&input,0);
}
void app_main(void) {
    ESP_LOGI(TAG,"STAYC Pocket / six members / paired full and chorus Opus 16 kbps");
    h2h_model_t model; h2h_init(&model,h2h_track_count,h2h_song_count,esp_random());
    stayc_control_t visuals={0};uint64_t visual_epoch=UINT64_MAX;
    h2h_activation(&model,false);
    stayc_game_init(&gameplay,h2h_track_count,h2h_song_count,esp_random());
    bool nvs_ready=nvs_flash_init()==ESP_OK;
    pocket_log_start();
    if(!nvs_ready)pocket_log_error(POCKET_LOG_ERROR_STORAGE);
    bool cloud_ok=nvs_ready && h2h_cloud_start()==ESP_OK;
    bool storage_ok=cloud_ok;
    if(cloud_ok && h2h_resource_runtime_start()!=ESP_OK) ESP_LOGI(TAG,"Resource slots unavailable; using built-in visuals");
    if(!cloud_ok) ESP_LOGW(TAG,"Account/storage service unavailable; locked");
    uint64_t model_epoch=0, model_generation=0;
    unsigned confirm_action=5; uint64_t confirm_until=0, confirm_epoch=0, confirm_generation=0;
    if(bsp_display_init()!=ESP_OK || !bsp_lvgl_init()) { pocket_log_error(POCKET_LOG_ERROR_DISPLAY);ESP_LOGE(TAG,"Display initialization failed"); return; }
    bsp_display_backlight(70);
    bool battery_ok=bsp_battery_init()==ESP_OK;
    bool audio_ok=true; /* Start audio only for authorized playback. */
    input_queue=xQueueCreate(16,sizeof(input_t));
    bool input_ok=input_queue && bsp_button_init(key_callback,NULL)==ESP_OK;
    if(!input_ok)pocket_log_error(POCKET_LOG_ERROR_INPUT);
    pocket_log_metrics(esp_get_free_heap_size(),esp_get_minimum_free_heap_size(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),uxTaskGetStackHighWaterMark(NULL));
    uint64_t now=(uint64_t)esp_timer_get_time()/1000, last=now, last_render=0,last_battery=0,last_save=now,last_input=now,last_stats=now;
    bool game_was_playable=false, rendered_game=false;
    unsigned handled_generation=0; int battery=-1; bool dimmed=false;
    h2h_save_t persisted; h2h_save_snapshot(&model,&persisted);
    bool account_open=false, ble_ok=false, setup_debug=false, account_rendered=false;
    bool preview_active=false, preview_pending=false;
    uint64_t preview_deadline=0;
    unsigned ble_attempts=0; uint64_t ble_retry_at=0, debug_input_after=0; esp_err_t ble_error=ESP_OK;
    h2h_setup_gesture_t setup_gesture; h2h_setup_gesture_init(&setup_gesture,now);
    for(;;) {
        now=(uint64_t)esp_timer_get_time()/1000;
        uint32_t elapsed=(uint32_t)(now-last); last=now;
        h2h_save_t record_save; uint64_t record_epoch=0, record_generation=0;
        if(!preview_active && !preview_pending && cloud_ok && h2h_cloud_model(&record_save,&record_epoch,&record_generation) && record_generation!=model_generation) {
            unsigned audio_generation=(gameplay.generation>model.generation?gameplay.generation:model.generation)+1;
            bool same_epoch=model_generation&&model_epoch==record_epoch;
            uint32_t normal_settled=gameplay.debug_mode?gameplay.debug_saved_settled:gameplay.settled_claims;
            stayc_game_save_t normal_game;stayc_game_save_snapshot(&gameplay,&normal_game);
            h2h_init(&model,h2h_track_count,h2h_song_count,esp_random());
            storage_ok=h2h_load(&model,&record_save,sizeof(record_save));
            stayc_game_init(&gameplay,h2h_track_count,h2h_song_count,esp_random());
            uint8_t full_record[164];int full_current=h2h_cloud_game_record(record_epoch,full_record);
            if(full_current!=0)gameplay_load(record_epoch,&record_save);
            else if(same_epoch){
                (void)stayc_game_load(&gameplay,&normal_game,sizeof(normal_game));gameplay.settled_claims=normal_settled;
                if(!stayc_game_import_base(&gameplay,&record_save,true))gameplay_store.blocked=true;
                gameplay_store.settled_claims|=gameplay.settled_claims;
            } else gameplay_load(record_epoch,&record_save);
            gameplay.generation=audio_generation;
            memset(&listening,0,sizeof(listening));listening.boundary=true;
            memset(&player_controls,0,sizeof(player_controls));
            model.generation=audio_generation; model_epoch=record_epoch; model_generation=record_generation;
            if(visual_epoch!=record_epoch){memset(&visuals,0,sizeof(visuals));stayc_preferences_load(record_epoch,&visuals);visual_epoch=record_epoch;}
            h2h_save_snapshot(&model,&persisted); confirm_action=5;
        }
        if(cloud_ok && storage_ok && !preview_active && !preview_pending && !gameplay.debug_mode) {
            stayc_game_save_t wallet;stayc_game_save_snapshot(&gameplay,&wallet);
            h2h_cloud_game_stars(model_epoch,model_generation,wallet.star_balance);
            if(!gameplay_store.blocked)h2h_cloud_game_snapshot(model_epoch,model_generation,&wallet,gameplay.settled_claims);
        }
        bool playable=cloud_ok && storage_ok && h2h_cloud_can_play_at(model_epoch,model_generation);
        h2h_activation(&model,playable);
        if(game_was_playable&&!playable&&!preview_active&&!preview_pending){
            gameplay.want_play=false;gameplay.countdown_ms=0;++gameplay.generation;
            visuals.view=STAYC_NORMAL;visuals.paused=false;
        }
        game_was_playable=playable;
        if(playable || (preview_active && gameplay.debug_mode)){
            stayc_game_tick_frame(&gameplay,elapsed);stayc_game_ui_tick(&gameplay,&visuals,elapsed);
            gameplay.duration_ms=h2h_tracks[gameplay.track].duration_ms;
            stayc_player_observe(&player_controls,&gameplay);
            if(!preview_active) gameplay_project_model(&model);
            if(gameplay.page==STAYC_GAME_ACCOUNT&&!gameplay.debug_mode&&model.page!=H2H_ACCOUNT){model.page=H2H_ACCOUNT;model.selection=0;}
        } else h2h_tick(&model,elapsed);
        if(!playable && !preview_active && !preview_pending && !setup_debug && input_ok) {
            bsp_button_snapshot_t sample;
            bool fresh=bsp_button_snapshot(&sample);
            if(h2h_setup_gesture_sample(&setup_gesture,fresh?sample.key:-2,
                (uint64_t)sample.changed_us/1000,(uint64_t)sample.sampled_us/1000,sample.sequence,now)) {
                setup_debug=true; model.selection=0; debug_input_after=now+500;
            }
        } else h2h_setup_gesture_init(&setup_gesture,now);
        input_t input;
        while(input_queue && xQueueReceive(input_queue,&input,0)==pdTRUE) {
            pocket_log_key((uint8_t)input.key+1,(uint8_t)input.event+1);
            last_input=now;
            if(dimmed) { bsp_display_backlight(70); dimmed=false; }
            if(preview_pending) {
                if((input.key==BSP_BTN_OK && input.event==BSP_BTN_LONG) ||
                   (input.key!=BSP_BTN_OK && input.event==BSP_BTN_CLICK)) {
                    preview_pending=false;
                    if(gameplay.debug_mode) stayc_game_debug_set(&gameplay,false);
                    visuals=preview_visuals;
                    if(cloud_ok) (void)h2h_cloud_debug_pause(false);
                    setup_debug=false;
                }
                continue;
            }
            if(preview_active && !gameplay.debug_mode) continue;
            if((playable || preview_active) && gameplay.page==STAYC_GAME_PLAYER && input.key!=BSP_BTN_OK &&
               (input.event==BSP_BTN_DOUBLE || input.event==BSP_BTN_TRIPLE)) {
                unsigned clicks=input.event==BSP_BTN_DOUBLE?2:3;
                while(clicks--) stayc_player_input(&player_controls,&gameplay,(stayc_game_key_t)input.key,STAYC_GAME_CLICK);
                continue;
            }
            if(!playable && !preview_active && !setup_debug) {
                if(input.key==BSP_BTN_OK && input.event==BSP_BTN_CLICK && !setup_gesture.stage) {
                    h2h_cloud_status_t snapshot={0}; if(cloud_ok) h2h_cloud_status(&snapshot);
                    if(snapshot.state==H2H_CLOUD_CLAIMED) (void)h2h_cloud_confirm();
                    else if(cloud_ok) (void)h2h_cloud_open();
                    ble_attempts=0; ble_retry_at=now;
                }
                continue;
            }
            if(!playable && !preview_active && setup_debug && now<debug_input_after) continue;
            if(!playable && !preview_active && setup_debug && input.key==BSP_BTN_OK && input.event==BSP_BTN_LONG) {
                setup_debug=false; h2h_setup_gesture_init(&setup_gesture,now); continue;
            }
            h2h_event_t event;
            if(input.event==BSP_BTN_PRESS) event=H2H_PRESS;
            else if(input.event==BSP_BTN_CLICK) event=H2H_CLICK;
            else if(input.event==BSP_BTN_LONG) event=H2H_LONG;
            else continue;
            if(!playable && !preview_active && setup_debug && model.page==H2H_ACCOUNT) {
                if(input.key!=BSP_BTN_OK) {
                    if(event==H2H_CLICK) model.selection=(model.selection+(input.key==BSP_BTN_DOWN?1u:5u))%6u;
                    continue;
                }
                if(model.selection==5 && event==H2H_CLICK) {
                    preview_pending=true; preview_deadline=now+30000;
                    preview_visuals=visuals;
                    if(cloud_ok) (void)h2h_cloud_debug_pause(true);
                    continue;
                }
            }
            if((!playable||(gameplay.page==STAYC_GAME_ACCOUNT&&!gameplay.debug_mode)) && model.page==H2H_ACCOUNT && input.key==BSP_BTN_OK && event==H2H_CLICK && !model.suppress_ok) {
                if(model.selection==0) {
                    ble_attempts=0; ble_retry_at=now;
                    h2h_cloud_status_t snapshot={0}; h2h_cloud_status(&snapshot);
                    if(snapshot.state==H2H_CLOUD_CLAIMED) (void)h2h_cloud_confirm();
                    else (void)h2h_cloud_open();
                } else if(model.selection==1) (void)h2h_cloud_refresh();
                else if(model.selection==2) (void)h2h_cloud_forget_wifi();
                else if(model.selection==3 || model.selection==4) {
                    // Two physical clicks on the same action; movement/timeout cancels.
                    if(confirm_action==model.selection && now<confirm_until && confirm_epoch==model_epoch && confirm_generation==model_generation) {
                        if(model.selection==3) {
                            (void)h2h_cloud_unbind(model_epoch,model_generation);
                            h2h_activation(&model,false); playable=false;
                        } else {
                            (void)h2h_cloud_restore(confirm_epoch,confirm_generation);
                        }
                        confirm_action=5;
                    } else { confirm_action=model.selection; confirm_until=now+5000; confirm_epoch=model_epoch; confirm_generation=model_generation; }
                }
            }
            if(input.key!=BSP_BTN_OK || event==H2H_LONG) confirm_action=5;
            if((playable || preview_active) && (gameplay.page!=STAYC_GAME_ACCOUNT||gameplay.debug_mode)){
                if(!stayc_game_ui_input(&gameplay,&visuals,(h2h_key_t)input.key,event))
                    stayc_player_input(&player_controls,&gameplay,(stayc_game_key_t)input.key,(stayc_game_event_t)event);
                if(gameplay.page==STAYC_GAME_ACCOUNT&&!gameplay.debug_mode){model.page=H2H_ACCOUNT;model.selection=0;model.suppress_ok=false;}
            } else {
                h2h_input(&model,(h2h_key_t)input.key,event);
                if(playable&&model.page!=H2H_ACCOUNT){stayc_game_account_back(&gameplay);gameplay.suppress_ok=model.suppress_ok;}
            }
        }
        /* Existing bound-account DEV mode uses the same cloud/radio barrier. */
        if(gameplay.debug_mode && !preview_active && !preview_pending) {
            preview_pending=true; preview_deadline=now+30000; preview_visuals=visuals;
        }
        if(preview_pending) {
            bool quiet=!cloud_ok || h2h_cloud_debug_pause(true);
            if(quiet && h2h_audio_stop()==ESP_OK && h2h_ble_stop()==ESP_OK) {
                stayc_game_debug_set(&gameplay,true);
                ++gameplay.generation; gameplay.page=STAYC_GAME_HOME; gameplay.selection=0;
                memset(&listening,0,sizeof(listening)); listening.boundary=true;
                memset(&player_controls,0,sizeof(player_controls));
                preview_active=true; preview_pending=false;
                setup_debug=false; account_open=false; ble_ok=false;
            } else if(now>=preview_deadline) {
                preview_pending=false;
                if(gameplay.debug_mode) stayc_game_debug_set(&gameplay,false);
                visuals=preview_visuals;
                if(cloud_ok) (void)h2h_cloud_debug_pause(false);
            }
        }
        if(preview_active && !gameplay.debug_mode && h2h_audio_stop()==ESP_OK) {
            preview_active=false; visuals=preview_visuals;
            memset(&listening,0,sizeof(listening)); listening.boundary=true;
            memset(&player_controls,0,sizeof(player_controls));
            if(cloud_ok) (void)h2h_cloud_debug_pause(false);
            h2h_setup_gesture_init(&setup_gesture,now);
        }
        bool local_play=preview_active && gameplay.debug_mode;
        bool in_account=preview_pending || (!playable && !local_play) || (gameplay.page==STAYC_GAME_ACCOUNT&&!gameplay.debug_mode);
        if(preview_pending || (preview_active && !gameplay.debug_mode)) {
            model.page=H2H_ACCOUNT; h2h_activation(&model,false);
        }
        if(in_account && !account_open) {
            account_open=true; account_rendered=false;
            ble_attempts=0; ble_retry_at=now; ble_error=ESP_OK;
            if(cloud_ok && !preview_pending && !preview_active) (void)h2h_cloud_open();
        } else if(!in_account && account_open) {
            /* Keep authenticated relay discoverable in the bound experience.
             * A page transition must not destroy the phone's final ACK read. */
            account_open=false;setup_debug=false;
            if(cloud_ok) (void)h2h_cloud_close();
        }
        if(!preview_pending && !preview_active && (in_account?account_rendered:last_render>0) && !ble_ok && cloud_ok && ble_attempts<3 && now>=ble_retry_at) {
            ++ble_attempts; ble_retry_at=now+5000;
            /* Render scan guidance before potentially slow radio startup. */
            ble_error=h2h_audio_stop();
            if(ble_error==ESP_OK) {
                h2h_cloud_status_t snapshot={0}; h2h_cloud_status(&snapshot);
                ble_error=h2h_ble_start(snapshot.device_id);
                ble_ok=ble_error==ESP_OK;
            }
            if(ble_error!=ESP_OK) ESP_LOGW(TAG,"BLE setup attempt %u failed: %s",ble_attempts,esp_err_to_name(ble_error));
        }
        h2h_ble_command_t ble_command;
        while(!preview_pending && !preview_active && ble_ok && h2h_ble_poll(&ble_command)) {
            if(ble_command.type==H2H_BLE_WIFI_CREDENTIALS) (void)h2h_cloud_wifi(ble_command.ssid,ble_command.password);
            else if(ble_command.type==H2H_BLE_THEME_REFRESH) (void)h2h_cloud_refresh();
            memset(&ble_command,0,sizeof(ble_command));
        }
        h2h_audio_status_t audio=h2h_audio_status();
        if(playable || local_play){
            if(audio.generation==gameplay.generation)gameplay.position_ms=audio.position_ms;
            if(audio.generation==gameplay.generation&&gameplay.want_play&&!audio.failed)
                stayc_game_listening_update(&listening,&gameplay,audio.played_samples,audio.generation);
            else {listening.samples=audio.played_samples;listening.pending=0;}
            if(audio.generation==gameplay.generation&&audio.generation!=handled_generation &&
               (audio.failed||(audio.ended&&gameplay.want_play&&!gameplay.audio_error))){
                handled_generation=audio.generation;stayc_player_end(&player_controls,&gameplay,audio.generation,audio.failed);
            }
            if(!audio_ok&&(gameplay.want_play||gameplay.countdown_ms))stayc_player_end(&player_controls,&gameplay,gameplay.generation,true);
            if(!preview_active) gameplay_project_model(&model);
            if(gameplay.shop_status==STAYC_GAME_SHOP_PENDING){
                stayc_game_save_t candidate;unsigned shop_generation=gameplay.shop_generation;uint32_t debug_epoch=gameplay.debug_epoch;
                size_t size=stayc_game_shop_prepare(&gameplay,&candidate);
                if(size&&stayc_game_shop_validate(&gameplay,&candidate,size,shop_generation,debug_epoch)){
                    bool written=gameplay_persist(&candidate,model_epoch,model_generation,&persisted);
                    bool finished=stayc_game_shop_finish(&gameplay,written?&candidate:NULL,written?size:0,
                        written?1:gameplay_store.blocked?2:0,shop_generation,debug_epoch);
                    if(written&&!finished){gameplay_store.blocked=true;ESP_LOGE(TAG,"Durable gameplay transaction could not finish; read-only");}
                }
            }
        } else {listening.samples=audio.played_samples;listening.pending=0;}
        h2h_audio_request_t request=local_play?
            (h2h_audio_request_t){gameplay.generation,gameplay.track,gameplay.save.volume,gameplay.want_play,gameplay.start_ms}:
            (h2h_audio_request_t){model.generation,model.track,model.save.volume,playable && h2h_cloud_can_play_at(model_epoch,model_generation) && model.want_play,model.start_ms};
        bool phone_connected=ble_ok&&h2h_ble_connected();
        if(phone_connected) {
            request.play=false;gameplay.want_play=false;model.want_play=false;
            if(audio_ok)(void)h2h_audio_stop(); /* Free the large decoder task before relay work. */
        }
        if(request.play && !phone_connected && !in_account && !account_open && audio_ok) audio_ok=h2h_audio_start()==ESP_OK;
        if(in_account || account_open) request.play=false;
        h2h_audio_request(&request);
        if(now-last_battery>=10000 || !last_battery) { battery=battery_ok?bsp_battery_soc():-1; last_battery=now; }
        bool should_dim=now-last_input>=30000 && (!(playable || local_play)||(gameplay.page!=STAYC_GAME_GAME&&gameplay.page!=STAYC_GAME_ECHO&&gameplay.page!=STAYC_GAME_LIFE&&gameplay.page!=STAYC_GAME_BADGE));
        if(should_dim!=dimmed) { dimmed=should_dim; bsp_display_backlight(dimmed?20:70); }
        if(now-last_render>=100) {
            if(bsp_lvgl_lock(100)) {
                h2h_cloud_status_t cloud={0};
                if(cloud_ok && !preview_active) h2h_cloud_status(&cloud);
                int passkey=-1; uint32_t pin;
                if(ble_ok && h2h_ble_passkey(&pin)) passkey=(int)pin;
                if(confirm_action<5 && now<confirm_until) snprintf(cloud.message,sizeof(cloud.message),"%s? Click OK again",confirm_action==3?"Unbind":"Restore progress");
                bool account_resources=!preview_active && !preview_pending;
                const h2h_resource_view_t *resources=account_resources?h2h_resource_runtime_view_begin(model_epoch,model_generation):NULL;
                h2h_ui_resources(resources);
                if(cloud_ok&&model.page==H2H_ACCOUNT&&!preview_active)h2h_cloud_profile(&account_profile);
                else {account_profile.available=false;account_profile.revision=0;}
                h2h_ui_profile(&account_profile);
                h2h_ui_setup_state(setup_debug,setup_gesture.pending,ble_error,preview_pending || (preview_active && !gameplay.debug_mode));
                bool show_game=passkey<0 && !preview_pending && (playable || local_play)&&(gameplay.page!=STAYC_GAME_ACCOUNT||gameplay.debug_mode);
                bool actual_phone_connected=ble_ok && h2h_ble_connected();
                stayc_game_ui_phone_connection(actual_phone_connected);
                pocket_log_state(show_game?(uint8_t)gameplay.page:255,(uint8_t)gameplay.game_kind,cloud_ok?(uint8_t)cloud.state:255,cloud.conflict?1:cloud.state==H2H_CLOUD_ERROR?2:0);
                if(show_game!=rendered_game){h2h_ui_reset();stayc_game_ui_reset();rendered_game=show_game;}
                if(show_game)stayc_game_ui_render(&gameplay,gameplay.position_ms,
                    audio.generation==gameplay.generation&&audio.playing,battery,gameplay_store.healthy,
                    gameplay_store.blocked,input_ok,(unsigned)now,&visuals);
                else {
                    /* Show the radio-issued PIN even while a bound card is on
                     * its game page. Rendering does not change saved UI state. */
                    h2h_model_t pin_model=model;
                    if(passkey>=0) pin_model.page=H2H_ACCOUNT;
                    h2h_ui_render(&pin_model,model.position_ms,audio.generation==model.generation&&audio.playing,battery,storage_ok,input_ok,(unsigned)now,
                                  &cloud,actual_phone_connected,passkey,&visuals);
                }
                if(account_resources) h2h_resource_runtime_view_end();
                if(in_account) account_rendered=true;
                bsp_lvgl_unlock();
            }
            last_render=now;
        }
        if(now-last_save >= (gameplay.dirty||visuals.preferences_dirty?3000u:30000u)) {
            if(playable&&!preview_pending&&!preview_active&&!gameplay.debug_mode&&!gameplay_store.blocked){
                stayc_game_save_t extended;stayc_game_save_snapshot(&gameplay,&extended);
                if(gameplay_persist(&extended,model_epoch,model_generation,&persisted)){
                    h2h_save_t save;stayc_game_project(&gameplay,&save);
                    if(memcmp(&persisted,&save,sizeof(save))){
                        storage_ok=h2h_cloud_local_save(model_epoch,model_generation,&save);
                        if(storage_ok){persisted=save;gameplay.dirty=false;}
                        else ESP_LOGW(TAG,"Cloud base save rejected; locked");
                    } else gameplay.dirty=false;
                } else ESP_LOGW(TAG,"Local gameplay save deferred");
            }
            if(playable&&!preview_pending&&!preview_active&&!gameplay.debug_mode&&visuals.preferences_dirty){
                stayc_preference_t preference={.magic=0x53545932,.member=visuals.member,.epoch=model_epoch,.decor=0};
                if(h2h_cloud_guarded_write(model_epoch,model_generation,stayc_member_write,&preference,sizeof(preference),NULL))visuals.preferences_dirty=false;
                else ESP_LOGW(TAG,"Member preference save deferred");
            }
            last_save=now;
        }
        if(now-last_stats>=60000) {
            int audio_stack=task_stack_minimum("h2h_audio"),cloud_stack=task_stack_minimum("h2h_cloud"),lvgl_stack=task_stack_minimum("taskLVGL");
            pocket_log_metrics(esp_get_free_heap_size(),esp_get_minimum_free_heap_size(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),uxTaskGetStackHighWaterMark(NULL));
            ESP_LOGI(TAG,"heap=%u minimum=%u largest=%u stack_min_bytes main=%u audio=%d cloud=%d lvgl=%d tracks=%u",
                     (unsigned)esp_get_free_heap_size(),(unsigned)esp_get_minimum_free_heap_size(),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                     (unsigned)uxTaskGetStackHighWaterMark(NULL),audio_stack,cloud_stack,lvgl_stack,h2h_track_count);
            (void)audio_stack;(void)cloud_stack;(void)lvgl_stack;
            last_stats=now;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
