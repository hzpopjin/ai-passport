#include "h2h_resource_tls.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define HOST "apps.randomdance.cn"
#define HEADER_MAX 2048u
#define ATTEMPT_MS 15000u
static bool pending(int n){return n==ESP_TLS_ERR_SSL_WANT_READ||n==ESP_TLS_ERR_SSL_WANT_WRITE;}
static bool scoped(const h2h_resource_job_t *job,uint32_t started,h2h_resource_clock_fn clock,
                   h2h_resource_scope_fn alive,void *ctx){
    uint32_t now=clock(ctx);
    return (uint32_t)(now-started)<ATTEMPT_MS&&job->session_ttl_ms==300000&&
        (uint32_t)(now-job->session_started_ms)<job->session_ttl_ms&&job->locator_ttl_ms>0&&job->locator_ttl_ms<=300000&&
        (uint32_t)(now-job->locator_started_ms)<job->locator_ttl_ms&&alive(job,ctx);
}
static bool headers(char *bytes,const h2h_resource_job_t *job){
    char *line=strstr(bytes,"\r\n");if(!line)return false;
    *line=0;
    if((strncmp(bytes,"HTTP/1.1 206",12)&&strncmp(bytes,"HTTP/1.0 206",12))||
       (bytes[12]&&bytes[12]!=' '))return false;
    char *range=NULL,*encoding=NULL,*length=NULL;bool transfer=false;
    for(char *p=line+2;*p;){
        char *end=strstr(p,"\r\n");if(!end)return false;*end=0;
        if(!*p)break;
        char *colon=strchr(p,':');if(!colon||colon==p)return false;*colon=0;
        for(char *k=p;*k;k++)if(!((*k>='a'&&*k<='z')||(*k>='A'&&*k<='Z')||(*k>='0'&&*k<='9')||strchr("!#$%&'*+-.^_`|~",*k)))return false;
        char *value=colon+1;while(*value==' '||*value=='\t')value++;
        char *tail=end;while(tail>value&&(tail[-1]==' '||tail[-1]=='\t'))*--tail=0;
        if(!strcasecmp(p,"Content-Range")){if(range)return false;range=value;}
        else if(!strcasecmp(p,"Content-Encoding")){if(encoding)return false;encoding=value;}
        else if(!strcasecmp(p,"Content-Length")){if(length)return false;length=value;}
        else if(!strcasecmp(p,"Transfer-Encoding"))transfer=true;
        p=end+2;
    }
    char expected[16];snprintf(expected,sizeof(expected),"%" PRIu32,job->length);
    return !transfer&&length&&!strcmp(length,expected)&&
        h2h_resource_http_range_valid(206,job->offset,job->length,job->length,range,encoding);
}
bool h2h_resource_tls_get(const h2h_resource_job_t *job,void *out,
    h2h_resource_clock_fn clock,h2h_resource_scope_fn alive,void *ctx){
    if(!job||!out||!clock||!alive||!job->length||job->length>1024||job->offset>=H2H_RESOURCE_BYTES||
       job->length>H2H_RESOURCE_BYTES-job->offset||!h2h_resource_locator_valid(job->locator,job->manifest.sha256))return false;
    uint32_t started=clock(ctx);if(!scoped(job,started,clock,alive,ctx))return false;
    char *header=malloc(HEADER_MAX+1),*request=malloc(1024);
    if(!header||!request){free(header);free(request);return false;}
    const char *path=job->locator+sizeof("https://" HOST)-1;
    int request_length=snprintf(request,1024,"GET %s HTTP/1.1\r\nHost: " HOST "\r\nRange: bytes=%" PRIu32 "-%" PRIu32 "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",path,job->offset,job->offset+job->length-1);
    esp_tls_t *tls=esp_tls_init();bool ok=tls&&request_length>0&&request_length<1024;
    esp_tls_cfg_t config={.non_block=true,.timeout_ms=8000,.crt_bundle_attach=esp_crt_bundle_attach,.skip_common_name=false};
    while(ok){
        if(!scoped(job,started,clock,alive,ctx)){ok=false;break;}
        int connected=esp_tls_conn_new_async(HOST,sizeof(HOST)-1,443,&config,tls);
        if(connected==1)break;
        if(connected<0){ok=false;break;}
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for(size_t sent=0;ok&&sent<(size_t)request_length;){
        if(!scoped(job,started,clock,alive,ctx)){ok=false;break;}
        int n=(int)esp_tls_conn_write(tls,request+sent,(size_t)request_length-sent);
        if(n>0&&(size_t)n<=(size_t)request_length-sent)sent+=(size_t)n;
        else if(pending(n))vTaskDelay(pdMS_TO_TICKS(10));
        else ok=false;
    }
    size_t received=0;
    while(ok){
        if(received>=HEADER_MAX||!scoped(job,started,clock,alive,ctx)){ok=false;break;}
        int n=(int)esp_tls_conn_read(tls,header+received,1);
        if(n==1){
            unsigned char c=(unsigned char)header[received++];
            if(c!=13&&c!=10&&c!=9&&(c<32||c>126)){ok=false;break;}
            if(received>=4&&!memcmp(header+received-4,"\r\n\r\n",4)){header[received]=0;ok=headers(header,job);break;}
        }else if(pending(n))vTaskDelay(pdMS_TO_TICKS(10));
        else ok=false;
    }
    for(size_t body=0;ok&&body<job->length;){
        if(!scoped(job,started,clock,alive,ctx)){ok=false;break;}
        int n=(int)esp_tls_conn_read(tls,(unsigned char *)out+body,job->length-body);
        if(n>0&&(size_t)n<=job->length-body)body+=(size_t)n;
        else if(pending(n))vTaskDelay(pdMS_TO_TICKS(10));
        else ok=false;
    }
    ok=ok&&scoped(job,started,clock,alive,ctx);
    if(tls)esp_tls_conn_destroy(tls);
    free(header);free(request);return ok;
}
