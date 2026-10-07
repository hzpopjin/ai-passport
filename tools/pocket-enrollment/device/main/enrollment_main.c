/* Temporary, headless USB enrollment application. No legacy secret getter,
 * Flash readback, Wi-Fi/BLE, save parser, erase operation, or key replacement. */
#include "enrollment_core.h"
#include "bsp_button.h"
#include "bsp_pins.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "mbedtls/md.h"
#include <stdio.h>
#include <string.h>

static pe_state_t state;
static nvs_handle_t readonly_handle;
static bool storage_ready;
static void hex_encode(const uint8_t *b,size_t n,char *out){static const char h[]="0123456789abcdef";for(size_t i=0;i<n;i++){out[2*i]=h[b[i]>>4];out[2*i+1]=h[b[i]&15];}out[2*n]=0;}
static bool decode_key(const char *s,uint8_t out[32]){if(!pe_hex(s,64))return false;for(unsigned i=0;i<32;i++){unsigned a=(s[2*i]<='9'?s[2*i]-'0':s[2*i]-'a'+10),b=(s[2*i+1]<='9'?s[2*i+1]-'0':s[2*i+1]-'a'+10);out[i]=(uint8_t)(a*16+b);}return true;}
static int key_size(void *unused){(void)unused;if(!storage_ready)return -2;size_t size=0;esp_err_t e=nvs_get_blob(readonly_handle,"relay_key",NULL,&size);if(e==ESP_ERR_NVS_NOT_FOUND)return -1;if(e!=ESP_OK||size>INT32_MAX)return -2;return (int)size;}
static bool insert_key(const uint8_t key[32],void *unused){
    (void)unused;nvs_handle_t writer;
    if(nvs_open("h2h_cloud",NVS_READWRITE,&writer)!=ESP_OK)return false;
    bool ok=nvs_set_blob(writer,"relay_key",key,32)==ESP_OK&&nvs_commit(writer)==ESP_OK;
    nvs_close(writer);return ok;
}
static void reply(const char *status,const char *proof){
    cJSON *j=cJSON_CreateObject();if(!j)return;
    cJSON_AddNumberToObject(j,"v",1);cJSON_AddStringToObject(j,"status",status);
    cJSON_AddStringToObject(j,"device_id",state.device);cJSON_AddStringToObject(j,"chip_mac",state.mac);
    cJSON_AddStringToObject(j,"helper_elf_sha256",state.image);cJSON_AddStringToObject(j,"nonce",state.nonce);
    cJSON_AddStringToObject(j,"operation_id",state.operation);
    cJSON_AddNumberToObject(j,"key_size",key_size(NULL));
    if(state.error)cJSON_AddStringToObject(j,"error",state.error);
    if(proof)cJSON_AddStringToObject(j,"proof",proof);
    char *s=cJSON_PrintUnformatted(j);cJSON_Delete(j);
    if(s){char line[768];int len=snprintf(line,sizeof(line),"POCKET_ENROLL %s\n",s);cJSON_free(s);if(len>0&&(size_t)len<sizeof(line))usb_serial_jtag_write_bytes(line,len,pdMS_TO_TICKS(100));}
}
static const char *field(cJSON *j,const char *name){cJSON *x=cJSON_GetObjectItemCaseSensitive(j,name);return cJSON_IsString(x)?x->valuestring:NULL;}
static bool schema(cJSON *j,bool install){
    static const char *allowed[]={"v","op","device_id","chip_mac","helper_elf_sha256","nonce","operation_id","key_hex"};
    if(!cJSON_IsObject(j))return false;
    unsigned n=0;
    for(cJSON *x=j->child;x;x=x->next){if(!x->string)return false;bool ok=false;for(unsigned i=0;i<(install?8:7);i++)if(!strcmp(x->string,allowed[i]))ok=true;if(!ok)return false;for(cJSON *y=j->child;y!=x;y=y->next)if(!strcmp(y->string,x->string))return false;n++;}
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,"v");return n==(install?8:7)&&cJSON_IsNumber(v)&&v->valuedouble==1;
}
static bool flat_wire(const char *line){
    /* Canonical host frames need no escaped strings or nesting. Reject them
     * before cJSON recursion, including embedded-NUL/escaped field aliases. */
    bool started=false,closed=false,quoted=false;
    for(const unsigned char *p=(const unsigned char*)line;*p;p++){
        if(*p<32||*p>126)return false;
        if(closed){if(*p!=' ')return false;continue;}
        if(!started){if(*p==' ')continue;if(*p!='{')return false;started=true;continue;}
        if(*p=='\\')return false;
        if(*p=='"'){quoted=!quoted;continue;}
        if(!quoted){if(*p=='{'||*p=='['||*p==']')return false;if(*p=='}')closed=true;}
    }
    return started&&closed&&!quoted;
}
static void handle(char *line){
    const char *end=NULL;cJSON *j=flat_wire(line)?cJSON_ParseWithOpts(line,&end,true):NULL;uint8_t key[32]={0};
    const char *op=j?field(j,"op"):NULL;bool install=op&&!strcmp(op,"install");
    if(!j||!op||(!install&&strcmp(op,"arm"))||!schema(j,install)){reply("invalid_input",NULL);goto out;}
    const char *d=field(j,"device_id"),*m=field(j,"chip_mac"),*i=field(j,"helper_elf_sha256"),*n=field(j,"nonce"),*o=field(j,"operation_id");
    if(!pe_hex(d,32)||!pe_hex(m,12)||!pe_hex(i,64)||!pe_hex(n,32)||!pe_hex(o,32)||strcmp(i,state.image)||strcmp(d,state.device)||strcmp(m,state.mac)||strcmp(n,state.nonce)){
        state.phase=PE_REFUSED;state.error="target_mismatch";reply("refused",NULL);goto out;
    }
    if(!install){pe_arm(&state,d,m,n,o,esp_timer_get_time());reply(pe_phase_name(state.phase),NULL);goto out;}
    if(!decode_key(field(j,"key_hex"),key)){reply("invalid_input",NULL);goto out;}
    /* A fresh BSP sample is mandatory at the instant of mutation as well. */
    bsp_button_snapshot_t snap;if(bsp_button_snapshot(&snap))pe_sample(&state,snap.key,snap.sampled_us,snap.changed_us,snap.sequence,esp_timer_get_time());
    else {state.phase=PE_REFUSED;state.error="button_sample_unavailable";}
    if(pe_install(&state,o,key,esp_timer_get_time())){
        char context[320],proof[65];uint8_t mac[32];int len=snprintf(context,sizeof(context),"pocket-enrollment-v1\n%s\n%s\n%s\n%s\n%s\nwritten",state.device,state.mac,state.image,state.nonce,state.operation);
        const mbedtls_md_info_t *md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if(len>0&&(size_t)len<sizeof(context)&&md&&mbedtls_md_hmac(md,key,32,(const unsigned char*)context,(size_t)len,mac)==0){hex_encode(mac,32,proof);reply("written",proof);}
        else {state.phase=PE_UNCERTAIN;state.error="receipt_unavailable";reply("indeterminate",NULL);}
        pe_zero(mac,sizeof(mac));pe_zero(context,sizeof(context));pe_zero(proof,sizeof(proof));
    }else reply(pe_phase_name(state.phase),NULL);
out:
    pe_zero(key,sizeof(key));
    if(j){for(cJSON *x=j->child;x;x=x->next)if(cJSON_IsString(x))pe_zero(x->valuestring,strlen(x->valuestring));cJSON_Delete(j);}
    pe_zero(line,768);
}
void app_main(void){
    /* Blank the old retained LCD image while this headless helper is active. */
    gpio_reset_pin(BSP_LCD_BL);gpio_set_direction(BSP_LCD_BL,GPIO_MODE_OUTPUT);gpio_set_level(BSP_LCD_BL,0);
    usb_serial_jtag_driver_config_t usb={.tx_buffer_size=2048,.rx_buffer_size=2048};
    if(usb_serial_jtag_driver_install(&usb)!=ESP_OK)return;
    char device[33]={0},mac[13],image[65],nonce[33];uint8_t public_mac[6],random[16];
    if(esp_read_mac(public_mac,ESP_MAC_EFUSE_FACTORY)!=ESP_OK)return;
    hex_encode(public_mac,6,mac);
    hex_encode(esp_app_get_description()->app_elf_sha256,32,image);esp_fill_random(random,16);hex_encode(random,16,nonce);pe_zero(random,16);
    /* Never erase when init reports exhausted pages, version mismatch or error. */
    if(nvs_flash_init()==ESP_OK&&nvs_open("h2h_cloud",NVS_READONLY,&readonly_handle)==ESP_OK){
        size_t length=sizeof(device);storage_ready=nvs_get_str(readonly_handle,"id",device,&length)==ESP_OK&&pe_hex(device,32);
    }
    pe_init(&state,device,mac,image,nonce,(pe_store_t){key_size,insert_key,NULL});
    if(bsp_button_init(NULL,NULL)!=ESP_OK){state.phase=PE_REFUSED;state.error="button_init_failed";}
    char line[768]={0};size_t used=0;bool overflow=false;int64_t hello_at=0;
    for(;;){
        int64_t now=esp_timer_get_time();bsp_button_snapshot_t snap;
        if(state.phase==PE_ARMED||state.phase==PE_CONFIRMED){
            pe_phase_t before=state.phase;
            if(bsp_button_snapshot(&snap))pe_sample(&state,snap.key,snap.sampled_us,snap.changed_us,snap.sequence,now);
            else if(now-state.armed_us>100000){state.phase=PE_REFUSED;state.error="button_sample_unavailable";}
            if(before!=state.phase)reply(pe_phase_name(state.phase),NULL);
        }
        if(now-hello_at>=1000000){reply(pe_phase_name(state.phase),NULL);hello_at=now;}
        uint8_t b;int got=usb_serial_jtag_read_bytes(&b,1,pdMS_TO_TICKS(10));
        if(got==1){
            if(b=='\n'){if(!overflow){line[used]=0;handle(line);}else{pe_zero(line,sizeof(line));reply("input_too_long",NULL);}used=0;overflow=false;}
            else if(!overflow){if(b<32||b>126||used>=sizeof(line)-1){overflow=true;pe_zero(line,sizeof(line));}else line[used++]=(char)b;}
        }
    }
}
