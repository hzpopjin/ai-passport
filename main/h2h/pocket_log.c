#include "pocket_log.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define LOG_OFFSET 0x7e0000u
#define LOG_SUBTYPE 0x40u
#define PERIOD_SECONDS 60u
#define WORKER_STACK_BYTES 4096u
typedef struct {
    const esp_partition_t *partition;
    pocket_log_ring_t ring;
    pocket_log_snapshot_t mailbox,previous;
    uint32_t boot;
    uint64_t last_write_seconds;
    TaskHandle_t task;
    bool started,opened,previous_valid,pending_error;
} log_service_t;
static log_service_t service;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static RTC_NOINIT_ATTR uint8_t rtc_checkpoint[64];
_Static_assert(sizeof(log_service_t)+sizeof(rtc_checkpoint)+sizeof(mux)<=1024,"Public log static budget exceeded");

static bool partition_matches(const esp_partition_t *p,esp_partition_type_t type,
                              uint8_t subtype,uint32_t offset,uint32_t size) {
    return p&&p->type==type&&p->subtype==subtype&&p->address==offset&&p->size==size&&!p->encrypted&&!p->readonly;
}
static const esp_partition_t *owned_partition(void) {
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,LOG_SUBTYPE,"pocket_logs");
    const esp_partition_t *factory=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,"factory");
    bool factory_ok=partition_matches(factory,ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,0x10000,0x7d0000)||
        (partition_matches(factory,ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,0x10000,0x7c8000)&&
         partition_matches(esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x41,"pocket_profile"),ESP_PARTITION_TYPE_DATA,0x41,0x7d8000,32768));
    if(!partition_matches(p,ESP_PARTITION_TYPE_DATA,LOG_SUBTYPE,LOG_OFFSET,POCKET_LOG_BYTES)||
       !factory_ok||
       !partition_matches(esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,"nvs"),ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,0x9000,0x6000)||
       !partition_matches(esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_PHY,"phy_init"),ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_PHY,0xf000,0x1000))return NULL;
    return p;
}
static bool in_range(uint32_t off,size_t n){return off<=POCKET_LOG_BYTES&&n<=POCKET_LOG_BYTES-off;}
static bool read_log(uint32_t off,void *bytes,size_t n,void *ctx){return in_range(off,n)&&esp_partition_read(ctx,off,bytes,n)==ESP_OK;}
static bool write_log(uint32_t off,const void *bytes,size_t n,void *ctx){return in_range(off,n)&&esp_partition_write(ctx,off,bytes,n)==ESP_OK;}
static bool erase_log(uint32_t off,size_t n,void *ctx){return off%4096==0&&n==4096&&in_range(off,n)&&esp_partition_erase_range(ctx,off,n)==ESP_OK;}
static uint16_t bucket(uint32_t bytes,unsigned shift){uint32_t value=bytes>>shift;return value>UINT16_MAX?UINT16_MAX:(uint16_t)value;}
static uint64_t seconds(void){int64_t us=esp_timer_get_time();return us>0?(uint64_t)us/1000000:0;}
static uint32_t uptime(void){uint64_t value=seconds();return value>UINT32_MAX?UINT32_MAX:(uint32_t)value;}
/* Call under the short mailbox lock. No Flash, allocation or LVGL access. */
static void checkpoint(void) {
    pocket_log_record_t public_record={.sequence=1,.boot=1,.kind=POCKET_LOG_CHECKPOINT,.snapshot=service.mailbox};
    public_record.snapshot.uptime_seconds=uptime();
    (void)pocket_log_record_encode(&public_record,rtc_checkpoint);
}
static void disable(void){portENTER_CRITICAL(&mux);service.started=false;portEXIT_CRITICAL(&mux);}
static pocket_log_snapshot_t current(bool *pending) {
    pocket_log_snapshot_t s;portENTER_CRITICAL(&mux);s=service.mailbox;
    if(pending){*pending=service.pending_error;service.pending_error=false;}
    portEXIT_CRITICAL(&mux);
    s.uptime_seconds=uptime();s.heap_kib=bucket(esp_get_free_heap_size(),10);
    s.minimum_kib=bucket(esp_get_minimum_free_heap_size(),10);
    s.largest_kib=bucket(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),10);
    s.log_stack_64b=bucket((uint32_t)uxTaskGetStackHighWaterMark(NULL),6);return s;
}
/* Worker-only and factored for fault-injection tests. A failure disables just
 * this logger; never retry uncertain writes or change ownership/save state. */
