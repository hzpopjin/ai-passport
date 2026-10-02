/* Actual worker with deterministic decoder/scheduler fakes. PCM values encode
 * source sample offsets, so these checks detect pre-skip, pre-roll and trim bugs. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "../main/h2h/h2h_audio.c"
static uint8_t compressed[576];
const h2h_track_t h2h_tracks[]={{"Fixture","Tests",170,compressed,compressed+sizeof(compressed),9,312,8160,64}};
const unsigned h2h_track_count=1;
const unsigned h2h_song_count=1;
static jmp_buf done;
static unsigned scenario,delays,writes,opens,sleeps,decoded,samples,next_sample;
static bool paused,resumed;
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle) {
    (void)name;(void)arg;assert(fn==worker && stack>=8192 && priority>0);*handle=(void *)1;return pdPASS;
}
void vTaskDelay(unsigned ticks) {
    (void)ticks;assert(++delays<1000);
    h2h_audio_status_t s=h2h_audio_status();
    if(s.ended || s.failed) longjmp(done,1);
    if(scenario==8 && !resumed) { assert(!writes && !decoded && s.position_ms==100);requested.play=true;resumed=true; }
    if(paused && !s.playing) { requested.play=true;resumed=true; }
}
esp_err_t bsp_audio_init(void) { return ESP_OK; }
esp_err_t bsp_audio_sleep(void) { ++sleeps; return ESP_OK; }
esp_err_t bsp_audio_wake(void) { return scenario==6?ESP_FAIL:ESP_OK; }
esp_err_t bsp_audio_set_format(uint32_t hz,uint8_t bits,uint8_t channels) { assert(hz==48000 && bits==16 && channels==1);return ESP_OK; }
void bsp_audio_set_volume(uint8_t v) { assert(v==35); }
esp_err_t bsp_audio_write(const void *pcm,size_t bytes) {
    assert(pcm && bytes>0 && bytes<=1920 && bytes%2==0);++writes;
    if(scenario==4) return ESP_FAIL;
    const uint16_t *values=pcm;
    for(unsigned i=0;i<bytes/2;i++) assert(values[i]==next_sample++);
    samples+=bytes/2;
    if(scenario==1 && writes==1) { requested.play=false;paused=true; }
    if(scenario==2 && writes==1) { ++requested.generation;next_sample=312; }
    if(scenario==9 && writes==1) { ++requested.generation;requested.start_ms=100;next_sample=5112; }
    return ESP_OK;
}
int esp_opus_dec_open(void *config,uint32_t size,void **handle) {
    const esp_opus_dec_cfg_t *cfg=config;
    assert(size==sizeof(*cfg) && cfg->sample_rate==48000 && cfg->channel==1 && cfg->frame_duration==3 && !cfg->self_delimited);
    *handle=(void *)1;++opens;return 0;
}
int esp_opus_dec_close(void *h) { assert(h);return 0; }
int esp_opus_dec_decode(void *h,esp_audio_dec_in_raw_t *raw,esp_audio_dec_out_frame_t *out,esp_audio_dec_info_t *info) {
    assert(h && raw->len==64 && out->len>=1920);++decoded;
    if(scenario==3) return -7;
    if(scenario==5) return 0; /* No progress is a fault, never a busy loop. */
    unsigned packet=raw->buffer[0];assert(packet<9);
    uint16_t *values=(uint16_t *)out->buffer;
    for(unsigned i=0;i<960;i++) values[i]=(uint16_t)(packet*960+i);
    raw->consumed=64;out->decoded_size=1920;*info=(esp_audio_dec_info_t){48000,1,16};return 0;
}
int main(void) {
    for(unsigned i=0;i<9;i++) compressed[i*64]=(uint8_t)i;
    assert(h2h_audio_start()==ESP_OK);assert(h2h_audio_start()==ESP_OK);
    for(scenario=0;scenario<=9;scenario++) {
        delays=writes=opens=sleeps=decoded=samples=0;paused=resumed=false;next_sample=scenario==7 || scenario==8?5112:312;
        memset(&status,0,sizeof(status));requested=(h2h_audio_request_t){1,0,35,scenario!=8,scenario==7 || scenario==8?100:0};
        if(!setjmp(done)) worker(NULL);
        h2h_audio_status_t s=h2h_audio_status();
        if(scenario>=3 && scenario<=6) { assert(s.failed && !s.ended && s.played_samples==0); }
        else {
            assert(s.ended && !s.failed && s.position_ms==170 && s.played_samples==samples);
            unsigned expected=scenario==2?8808:scenario==7 || scenario==8?3360:scenario==9?4008:8160;
            assert(samples==expected && next_sample==8472);
            if(scenario==1) assert(resumed && sleeps>=2);
            if(scenario==2 || scenario==9) assert(opens==2 && s.generation==2);
            if(scenario==7 || scenario==8) assert(decoded==9 && writes==4);
            if(scenario==8) assert(resumed);
        }
    }
    puts("H2H audio worker: PCM trim, pause, seek/pre-roll, paused seek, restart and fault recovery PASS");
}
