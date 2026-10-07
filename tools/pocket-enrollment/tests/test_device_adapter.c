/* Compile actual enrollment_main.c against metadata-only NVS/USB/ADC probes,
 * genuine cJSON and OpenSSL HMAC. Fixed PUBLIC synthetic key only. */
#include "test_platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../device/main/enrollment_main.c"
static const char *D="112233445566778899aabbccddeeff00",*M="112233445566",*I="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",*N="ffeeddccbbaa99887766554433221100",*O="00112233445566778899aabbccddeeff";
static const char *KHEX="000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
static int present,sets,commits,ro_open,rw_open,fail_commit;
static unsigned char stored[32],save[164];
static char output[1024];
static int64_t clock_us;
static bsp_button_snapshot_t button;
int usb_serial_jtag_write_bytes(const void*p,size_t n,int timeout){(void)timeout;assert(n<sizeof(output));memcpy(output,p,n);output[n]=0;assert(!strstr(output,KHEX));return (int)n;}
int usb_serial_jtag_read_bytes(void*p,size_t n,int t){(void)p;(void)n;(void)t;assert(false);return 0;}
int usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t*c){(void)c;return ESP_OK;}
int nvs_flash_init(void){return ESP_OK;}
int nvs_open(const char*s,int mode,nvs_handle_t*h){assert(!strcmp(s,"h2h_cloud"));if(mode==NVS_READONLY)ro_open++;else rw_open++;*h=17;return ESP_OK;}
int nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*n){assert(h==17&&!strcmp(k,"relay_key")&&out==NULL);if(present<0)return ESP_ERR_NVS_NOT_FOUND;*n=(size_t)present;return ESP_OK;}
int nvs_get_str(nvs_handle_t h,const char*k,char*out,size_t*n){assert(h==17&&!strcmp(k,"id")&&*n>=33);memcpy(out,D,33);*n=33;return ESP_OK;}
int nvs_set_blob(nvs_handle_t h,const char*k,const void*in,size_t n){assert(h==17&&!strcmp(k,"relay_key")&&n==32&&present==-1);memcpy(stored,in,n);present=32;sets++;return ESP_OK;}
int nvs_commit(nvs_handle_t h){assert(h==17);commits++;return fail_commit?-1:ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==17);}
int64_t esp_timer_get_time(void){return clock_us;}
int esp_read_mac(uint8_t*p,int mode){assert(mode==ESP_MAC_EFUSE_FACTORY);for(int i=0;i<6;i++)p[i]=(uint8_t)(0x11*(i+1));return ESP_OK;}
void esp_fill_random(void*p,size_t n){memset(p,0x55,n);}
const esp_app_desc_t*esp_app_get_description(void){static esp_app_desc_t d;return &d;}
bool bsp_button_snapshot(bsp_button_snapshot_t*out){*out=button;return true;}
int bsp_button_init(void*a,void*b){(void)a;(void)b;return ESP_OK;}
int gpio_reset_pin(int p){assert(p==21);return 0;}
int gpio_set_direction(int p,int mode){assert(p==21&&mode==GPIO_MODE_OUTPUT);return 0;}
int gpio_set_level(int p,int level){assert(p==21&&level==0);return 0;}
const mbedtls_md_info_t*mbedtls_md_info_from_type(int type){assert(type==256);return EVP_sha256();}
int mbedtls_md_hmac(const mbedtls_md_info_t*md,const unsigned char*k,size_t n,const unsigned char*data,size_t len,unsigned char*out){unsigned size=0;return HMAC(md,k,(int)n,data,len,out,&size)&&size==32?0:-1;}
static void reset(void){present=-1;sets=commits=ro_open=rw_open=fail_commit=0;storage_ready=true;readonly_handle=17;clock_us=1000;memset(save,0x5a,sizeof(save));memset(output,0,sizeof(output));pe_init(&state,D,M,I,N,(pe_store_t){key_size,insert_key,NULL});}
static void message(const char*op,bool install){char line[768];snprintf(line,sizeof(line),"{\"v\":1,\"op\":\"%s\",\"device_id\":\"%s\",\"chip_mac\":\"%s\",\"helper_elf_sha256\":\"%s\",\"nonce\":\"%s\",\"operation_id\":\"%s\"%s}",op,D,M,I,N,O,install?",\"key_hex\":\"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\"":"");handle(line);for(size_t i=0;i<sizeof(line);i++)assert(line[i]==0);}
static void confirmation(void){message("arm",false);assert(state.phase==PE_ARMED);pe_sample(&state,-1,2000,2000,1,2000);pe_sample(&state,2,10000,10000,2,10000);pe_sample(&state,-1,3010000,3010000,3,3010000);clock_us=3020000;button=(bsp_button_snapshot_t){-1,3020000,3010000,3};assert(state.phase==PE_CONFIRMED);}
int main(void){int count=0;
 reset();confirmation();message("install",true);assert(sets==1&&commits==1&&rw_open==1&&state.phase==PE_WRITTEN);for(int i=0;i<32;i++)assert(stored[i]==i);for(size_t i=0;i<sizeof(save);i++)assert(save[i]==0x5a);assert(strstr(output,"\"status\":\"written\""));cJSON*j=cJSON_Parse(output+14);const char*proof=cJSON_GetObjectItemCaseSensitive(j,"proof")->valuestring;char ctx[320],expected[65];unsigned char digest[32];unsigned len=0;int n=snprintf(ctx,sizeof(ctx),"pocket-enrollment-v1\n%s\n%s\n%s\n%s\n%s\nwritten",D,M,I,N,O);HMAC(EVP_sha256(),stored,32,(unsigned char*)ctx,n,digest,&len);hex_encode(digest,32,expected);assert(!strcmp(proof,expected));cJSON_Delete(j);message("install",true);assert(sets==1);count++;
 reset();message("install",true);assert(sets==0&&rw_open==0);count++;
 reset();present=32;message("arm",false);assert(state.phase==PE_REFUSED&&sets==0&&rw_open==0);count++;
 reset();confirmation();fail_commit=1;message("install",true);assert(present==32&&state.phase==PE_UNCERTAIN&&sets==1&&strstr(output,"indeterminate")&&!strstr(output,"\"proof\""));count++;
 const char*bad[]={"{\"v\":1,\"v\":1,\"op\":\"arm\"}","{\"v\":1,\"\\u0076\":1,\"op\":\"arm\"}","{\"v\":1,\"op\":\"erase\"}","{} trailing","{\"v\":1,\"op\":\"arm\",\"secret\":\"NO_READ\"}","[]","{\"x\":{\"x\":{}}}","{\"v\":1,\"op\":\"arm\\u0000install\"}"};
 for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++){reset();char line[768];strcpy(line,bad[i]);handle(line);assert(sets==0&&rw_open==0&&strstr(output,"invalid_input"));count++;}
 reset();char nested[768];memset(nested,'[',383);memset(nested+383,']',383);nested[766]=0;handle(nested);assert(sets==0&&strstr(output,"invalid_input"));count++;
 reset();confirmation();button.sampled_us=2800000;message("install",true);assert(sets==0&&state.phase==PE_REFUSED);count++;
 reset();confirmation();char line[768];snprintf(line,sizeof(line),"{\"v\":1,\"op\":\"install\",\"device_id\":\"%s\",\"chip_mac\":\"000000000000\",\"helper_elf_sha256\":\"%s\",\"nonce\":\"%s\",\"operation_id\":\"%s\",\"key_hex\":\"%s\"}",D,I,N,O,KHEX);handle(line);assert(sets==0&&state.phase==PE_REFUSED);count++;
 printf("PASS %d actual device-adapter synthetic scenarios; old secret getters forbidden\n",count);return 0;
}