static bool step(void) {
    if(!service.started)return false;
    if(!service.opened) {
        pocket_log_io_t io={read_log,write_log,erase_log,(void *)service.partition,POCKET_LOG_BYTES};
        if(!pocket_log_ring_open(&service.ring,&io)){disable();return false;}
        if(service.ring.has_last&&service.ring.last.boot==UINT32_MAX){disable();return false;}
        service.boot=service.ring.has_last?service.ring.last.boot+1:1;
        pocket_log_snapshot_t s=current(NULL);
        if(service.previous_valid)s=service.previous;
        else if(service.ring.has_last)s=service.ring.last.snapshot;
        s.reset_reason=(uint8_t)esp_reset_reason();
        memcpy(s.firmware_elf_prefix,esp_app_get_description()->app_elf_sha256,8);
        if(!pocket_log_ring_append(&service.ring,POCKET_LOG_BOOT,service.boot,&s)){disable();return false;}
        service.opened=true;service.last_write_seconds=seconds();return true;
    }
    uint64_t now=seconds();if(now<service.last_write_seconds||now-service.last_write_seconds<PERIOD_SECONDS)return true;
    bool error=false;pocket_log_snapshot_t s=current(&error);
    if(!pocket_log_ring_append(&service.ring,error?POCKET_LOG_ERROR:POCKET_LOG_CHECKPOINT,service.boot,&s)){disable();return false;}
    service.last_write_seconds=now;return true;
}
static void worker(void *ctx){(void)ctx;while(step())vTaskDelay(pdMS_TO_TICKS(1000));vTaskDelete(NULL);}
void pocket_log_start(void) {
    /* Exactly once in app_main, before any setter. Scan and all I/O run in
     * the background. Unknown layout leaves the current firmware untouched. */
    if(service.started||service.partition)return;
    const esp_partition_t *p=owned_partition();if(!p)return;
    if(esp_get_free_heap_size()<16384||heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<8192)return;
    pocket_log_record_t previous={0};bool valid=pocket_log_record_decode(&previous,rtc_checkpoint);
    memset(&service,0,sizeof(service));service.partition=p;
    service.previous_valid=valid&&esp_reset_reason()!=ESP_RST_POWERON;
    if(service.previous_valid)service.previous=previous.snapshot;
    service.mailbox.page=255;service.mailbox.cloud_state=255;
    service.mailbox.reset_reason=(uint8_t)esp_reset_reason();
    memcpy(service.mailbox.firmware_elf_prefix,esp_app_get_description()->app_elf_sha256,8);
    service.started=true;
    if(xTaskCreate(worker,"pocket_log",WORKER_STACK_BYTES,NULL,1,&service.task)!=pdPASS)disable();
}
void pocket_log_key(uint8_t key,uint8_t event) {
    if(key>3||event>5)return;
    portENTER_CRITICAL(&mux);if(service.started){service.mailbox.key=key;service.mailbox.event=event;checkpoint();}portEXIT_CRITICAL(&mux);
}
void pocket_log_state(uint8_t page,uint8_t game,uint8_t cloud,uint8_t result) {
    if((page>31&&page!=255)||game>2||(cloud>8&&cloud!=255)||result>2)return;
    portENTER_CRITICAL(&mux);
    if(service.started){service.mailbox.page=page;service.mailbox.game=game;service.mailbox.cloud_state=cloud;service.mailbox.result=result;checkpoint();}
    portEXIT_CRITICAL(&mux);
}
void pocket_log_metrics(uint32_t heap,uint32_t minimum,uint32_t largest,uint32_t main_stack) {
    portENTER_CRITICAL(&mux);
    if(service.started){service.mailbox.heap_kib=bucket(heap,10);service.mailbox.minimum_kib=bucket(minimum,10);service.mailbox.largest_kib=bucket(largest,10);service.mailbox.main_stack_64b=bucket(main_stack,6);checkpoint();}
    portEXIT_CRITICAL(&mux);
}
void pocket_log_error(uint8_t code) {
    if(code<1||code>POCKET_LOG_ERROR_INTERNAL)return;
    portENTER_CRITICAL(&mux);
    if(service.started){service.mailbox.error=code;if(service.mailbox.errors<UINT16_MAX)service.mailbox.errors++;service.pending_error=true;checkpoint();}
    portEXIT_CRITICAL(&mux);
}
