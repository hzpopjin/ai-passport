#include "h2h_model.h"
#include "h2h_audio.h"
#include "h2h_ui.h"
#include "h2h_tracks.h"
#include "h2h_ble.h"
#include "h2h_cloud.h"
#include "h2h_theme.h"
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

static const char *TAG="h2h";
typedef struct { bsp_btn_t key; bsp_btn_ev_t event; } input_t;
static QueueHandle_t input_queue;
static void key_callback(bsp_btn_t key,bsp_btn_ev_t event,void *user) {
    (void)user;
    input_t input={key,event};
    if(input_queue) (void)xQueueSend(input_queue,&input,0);
}
void app_main(void) {
    ESP_LOGI(TAG,"H2H Pocket / IAN / full and chorus Opus 16 kbps");
    h2h_model_t model; h2h_init(&model,h2h_track_count,h2h_song_count,esp_random());
    nvs_handle_t store=0;
    bool storage_ok=false, nvs_ready=false;
    esp_err_t e=nvs_flash_init();
    if(e==ESP_OK) {
        nvs_ready=true; e=nvs_open("h2h_pocket",NVS_READWRITE,&store);
        if(e==ESP_OK) {
            h2h_save_t save; size_t length=sizeof(save);
            e=nvs_get_blob(store,"state",&save,&length);
            if(e==ESP_OK && !h2h_load(&model,&save,length)) ESP_LOGW(TAG,"Invalid saved state: using defaults");
            storage_ok=e==ESP_OK || e==ESP_ERR_NVS_NOT_FOUND;
        }
    }
    if(!storage_ok) ESP_LOGW(TAG,"Storage unavailable; preserving existing NVS (no erase)");
    bool cloud_ok=nvs_ready && h2h_cloud_start()==ESP_OK;
    if(!cloud_ok) ESP_LOGW(TAG,"Account service unavailable");
    if(bsp_display_init()!=ESP_OK || !bsp_lvgl_init()) { ESP_LOGE(TAG,"Display initialization failed"); return; }
    bsp_display_backlight(70);
    bool battery_ok=bsp_battery_init()==ESP_OK;
    bool audio_ok=h2h_audio_start()==ESP_OK;
    input_queue=xQueueCreate(16,sizeof(input_t));
    bool input_ok=input_queue && bsp_button_init(key_callback,NULL)==ESP_OK;
    uint64_t now=(uint64_t)esp_timer_get_time()/1000, last=now, last_render=0,last_battery=0,last_save=now,last_input=now,last_stats=now;
    uint64_t accounted_samples=0, pending_samples=0;
    unsigned handled_generation=0; int battery=-1; bool dimmed=false;
    h2h_save_t persisted; h2h_save_snapshot(&model,&persisted);
    bool account_open=false, ble_ok=false;
    for(;;) {
        now=(uint64_t)esp_timer_get_time()/1000;
        uint32_t elapsed=(uint32_t)(now-last); last=now; h2h_tick(&model,elapsed);
        model.duration_ms=h2h_tracks[model.track].duration_ms;
        input_t input;
        while(input_queue && xQueueReceive(input_queue,&input,0)==pdTRUE) {
            last_input=now;
            if(dimmed) { bsp_display_backlight(70); dimmed=false; }
            if(model.page==H2H_PLAYER && input.key!=BSP_BTN_OK &&
               (input.event==BSP_BTN_DOUBLE || input.event==BSP_BTN_TRIPLE)) {
                unsigned clicks=input.event==BSP_BTN_DOUBLE?2:3;
                while(clicks--) h2h_input(&model,(h2h_key_t)input.key,H2H_CLICK);
                continue;
            }
            h2h_event_t event;
            if(input.event==BSP_BTN_PRESS) event=H2H_PRESS;
            else if(input.event==BSP_BTN_CLICK) event=H2H_CLICK;
            else if(input.event==BSP_BTN_LONG) event=H2H_LONG;
            else continue;
            if(model.page==H2H_ACCOUNT && input.key==BSP_BTN_OK && event==H2H_CLICK && !model.suppress_ok) {
                if(model.selection==0) {
                    h2h_cloud_status_t snapshot={0}; h2h_cloud_status(&snapshot);
                    if(snapshot.state==H2H_CLOUD_CLAIMED) (void)h2h_cloud_confirm();
                    else (void)h2h_cloud_open();
                } else if(model.selection==1) (void)h2h_cloud_refresh();
                else (void)h2h_cloud_forget_wifi();
            }
            h2h_input(&model,(h2h_key_t)input.key,event);
        }
        if(model.page==H2H_ACCOUNT && !account_open) {
            account_open=true;
            if(cloud_ok) {
                h2h_cloud_status_t snapshot={0}; h2h_cloud_status(&snapshot);
                (void)h2h_cloud_open();
                ble_ok=h2h_ble_start(snapshot.device_id)==ESP_OK;
            }
        } else if(model.page!=H2H_ACCOUNT && account_open) {
            account_open=false;
            if(ble_ok) { h2h_ble_stop(); ble_ok=false; }
            if(cloud_ok) (void)h2h_cloud_close();
        }
        h2h_ble_command_t ble_command;
        while(ble_ok && h2h_ble_poll(&ble_command)) {
            if(ble_command.type==H2H_BLE_WIFI_CREDENTIALS) (void)h2h_cloud_wifi(ble_command.ssid,ble_command.password);
            else if(ble_command.type==H2H_BLE_THEME_REFRESH) (void)h2h_cloud_refresh();
            memset(&ble_command,0,sizeof(ble_command));
        }
        h2h_theme_t theme;
        if(cloud_ok && h2h_cloud_poll_theme(&theme)) {
            uint32_t old_outfit=model.save.outfit, old_room=model.save.room;
            h2h_theme_apply(&model,&theme);
            h2h_save_t save; h2h_save_snapshot(&model,&save);
            bool saved=false;
            if(store) {
                e=nvs_set_blob(store,"state",&save,sizeof(save));
                if(e==ESP_OK) e=nvs_commit(store);
                saved=e==ESP_OK;
            }
            if(saved) { persisted=save; model.dirty=false; }
            else { model.save.outfit=old_outfit; model.save.room=old_room; model.dirty=false; }
            h2h_cloud_theme_applied(saved);
        }
        h2h_audio_status_t audio=h2h_audio_status();
        if(audio.generation==model.generation) model.position_ms=audio.position_ms;
        if(audio.played_samples>=accounted_samples) pending_samples+=audio.played_samples-accounted_samples;
        accounted_samples=audio.played_samples;
        if(pending_samples>=48) { h2h_listened(&model,(uint32_t)(pending_samples/48)); pending_samples%=48; }
        if(audio.generation==model.generation && audio.generation!=handled_generation && (audio.ended || audio.failed)) {
            handled_generation=audio.generation; h2h_audio_end(&model,audio.generation,audio.failed);
        }
        if(!audio_ok && (model.want_play || model.countdown_ms)) h2h_audio_end(&model,model.generation,true);
        h2h_audio_request_t request={model.generation,model.track,model.save.volume,model.want_play,model.start_ms};
        h2h_audio_request(&request);
        if(now-last_battery>=10000 || !last_battery) { battery=battery_ok?bsp_battery_soc():-1; last_battery=now; }
        bool should_dim=now-last_input>=30000 && model.page!=H2H_GAME && model.page!=H2H_BADGE;
        if(should_dim!=dimmed) { dimmed=should_dim; bsp_display_backlight(dimmed?20:70); }
        if(now-last_render>=100) {
            if(bsp_lvgl_lock(100)) {
                h2h_cloud_status_t cloud={0};
                if(cloud_ok) h2h_cloud_status(&cloud);
                int passkey=-1; uint32_t pin;
                if(ble_ok && h2h_ble_passkey(&pin)) passkey=(int)pin;
                h2h_ui_render(&model,model.position_ms,audio.generation==model.generation && audio.playing,battery,storage_ok,input_ok,(unsigned)now,
                              &cloud,ble_ok && h2h_ble_connected(),passkey);
                bsp_lvgl_unlock();
            }
            last_render=now;
        }
        if(now-last_save >= (model.dirty?3000u:30000u)) {
            h2h_save_t save; h2h_save_snapshot(&model,&save);
            if(!store && nvs_ready) storage_ok=nvs_open("h2h_pocket",NVS_READWRITE,&store)==ESP_OK;
            if(store && (memcmp(&persisted,&save,sizeof(save)) || !storage_ok)) {
                e=nvs_set_blob(store,"state",&save,sizeof(save));
                if(e==ESP_OK) e=nvs_commit(store);
                storage_ok=e==ESP_OK;
                if(storage_ok) { persisted=save; model.dirty=false; }
                else ESP_LOGW(TAG,"Save failed: %s",esp_err_to_name(e));
            }
            if(storage_ok) model.dirty=false;
            last_save=now;
        }
        if(now-last_stats>=60000) {
            ESP_LOGI(TAG,"heap=%u largest=%u tracks=%u",(unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),h2h_track_count);
            last_stats=now;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
