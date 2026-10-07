#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/h2h/pocket_log.c"
static esp_partition_t partitions[5];
static uint8_t flash[POCKET_LOG_BYTES];
static unsigned reads,writes,erases,created;
static uint32_t heap=70000,largest=65000;
static bool absent,allocation_fail,read_fail,write_fail,erase_fail;
static int64_t now_us;
static int reason=1;
const esp_partition_t *esp_partition_find_first(int t,int s,const char *label){if(absent)return NULL;for(unsigned i=0;i<5;i++)if(partitions[i].type==t&&partitions[i].subtype==s&&!strcmp(label,partitions[i].label))return &partitions[i];return NULL;}
static void bounds(const esp_partition_t *p,size_t off,size_t n){assert(p==&partitions[0]&&p->address==0x7e0000&&off<=sizeof(flash)&&n<=sizeof(flash)-off);}
int esp_partition_read(const esp_partition_t *p,size_t off,void *out,size_t n){bounds(p,off,n);reads++;if(read_fail)return ESP_FAIL;memcpy(out,flash+off,n);return ESP_OK;}
int esp_partition_write(const esp_partition_t *p,size_t off,const void *in,size_t n){bounds(p,off,n);writes++;if(write_fail)return ESP_FAIL;const uint8_t *b=in;for(size_t i=0;i<n;i++){assert((flash[off+i]&b[i])==b[i]);flash[off+i]&=b[i];}return ESP_OK;}
int esp_partition_erase_range(const esp_partition_t *p,size_t off,size_t n){bounds(p,off,n);assert(off%4096==0&&n==4096);erases++;if(erase_fail)return ESP_FAIL;memset(flash+off,255,n);return ESP_OK;}
const esp_app_desc_t *esp_app_get_description(void){static const esp_app_desc_t d={{9,8,7,6,5,4,3,2}};return &d;}
uint32_t esp_get_free_heap_size(void){return heap;}
uint32_t esp_get_minimum_free_heap_size(void){return heap-1000;}
size_t heap_caps_get_largest_free_block(int caps){assert(caps==MALLOC_CAP_8BIT);return largest;}
int64_t esp_timer_get_time(void){return now_us;}
int esp_reset_reason(void){return reason;}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t h){(void)h;return 2048;}
int xTaskCreate(void (*t)(void *),const char *n,unsigned bytes,void *ctx,unsigned p,TaskHandle_t *handle){assert(t==worker&&!strcmp(n,"pocket_log")&&bytes==4096&&!ctx&&p==1);created++;*handle=(void *)1;return allocation_fail?0:pdPASS;}
void vTaskDelay(unsigned ticks){(void)ticks;}
void vTaskDelete(TaskHandle_t h){(void)h;}
static void reset(void) {
    memset(&service,0,sizeof(service));memset(rtc_checkpoint,0,64);memset(flash,255,sizeof(flash));
    partitions[0]=(esp_partition_t){1,0x40,0x7e0000,0x20000,"pocket_logs",false,false};
    partitions[1]=(esp_partition_t){0,0,0x10000,0x7d0000,"factory",false,false};
    partitions[2]=(esp_partition_t){1,2,0x9000,0x6000,"nvs",false,false};
    partitions[3]=(esp_partition_t){1,1,0xf000,0x1000,"phy_init",false,false};
    memset(&partitions[4],0,sizeof(partitions[4]));
    reads=writes=erases=created=0;heap=70000;largest=65000;reason=1;now_us=0;
    absent=allocation_fail=read_fail=write_fail=erase_fail=false;
}
static void forbidden_layouts(void) {
    reset();absent=true;pocket_log_start();assert(!created&&!reads&&!writes&&!erases);
    for(unsigned i=0;i<4;i++)for(unsigned field=0;field<5;field++) {
        reset();if(field==0)partitions[i].address+=4096;if(field==1)partitions[i].size-=4096;if(field==2)partitions[i].encrypted=true;if(field==3)partitions[i].readonly=true;if(field==4)partitions[i].subtype++;
        pocket_log_start();pocket_log_key(1,1);pocket_log_error(7);assert(!created&&!reads&&!writes&&!erases);
    }
    reset();heap=16383;pocket_log_start();assert(!created);
    reset();largest=8191;pocket_log_start();assert(!created);
    reset();allocation_fail=true;pocket_log_start();assert(created==1&&!service.started&&!reads&&!writes&&!erases);
    puts("partition/allocation: default, 20 incompatible layouts and resource failures perform no Flash I/O");
}
static void combined_layout(void) {
    reset();partitions[1].size=0x7c8000;partitions[4]=(esp_partition_t){1,0x41,0x7d8000,0x8000,"pocket_profile",false,false};pocket_log_start();assert(created==1&&!reads&&!writes&&!erases);assert(step());
    reset();partitions[1].size=0x7c8000;pocket_log_start();assert(!created&&!reads&&!writes&&!erases);
    puts("combined profile/log layout: exact new factory/profile accepted; missing profile rejected; all log I/O stays within log partition");
}
static void rtc_and_rate(void) {
    reset();pocket_log_start();assert(created==1&&!reads&&!writes&&!erases);assert(step());assert(service.ring.last.kind==POCKET_LOG_BOOT&&writes==2&&erases==1);
    for(unsigned i=1;i<600;i++){now_us=(int64_t)i*100000;pocket_log_key(1+i%3,1+i%5);pocket_log_state(13,1,7,0);pocket_log_error(6);assert(step());}
    assert(writes==2);now_us=60000000;assert(step());assert(writes==3&&service.ring.last.kind==POCKET_LOG_ERROR&&service.ring.last.snapshot.errors==599);
    assert(service.ring.last.snapshot.heap_kib==70000/1024&&service.ring.last.snapshot.log_stack_64b==32);
    pocket_log_key(2,1);pocket_log_state(16,2,8,2);pocket_log_record_t saved;assert(pocket_log_record_decode(&saved,rtc_checkpoint)&&saved.snapshot.page==16);
    /* Warm-reset captures the RTC page even when it has never reached Flash. */
    memset(&service,0,sizeof(service));reason=4;now_us=0;pocket_log_start();assert(step());
    assert(service.ring.last.boot==2&&service.ring.last.snapshot.reset_reason==4&&service.ring.last.snapshot.page==16);
    /* Torn RTC rejected; power-on uses only last durable Flash context. */
    rtc_checkpoint[30]^=1;memset(&service,0,sizeof(service));reason=1;pocket_log_start();assert(step());
    assert(service.ring.last.boot==3&&service.ring.last.snapshot.reset_reason==1&&service.ring.last.snapshot.page==16);
    puts("RTC/rate: 599 input/error updates cause no hot writes; warm-reset context, corrupt RTC and power-on fallback PASS");
}
static void storage_faults(void) {
    for(unsigned mode=0;mode<3;mode++) {
        reset();pocket_log_start();if(mode==0)read_fail=true;if(mode==1)write_fail=true;if(mode==2)erase_fail=true;
        assert(!step()&&!service.started);unsigned total=reads+writes+erases;
        pocket_log_key(1,1);pocket_log_error(7);assert(!step());assert(total==reads+writes+erases);
    }
    puts("Flash failures: logger stops without retry or touching save/NVS/PHY/application partitions");
}
int main(void){forbidden_layouts();combined_layout();rtc_and_rate();storage_faults();printf("Actual ESP adapter: PASS; static service+RTC+lock=%zu bytes; stack request=4096 bytes\n",sizeof(service)+sizeof(rtc_checkpoint)+sizeof(mux));}
