#include "h2h_audio.h"
#include "h2h_tracks.h"
#include "bsp_audio.h"
#include "esp_opus_dec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG="h2h_audio";
static portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
static h2h_audio_request_t requested;
static h2h_audio_status_t status;
static TaskHandle_t task;
void h2h_audio_request(const h2h_audio_request_t *r) { portENTER_CRITICAL(&mutex); requested=*r; portEXIT_CRITICAL(&mutex); }
static h2h_audio_request_t read_request(void) { portENTER_CRITICAL(&mutex); h2h_audio_request_t r=requested; portEXIT_CRITICAL(&mutex); return r; }
h2h_audio_status_t h2h_audio_status(void) { portENTER_CRITICAL(&mutex); h2h_audio_status_t s=status; portEXIT_CRITICAL(&mutex); return s; }
static void publish(h2h_audio_status_t *s) { portENTER_CRITICAL(&mutex); status=*s; portEXIT_CRITICAL(&mutex); }
static void worker(void *arg) {
    (void)arg;
    uint8_t input[512], output[1920];
    void *decoder=NULL;
    h2h_audio_status_t s={0};
    unsigned generation=0, volume=101, packet_index=0;
    uint64_t target_sample=0;
    bool awake=false, terminal=false;
    esp_err_t init=bsp_audio_init();
    if(init==ESP_OK) init=bsp_audio_sleep();
    for(;;) {
        h2h_audio_request_t r=read_request();
        if(r.generation!=generation) {
            if(decoder) { esp_opus_dec_close(decoder); decoder=NULL; }
            generation=r.generation; terminal=false;
            s.generation=generation; s.position_ms=r.start_ms; s.ended=s.failed=s.playing=false;
            if(r.track>=h2h_track_count) goto fail;
            const h2h_track_t *track=&h2h_tracks[r.track];
            if(!track->packet_bytes || track->packet_bytes>sizeof(input) || !track->packet_count || !track->pcm_samples) goto fail;
            uint64_t target=(uint64_t)r.start_ms*48;
            if(target>track->pcm_samples) target=track->pcm_samples;
            s.position_ms=(uint32_t)(target/48);
            target_sample=track->pre_skip+target;
            /* A 500 ms warm-up was verified across every 16 kbps recording. */
            packet_index=target_sample>24000?(unsigned)((target_sample-24000)/960):0;
            if(awake) { init=bsp_audio_sleep(); awake=false; }
            publish(&s);
        }
        if(!r.play || !generation || terminal) {
            if(awake) {
                esp_err_t sleep_result=bsp_audio_sleep(); awake=false;
                if(sleep_result!=ESP_OK) ESP_LOGE(TAG,"Codec suspend failed: %s",esp_err_to_name(sleep_result));
            }
            s.playing=false; publish(&s); vTaskDelay(pdMS_TO_TICKS(20)); continue;
        }
        if(r.track>=h2h_track_count) goto fail;
        if(!awake) {
            /* Only this worker owns codec lifetime and blocking PCM operations. */
            if(init!=ESP_OK) init=bsp_audio_init();
            if(init!=ESP_OK || bsp_audio_wake()!=ESP_OK || bsp_audio_set_format(48000,16,1)!=ESP_OK) goto fail;
            awake=true; volume=101;
        }
        if(volume!=r.volume) { volume=r.volume; bsp_audio_set_volume((uint8_t)volume); }
        if(!decoder) {
            esp_opus_dec_cfg_t cfg={.sample_rate=48000,.channel=1,.frame_duration=ESP_OPUS_DEC_FRAME_DURATION_20_MS,.self_delimited=false};
            int result=esp_opus_dec_open(&cfg,sizeof(cfg),&decoder);
            if(result!=ESP_AUDIO_ERR_OK) { ESP_LOGE(TAG,"Open Opus failed: %d",result); goto fail; }
        }
        const h2h_track_t *track=&h2h_tracks[r.track];
        if(packet_index>=track->packet_count) {
            s.ended=true; s.playing=false; terminal=true; publish(&s); continue;
        }
        size_t length=(size_t)(track->end-track->start);
        size_t offset=(size_t)packet_index*track->packet_bytes;
        if(!track->packet_bytes || track->packet_bytes>sizeof(input) || offset>length || track->packet_bytes>length-offset) goto fail;
        memcpy(input,track->start+offset,track->packet_bytes);
        esp_audio_dec_in_raw_t raw={.buffer=input,.len=track->packet_bytes};
        esp_audio_dec_out_frame_t out={.buffer=output,.len=sizeof(output)};
        esp_audio_dec_info_t info={0};
        int result=esp_opus_dec_decode(decoder,&raw,&out,&info);
        if(result!=ESP_AUDIO_ERR_OK || raw.consumed!=raw.len || out.decoded_size!=1920 || info.sample_rate!=48000 || info.channel!=1 || info.bits_per_sample!=16) {
            ESP_LOGE(TAG,"Decode failed or invalid frame: %d",result); goto fail;
        }
        uint64_t begin=(uint64_t)packet_index*960, end=begin+960;
        ++packet_index;
        uint64_t keep_begin=begin>target_sample?begin:target_sample;
        uint64_t song_end=(uint64_t)track->pre_skip+track->pcm_samples;
        if(end>song_end) end=song_end;
        if(end>keep_begin) {
            h2h_audio_request_t latest=read_request();
            if(latest.generation!=generation) continue;
            /* Finish one bounded frame on pause, then keep its next position. */
            size_t samples=(size_t)(end-keep_begin);
            if(bsp_audio_write(output+(keep_begin-begin)*2,samples*2)!=ESP_OK) goto fail;
            s.played_samples+=samples;
            s.position_ms=(uint32_t)((end-track->pre_skip)/48); s.playing=true; publish(&s);
        }
        vTaskDelay(1);
        continue;
fail:
        if(decoder) { esp_opus_dec_close(decoder); decoder=NULL; }
        if(awake) { (void)bsp_audio_sleep(); awake=false; }
        s.failed=true; s.playing=false; terminal=true; publish(&s);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
esp_err_t h2h_audio_start(void) {
    if(task) return ESP_OK;
    /* Fixed-size 20 ms Opus packets live consecutively in Flash. */
    return xTaskCreate(worker,"h2h_audio",24576,NULL,5,&task)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
