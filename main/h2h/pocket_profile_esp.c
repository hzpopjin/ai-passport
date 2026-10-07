#include "pocket_profile_esp.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
static bool matches(const esp_partition_t *p,int type,int subtype,uint32_t offset,uint32_t size){return p&&p->type==type&&p->subtype==subtype&&p->address==offset&&p->size==size&&!p->encrypted&&!p->readonly;}
static bool range(uint32_t off,size_t n){return off<=32768&&n<=32768-off;}
static bool read_bytes(uint32_t off,void *b,size_t n,void *ctx){return range(off,n)&&esp_partition_read(ctx,off,b,n)==ESP_OK;}
static bool write_bytes(uint32_t off,const void *b,size_t n,void *ctx){return range(off,n)&&esp_partition_write(ctx,off,b,n)==ESP_OK;}
static bool erase_bytes(uint32_t off,size_t n,void *ctx){return range(off,n)&&off%16384==0&&n==16384&&esp_partition_erase_range(ctx,off,n)==ESP_OK;}
static bool sha_bytes(uint32_t off,size_t n,uint8_t out[32],void *ctx){
    if(!range(off,n)||n!=8192)return false;
    mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);int e=mbedtls_sha256_starts(&hash,0);uint8_t b[512];
    while(!e&&n){size_t part=n<sizeof(b)?n:sizeof(b);if(!read_bytes(off,b,part,ctx)){e=-1;break;}e=mbedtls_sha256_update(&hash,b,part);off+=(uint32_t)part;n-=part;}
    if(!e)e=mbedtls_sha256_finish(&hash,out);
    mbedtls_sha256_free(&hash);return e==0;
}
bool pocket_profile_esp_io(pocket_profile_io_t *io){
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x41,"pocket_profile");
    if(!io||!matches(p,ESP_PARTITION_TYPE_DATA,0x41,0x7d8000,32768)||
       !matches(esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,"factory"),ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,0x10000,0x7c8000)||
       !matches(esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"pocket_logs"),ESP_PARTITION_TYPE_DATA,0x40,0x7e0000,131072)||
       !matches(esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,"nvs"),ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,0x9000,0x6000)||
       !matches(esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_PHY,"phy_init"),ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_PHY,0xf000,0x1000))return false;
    *io=(pocket_profile_io_t){read_bytes,write_bytes,erase_bytes,sha_bytes,(void *)p};return true;
}
