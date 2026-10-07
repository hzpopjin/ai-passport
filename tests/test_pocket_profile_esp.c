#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/h2h/pocket_profile_esp.c"
static esp_partition_t parts[5];static unsigned reads,writes,erases;static uint8_t flash[32768];static bool absent;
const esp_partition_t *esp_partition_find_first(int t,int s,const char *label){if(absent)return NULL;for(unsigned i=0;i<5;i++)if(parts[i].type==t&&parts[i].subtype==s&&!strcmp(parts[i].label,label))return &parts[i];return NULL;}
int esp_partition_read(const esp_partition_t *p,size_t off,void *b,size_t n){assert(p==&parts[4]&&off<=32768&&n<=32768-off);reads++;memcpy(b,flash+off,n);return ESP_OK;}
int esp_partition_write(const esp_partition_t *p,size_t off,const void *b,size_t n){assert(p==&parts[4]&&off<=32768&&n<=32768-off);writes++;memcpy(flash+off,b,n);return ESP_OK;}
int esp_partition_erase_range(const esp_partition_t *p,size_t off,size_t n){assert(p==&parts[4]&&off%16384==0&&n==16384&&off<=16384);erases++;memset(flash+off,255,n);return ESP_OK;}
static void reset(void){parts[0]=(esp_partition_t){1,0x40,0x7e0000,0x20000,"pocket_logs",false,false};parts[1]=(esp_partition_t){0,0,0x10000,0x7c8000,"factory",false,false};parts[2]=(esp_partition_t){1,2,0x9000,0x6000,"nvs",false,false};parts[3]=(esp_partition_t){1,1,0xf000,0x1000,"phy_init",false,false};parts[4]=(esp_partition_t){1,0x41,0x7d8000,0x8000,"pocket_profile",false,false};absent=false;reads=writes=erases=0;}
int main(void){pocket_profile_io_t io;reset();absent=true;assert(!pocket_profile_esp_io(&io));assert(!reads&&!writes&&!erases);
 for(unsigned i=0;i<5;i++)for(unsigned field=0;field<5;field++){reset();if(field==0)parts[i].address+=4096;if(field==1)parts[i].size-=4096;if(field==2)parts[i].encrypted=true;if(field==3)parts[i].readonly=true;if(field==4)parts[i].subtype++;assert(!pocket_profile_esp_io(&io));assert(!reads&&!writes&&!erases);}
 reset();parts[1].size=0x7f0000;assert(!pocket_profile_esp_io(&io));assert(!reads&&!writes&&!erases);
 reset();assert(pocket_profile_esp_io(&io)&&!reads&&!writes&&!erases);uint8_t bytes[512]={0},hash[32];assert(io.erase(0,16384,io.context));assert(io.write(256,bytes,512,io.context));assert(io.read(256,bytes,512,io.context));assert(io.sha(256,8192,hash,io.context));unsigned calls=reads+writes+erases;
 assert(!io.read(32768,bytes,1,io.context)&&!io.write(32768,bytes,1,io.context)&&!io.erase(16384,32768,io.context)&&!io.sha(0,8193,hash,io.context));assert(reads+writes+erases==calls);
 puts("PASS actual ESP profile adapter: minimal/absent + 25 mismatched layouts zero Flash I/O; exact five-entry layout and 32KiB bounds; 512-byte streaming SHA; all I/O confined to profile partition");
}
