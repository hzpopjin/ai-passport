#include "h2h_resource_protocol.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

bool h2h_resource_locator_valid(const char *url,const unsigned char sha[32]) {
    if(!url||!sha||strlen(url)>=H2H_RESOURCE_URL_MAX)return false;
    char prefix[192],hex[65];
    for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",sha[i]);
    int n=snprintf(prefix,sizeof(prefix),"https://apps.randomdance.cn/api/v1/passport/relay/v2/resources/%s/blob?ticket=",hex);
    if(n<0||(size_t)n>=sizeof(prefix)||strncmp(url,prefix,(size_t)n))return false;
    const char *ticket=url+n;size_t length=strlen(ticket);
    if(length<16||length>256)return false;
    for(size_t i=0;i<length;i++) {
        char c=ticket[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    }
    return true;
}
bool h2h_resource_http_range_valid(int status,uint32_t offset,uint32_t length,size_t received,
                                   const char *range,const char *encoding) {
    if(status!=206||!length||length>1024||offset>=H2H_RESOURCE_BYTES||
       length>H2H_RESOURCE_BYTES-offset||received!=length||!range||
       (encoding&&strcmp(encoding,"identity")))return false;
    char expected[80];
    snprintf(expected,sizeof(expected),"bytes %" PRIu32 "-%" PRIu32 "/%u",offset,offset+length-1,H2H_RESOURCE_BYTES);
    return !strcmp(expected,range);
}
