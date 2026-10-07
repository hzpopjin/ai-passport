#include "h2h_relay.h"
#include "cJSON.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define BODY_MAX 2048u
static bool resource_id_valid(const char *id);
static bool hex(const char *s,size_t n){if(!s||strlen(s)!=n)return false;for(size_t i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;return true;}
static void tohex(const uint8_t *p,size_t n,char *out){const char *h="0123456789abcdef";for(size_t i=0;i<n;i++){out[i*2]=h[p[i]>>4];out[i*2+1]=h[p[i]&15];}out[n*2]=0;}
static const char *str(const cJSON *o,const char *k){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,k);return cJSON_IsString(v)?v->valuestring:NULL;}
static bool num(const cJSON *o,const char *k,uint64_t *n){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,k);if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>H2H_POCKET_MAX_INTEGER||floor(v->valuedouble)!=v->valuedouble)return false;*n=(uint64_t)v->valuedouble;return true;}
static bool truth(const cJSON *o,const char *k,bool value){return value?cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,k)):cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(o,k));}
static bool unique(const cJSON *o,unsigned depth){if(depth>12)return false;for(const cJSON *a=o->child;a;a=a->next){if(cJSON_IsObject(o))for(const cJSON *b=a->next;b;b=b->next)if(!a->string||!b->string||strcmp(a->string,b->string)==0)return false;if((cJSON_IsObject(a)||cJSON_IsArray(a))&&!unique(a,depth+1))return false;}return true;}
static cJSON *parse(const char *s,size_t n){if(!s||!n||memchr(s,0,n))return NULL;const char *end=NULL;cJSON *o=cJSON_ParseWithLengthOpts(s,n+1,&end,1);if(!cJSON_IsObject(o)||end!=s+n||!unique(o,0)){cJSON_Delete(o);return NULL;}return o;}
static bool encode(const void *data,size_t n,char *out,size_t cap){const uint8_t *p=data;const char *a="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t need=((n+2)/3)*4;if(need>=cap)return false;size_t j=0;for(size_t i=0;i<n;i+=3){uint32_t v=(uint32_t)p[i]<<16;if(i+1<n)v|=(uint32_t)p[i+1]<<8;if(i+2<n)v|=p[i+2];out[j++]=a[v>>18];out[j++]=a[(v>>12)&63];out[j++]=i+1<n?a[(v>>6)&63]:'=';out[j++]=i+2<n?a[v&63]:'=';}out[j]=0;return true;}
static int sextet(char c){if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;}
static size_t decode(const char *s,uint8_t *out,size_t cap){if(!s)return 0;size_t n=strlen(s);if(!n||n%4||n>4*((cap+2)/3))return 0;size_t j=0;for(size_t i=0;i<n;i+=4){int a=sextet(s[i]),b=sextet(s[i+1]),c=s[i+2]=='='?0:sextet(s[i+2]),d=s[i+3]=='='?0:sextet(s[i+3]);bool pc=s[i+2]=='=',pd=s[i+3]=='=';if(a<0||b<0||c<0||d<0||(pc&&!pd)||((pc||pd)&&i+4!=n)||(pc&&(b&15))||(pd&&!pc&&(c&3)))return 0;uint32_t v=(uint32_t)a<<18|(uint32_t)b<<12|(uint32_t)c<<6|(uint32_t)d;if(j>=cap)return 0;out[j++]=(uint8_t)(v>>16);if(!pc){if(j>=cap)return 0;out[j++]=(uint8_t)(v>>8);}if(!pd){if(j>=cap)return 0;out[j++]=(uint8_t)v;}}return j;}
static void fail(h2h_relay_t *r,const char *code){r->stage=H2H_RELAY_ERROR;r->resource_applied=false;snprintf(r->error,sizeof(r->error),"%s",code);r->request_json[0]=0;}
static bool live(h2h_relay_t *r,uint32_t now){if(!r->active)return false;if((uint32_t)(now-r->started)>=r->ttl_ms){fail(r,"session_expired");return false;}return true;}
static bool hash_save(h2h_relay_t *r,const h2h_save_t *save,char out[65]){uint8_t h[32];if(!r->ops.sha(save,sizeof(*save),h))return false;tohex(h,32,out);return true;}
static bool mac(h2h_relay_t *r,const char *direction,const cJSON *e,char out[65]){
    const char *device=str(e,"device_id"),*session=str(e,"session_id"),*request=str(e,"request_id"),*operation=str(e,"operation"),*body=str(e,"body_b64");uint64_t v;
    if(cJSON_GetArraySize(e)!=7||!num(e,"version",&v)||(v!=1&&v!=2)||!hex(device,32)||!hex(session,32)||!hex(request,32)||!operation||strlen(operation)>23||!body||strlen(body)>2732||!hex(str(e,"mac"),64))return false;
    char *input=malloc(3200);if(!input)return false;int n=snprintf(input,3200,"pocket-relay-v%u\n%s\n%s\n%s\n%s\n%s\n%s",(unsigned)v,direction,device,session,request,operation,body);uint8_t bytes[32];bool ok=n>0&&n<3200&&r->ops.hmac(r->key,input,(size_t)n,bytes);free(input);if(ok)tohex(bytes,32,out);return ok;
}
static cJSON *verify(h2h_relay_t *r,const cJSON *e,const char *direction){
    char expected[65];if(!r->enrolled||!mac(r,direction,e,expected))return NULL;unsigned diff=0;const char *given=str(e,"mac");for(unsigned i=0;i<64;i++)diff|=(unsigned char)expected[i]^(unsigned char)given[i];if(diff||strcmp(str(e,"device_id"),r->device)!=0)return NULL;
    uint8_t *body=malloc(BODY_MAX+1);if(!body)return NULL;size_t n=decode(str(e,"body_b64"),body,BODY_MAX);body[n]=0;cJSON *o=n?parse((char *)body,n):NULL;free(body);return o;
}
static bool request(h2h_relay_t *r,const char *operation,const char *body,h2h_relay_stage_t stage){
    if(strlen(body)>BODY_MAX)return false;
    uint8_t nonce[16];r->ops.random(nonce,sizeof(nonce));tohex(nonce,sizeof(nonce),r->request);char *b64=malloc(2733);if(!b64)return false;if(!encode(body,strlen(body),b64,2733)){free(b64);return false;}
    cJSON *e=cJSON_CreateObject();cJSON_AddNumberToObject(e,"version",r->version?r->version:1);cJSON_AddStringToObject(e,"device_id",r->device);cJSON_AddStringToObject(e,"session_id",r->session);cJSON_AddStringToObject(e,"request_id",r->request);cJSON_AddStringToObject(e,"operation",operation);cJSON_AddStringToObject(e,"body_b64",b64);cJSON_AddStringToObject(e,"mac","0000000000000000000000000000000000000000000000000000000000000000");free(b64);char h[65];bool ok=mac(r,"request",e,h);if(ok){cJSON_ReplaceItemInObjectCaseSensitive(e,"mac",cJSON_CreateString(h));char *s=cJSON_PrintUnformatted(e);ok=s&&strlen(s)<sizeof(r->request_json);if(ok)strcpy(r->request_json,s);cJSON_free(s);}cJSON_Delete(e);if(ok){strcpy(r->operation,operation);if(!strcmp(operation,"sync"))strcpy(r->sync_request,r->request);r->stage=stage;}return ok;
}
static bool profile_list(const cJSON *body,const char *key,const char *required){
    const cJSON *a=cJSON_GetObjectItemCaseSensitive(body,key);bool found=required==NULL;
    if(!cJSON_IsArray(a)||cJSON_GetArraySize(a)>16)return false;
    for(const cJSON *v=a->child;v;v=v->next){
        if(!cJSON_IsString(v)||!v->valuestring[0]||strlen(v->valuestring)>32)return false;
        for(const char *p=v->valuestring;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_'))return false;
        for(const cJSON *w=v->next;w;w=w->next)if(cJSON_IsString(w)&&!strcmp(v->valuestring,w->valuestring))return false;
        if(required&&!strcmp(v->valuestring,required))found=true;
    }
    return found;
}
static bool start_capabilities(h2h_relay_t *r){
    h2h_resource_capabilities_t *caps=&r->resource_caps;
    r->resource_offered=r->ops.resource_work&&caps->key_count>0&&caps->key_count<=8&&
        caps->capacity[0]>=H2H_RESOURCE_SLOT_BYTES&&caps->capacity[1]>=H2H_RESOURCE_SLOT_BYTES;
    if(r->resource_offered)for(unsigned i=0;i<caps->key_count;i++){
        if(!memchr(caps->key_ids[i],0,33)||!resource_id_valid(caps->key_ids[i]))r->resource_offered=false;
        for(unsigned j=0;j<i;j++)if(!memcmp(caps->key_ids[i],caps->key_ids[j],33))r->resource_offered=false;
    }
    cJSON *body=cJSON_CreateObject(),*save=cJSON_CreateArray(),*profiles=cJSON_CreateArray(),*slots=cJSON_CreateArray(),*keys=cJSON_CreateArray();
    cJSON_AddNumberToObject(body,"ownership_epoch",r->server_epoch);cJSON_AddStringToObject(body,"model","esp32c3-pocket-8m");
    cJSON_AddNumberToObject(body,"firmware",1);cJSON_AddItemToArray(save,cJSON_CreateString(H2H_FULL_PROFILE));
    cJSON_AddItemToObject(body,"save_profiles",save);
    if(r->resource_offered)cJSON_AddItemToArray(profiles,cJSON_CreateString(H2H_RESOURCE_PROFILE));
    cJSON_AddItemToObject(body,"resource_profiles",profiles);
    for(unsigned i=0;i<2;i++)cJSON_AddItemToArray(slots,cJSON_CreateNumber(caps->capacity[i]));
    cJSON_AddItemToObject(body,"slot_capacity_bytes",slots);cJSON_AddBoolToObject(body,"wifi_usable",r->resource_offered&&caps->wifi_usable);
    if(r->resource_offered)for(unsigned i=0;i<caps->key_count;i++)cJSON_AddItemToArray(keys,cJSON_CreateString(caps->key_ids[i]));
    cJSON_AddItemToObject(body,"resource_key_ids",keys);cJSON_AddNumberToObject(body,"max_chunk_bytes",r->max_chunk);
    char *text=cJSON_PrintUnformatted(body);bool ok=text&&request(r,"capabilities",text,H2H_RELAY_CAPABILITIES);
    cJSON_free(text);cJSON_Delete(body);return ok;
}
static bool hash_full(h2h_relay_t *r,const uint8_t bytes[164],char out[65]){uint8_t hash[32];if(!r->ops.sha(bytes,164,hash))return false;tohex(hash,32,out);return true;}
static bool start_full_sync(h2h_relay_t *r){
    h2h_fullsave_t *f=r->ops.full;
    if(!r->full_selected||!f||!f->healthy||!h2h_pocket_can_play(r->pocket)||r->pending_only)return false;
    r->ack_json[0]=0;r->ack_full_generation=0;memset(r->ack_full,0,164);r->sent=r->pocket->record;
    memcpy(r->sent_full,f->local,164);r->sent_full_revision=f->revision;
    char body[800],save[600]="",digest[65],data[221];
    if(f->has_local){
        if(!h2h_fullsave_valid(f->local,164,r->server_epoch)||!hash_full(r,f->local,digest))return false;
        if(f->restore_ack)snprintf(save,sizeof(save),",\"save_ack\":{\"revision\":%" PRIu64 ",\"sha256\":\"%s\"}",f->revision,digest);
        else if(f->dirty||!f->server_full){
            if(!encode(f->local,164,data,sizeof(data)))return false;
            snprintf(save,sizeof(save),",\"save\":{\"base_revision\":%" PRIu64 ",\"schema\":4,\"record_version\":2,\"data\":\"%s\",\"sha256\":\"%s\"}",f->revision,data,digest);
        }
    }
    snprintf(body,sizeof(body),"{\"ownership_epoch\":%" PRIu64 "%s}",r->server_epoch,save);
    return request(r,"sync",body,H2H_RELAY_SYNC);
}
static bool start_sync(h2h_relay_t *r){
    if(r->version==2)return start_full_sync(r);
    if(!r->pocket->healthy)return false;
    r->ack_json[0]=0;memset(&r->ack_record,0,sizeof(r->ack_record));
    r->sent=r->pocket->record;h2h_pocket_record_t *s=&r->sent;char save[360]="",unbind[160]="",body[1050];
    if(r->pending_only&&!s->ownership.pending_unbind[0])return false;
    if(s->ownership.pending_unbind[0])snprintf(unbind,sizeof(unbind),",\"pending_unbind\":{\"operation_id\":\"%s\",\"expected_epoch\":%" PRIu64 "}",s->ownership.pending_unbind,s->ownership.pending_epoch);
    else if(s->ownership.epoch==r->server_epoch&&r->owner_authorized&&s->restore_ack){char h[65];if(!hash_save(r,&s->remote_save,h))return false;snprintf(save,sizeof(save),",\"save_ack\":{\"revision\":%" PRIu64 ",\"sha256\":\"%s\"}",s->remote_revision,h);}
    else if(s->ownership.epoch==r->server_epoch&&r->owner_authorized&&s->dirty&&!s->conflict&&h2h_ownership_can_play(&s->ownership)){char h[65],b[65];if(!hash_save(r,&s->save,h)||!encode(&s->save,sizeof(s->save),b,sizeof(b)))return false;snprintf(save,sizeof(save),",\"save\":{\"base_revision\":%" PRIu64 ",\"schema\":1,\"sha256\":\"%s\",\"data\":\"%s\"}",s->revision,h,b);}
    snprintf(body,sizeof(body),r->pending_only?"{\"ownership_epoch\":%" PRIu64 "%s%s}":"{\"ownership_epoch\":%" PRIu64 "%s%s,\"capabilities\":{\"firmware\":1,\"resource_format\":2,\"resource_capacity\":0,\"resource_keys\":[]}}",s->ownership.epoch,unbind,save);
    return request(r,"sync",body,H2H_RELAY_SYNC);
}
static bool start_ack(h2h_relay_t *r){
    if(r->pending_only)return false;
    if(r->version==2){
        h2h_fullsave_t *f=r->ops.full;stayc_game_record_t record;char hash[65];
        if(!r->full_selected||!f||!f->healthy||!f->has_local||f->dirty||f->conflict||f->restore_ack||!f->server_full||
           !h2h_fullsave_valid(f->local,164,r->server_epoch)||!stayc_game_record_decode(&record,f->local,164)||!hash_full(r,f->local,hash))return false;
        memcpy(r->ack_full,f->local,164);r->ack_full_generation=f->generation;r->ack_record=r->pocket->record;
        snprintf(r->ack_json,sizeof(r->ack_json),"{\"device_id\":\"%s\",\"session_id\":\"%s\",\"account_id\":%" PRIu32 ",\"ownership_epoch\":%" PRIu64 ",\"save_revision\":%" PRIu64 ",\"save_sha256\":\"%s\",\"activated\":true,\"locked\":false,\"durable\":true,\"game_hearts\":%" PRIu32 ",\"game_stars\":%" PRIu32 ",\"local_game_schema\":4,\"extended_save_cloud_synced\":true,\"save_profile\":\"" H2H_FULL_PROFILE "\",\"save_schema\":4,\"record_version\":2,\"sync_request_id\":\"%s\"}",r->device,r->session,r->account,r->server_epoch,f->revision,hash,record.gameplay.hearts,record.gameplay.star_balance,r->sync_request);
        return request(r,"ack",r->ack_json,H2H_RELAY_ACK);
    }
    r->ack_record=r->pocket->record;h2h_pocket_record_t *s=&r->ack_record;char h[65],stars[24]="null";if(!hash_save(r,&s->save,h))return false;
    if(r->stars_valid&&r->stars_epoch==s->ownership.epoch&&r->stars_generation==r->pocket->model_generation)snprintf(stars,sizeof(stars),"%" PRIu32,r->stars);
    snprintf(r->ack_json,sizeof(r->ack_json),"{\"device_id\":\"%s\",\"session_id\":\"%s\",\"account_id\":%" PRIu32 ",\"ownership_epoch\":%" PRIu64 ",\"save_revision\":%" PRIu64 ",\"save_sha256\":\"%s\",\"activated\":true,\"locked\":false,\"durable\":true,\"game_hearts\":%" PRIu32 ",\"game_stars\":%s,\"local_game_schema\":4,\"extended_save_cloud_synced\":false}",r->device,r->session,r->account,s->ownership.epoch,s->revision,h,s->save.hearts,stars);
    return request(r,"ack",r->ack_json,H2H_RELAY_ACK);
}
static bool same_save(const h2h_pocket_record_t *a,const h2h_pocket_record_t *b){return a->ownership.epoch==b->ownership.epoch&&a->ownership.account_id==b->ownership.account_id&&a->revision==b->revision&&a->dirty==b->dirty&&!memcmp(&a->save,&b->save,sizeof(a->save));}
static bool resource_id_valid(const char *id){
    if(!id||!id[0]||strlen(id)>32)return false;
    for(const char *p=id;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))return false;
    return true;
}
static bool unhex(const char *text,unsigned char *out,size_t length){
    if(!hex(text,length*2))return false;
    for(size_t i=0;i<length;i++){char pair[3]={text[2*i],text[2*i+1],0};out[i]=(unsigned char)strtoul(pair,NULL,16);}return true;
}
static bool resource_selection(h2h_relay_t *r,const cJSON *body){
    const cJSON *selection=cJSON_GetObjectItemCaseSensitive(body,"resource_selection");
    if(!r->resource_selected){
        if(selection&&!cJSON_IsNull(selection)){fail(r,"resource_unsupported");return false;}return true;
    }
    uint64_t revision;const char *id=str(selection,"id"),*sha=str(selection,"sha256");
    if(!num(body,"resource_revision",&revision)||revision>UINT32_MAX||!selection)return false;
    if(cJSON_IsNull(selection)){
        if(r->resource_started)return false;
        r->resource_id[0]=r->resource_sha[0]=0;r->resource_revision=0;return true;
    }
    if(!revision||!cJSON_IsObject(selection)||cJSON_GetArraySize(selection)!=2||!resource_id_valid(id)||!hex(sha,64))return false;
    if(r->resource_started)return revision==r->resource_revision&&!strcmp(id,r->resource_id)&&!strcmp(sha,r->resource_sha);
    r->resource_revision=(uint32_t)revision;strcpy(r->resource_id,id);strcpy(r->resource_sha,sha);return true;
}
static bool resource_job(h2h_relay_t *r,h2h_resource_job_kind_t kind){
    if(!r->resource_started||r->resource_model_generation!=r->pocket->model_generation){fail(r,"resource_scope_changed");return false;}
    uint8_t nonce[16];r->ops.random(nonce,sizeof(nonce));tohex(nonce,sizeof(nonce),r->resource_job.work_id);
    r->resource_job.kind=kind;strcpy(r->resource_job.session,r->session);
    r->resource_job.epoch=r->server_epoch;r->resource_job.model_generation=r->resource_model_generation;
    r->resource_job.revision=r->resource_revision;r->resource_job.session_started_ms=r->started;r->resource_job.session_ttl_ms=r->ttl_ms;
    r->resource_job.offset=r->resource_progress.cursor;
    uint32_t n=H2H_RESOURCE_BYTES-r->resource_progress.cursor;
    if(n>r->max_chunk)n=r->max_chunk;
    if(n>4096-r->resource_progress.cursor%4096)n=4096-r->resource_progress.cursor%4096;
    if(kind==H2H_RESOURCE_JOB_WIFI)r->resource_job.length=n;
    r->resource_job_taken=false;r->request_json[0]=0;r->stage=H2H_RELAY_RESOURCE_WORK;return true;
}
static bool resource_next(h2h_relay_t *r){
    if(r->resource_progress.cursor>H2H_RESOURCE_BYTES)return false;
    if(r->resource_progress.cursor==H2H_RESOURCE_BYTES)return resource_job(r,H2H_RESOURCE_JOB_FINISH);
    if(r->resource_wifi)return resource_job(r,H2H_RESOURCE_JOB_WIFI);
    uint32_t n=H2H_RESOURCE_BYTES-r->resource_progress.cursor;
    if(n>r->max_chunk)n=r->max_chunk;
    if(n>4096-r->resource_progress.cursor%4096)n=4096-r->resource_progress.cursor%4096;
    char body[350],prefix[65];tohex(r->resource_progress.prefix_sha256,32,prefix);
    r->resource_job.offset=r->resource_progress.cursor;r->resource_job.length=n;
    snprintf(body,sizeof(body),"{\"ownership_epoch\":%" PRIu64 ",\"resource_revision\":%" PRIu32 ",\"sha256\":\"%s\",\"offset\":%" PRIu32 ",\"length\":%" PRIu32 ",\"prefix_sha256\":\"%s\"}",r->server_epoch,r->resource_revision,r->resource_sha,r->resource_progress.cursor,n,prefix);
    return request(r,"resource_chunk",body,H2H_RELAY_RESOURCE_CHUNK);
}
static bool resource_start(h2h_relay_t *r){
    if(!r->resource_selected||!r->resource_id[0]||r->resource_applied)return start_ack(r);
    char body[160];
    if(!r->resource_started){r->resource_started=true;r->resource_model_generation=r->pocket->model_generation;}
    snprintf(body,sizeof(body),"{\"ownership_epoch\":%" PRIu64 ",\"resource_revision\":%" PRIu32 ",\"resource_id\":\"%s\"}",r->server_epoch,r->resource_revision,r->resource_id);
    return request(r,"resource_manifest",body,H2H_RELAY_RESOURCE_MANIFEST);
}
static bool resource_manifest(h2h_relay_t *r,const cJSON *body,uint32_t now){
    uint64_t epoch,revision,value;const cJSON *m=cJSON_GetObjectItemCaseSensitive(body,"manifest"),*transport=cJSON_GetObjectItemCaseSensitive(body,"transport");
    h2h_resource_manifest_t manifest={0};const char *kind=str(m,"kind"),*model=str(m,"model"),*id=str(m,"id"),*sha=str(m,"sha256"),*key_id=str(m,"key_id");
    if(cJSON_GetArraySize(body)!=4||!num(body,"ownership_epoch",&epoch)||epoch!=r->server_epoch||
       !num(body,"resource_revision",&revision)||revision!=r->resource_revision||!cJSON_IsObject(m)||cJSON_GetArraySize(m)!=12||
       !kind||(strcmp(kind,"member_visual")&&strcmp(kind,"group_visual"))||
       !model||strcmp(model,"esp32c3-pocket-8m")||!resource_id_valid(id)||strcmp(id,r->resource_id)||
       !hex(sha,64)||strcmp(sha,r->resource_sha)||!resource_id_valid(key_id)||
       !num(m,"format",&value)||value!=2||!num(m,"size",&value)||value!=H2H_RESOURCE_BYTES||
       !num(m,"frame_bytes",&value)||value!=15360)return false;
    manifest.kind=!strcmp(kind,"member_visual")?1:2;
    if(!num(m,"version",&value)||!value||value>UINT32_MAX)return false;
    manifest.version=(uint32_t)value;
    if(!num(m,"min_firmware",&value)||!value||value>UINT32_MAX)return false;
    manifest.min_firmware=(uint32_t)value;
    if(!num(m,"max_firmware",&value)||!value||value>UINT32_MAX)return false;
    manifest.max_firmware=(uint32_t)value;
    bool known=false;for(unsigned i=0;i<r->resource_caps.key_count;i++)if(!strcmp(key_id,r->resource_caps.key_ids[i]))known=true;
    if(!known||decode(str(m,"signature"),manifest.signature,64)!=64||!unhex(sha,manifest.sha256,32))return false;
    strcpy(manifest.id,id);strcpy(manifest.key_id,key_id);
    const char *channel=str(transport,"kind");
    if(!cJSON_IsObject(transport)||!channel)return false;
    if(!strcmp(channel,"wifi")){
        const char *url=str(transport,"url");uint64_t ttl;
        if(!r->resource_wifi||cJSON_GetArraySize(transport)!=3||!num(transport,"ttl",&ttl)||!ttl||ttl>300||
           !h2h_resource_locator_valid(url,manifest.sha256))return false;
        strcpy(r->resource_job.locator,url);r->resource_job.locator_started_ms=now;r->resource_job.locator_ttl_ms=(uint32_t)ttl*1000;
    }else if(!strcmp(channel,"ble")&&cJSON_GetArraySize(transport)==1)r->resource_wifi=false;
    else return false;
    r->resource_job.manifest=manifest;r->resource_job.restart=false;return resource_job(r,H2H_RESOURCE_JOB_BEGIN);
}
static bool resource_chunk(h2h_relay_t *r,const cJSON *body){
    uint64_t epoch,revision,offset,total;const char *sha=str(body,"sha256");
    if(cJSON_GetArraySize(body)!=7||!num(body,"ownership_epoch",&epoch)||epoch!=r->server_epoch||
       !num(body,"resource_revision",&revision)||revision!=r->resource_revision||!sha||strcmp(sha,r->resource_sha)||
       !num(body,"offset",&offset)||offset!=r->resource_job.offset||!num(body,"total_size",&total)||total!=H2H_RESOURCE_BYTES||
       decode(str(body,"data"),r->resource_job.data,1024)!=r->resource_job.length||
       !unhex(str(body,"chunk_sha256"),r->resource_job.chunk_sha256,32))return false;
    return resource_job(r,H2H_RESOURCE_JOB_CHUNK);
}
static bool apply_full_sync(h2h_relay_t *r,const cJSON *body){
    uint64_t epoch,account,revision,schema,record_version;const char *status=str(body,"save_status");
    h2h_fullsave_t *f=r->ops.full;const cJSON *save=cJSON_GetObjectItemCaseSensitive(body,"save");
    uint8_t bytes[164];char digest[65];
    if(!r->full_selected||!f||!r->ops.full_io||cJSON_GetArraySize(body)!=(r->resource_selected?9:7)||
       !num(body,"ownership_epoch",&epoch)||epoch!=r->server_epoch||
       epoch!=r->pocket->record.ownership.epoch||!num(body,"account_id",&account)||account!=r->account||
       !truth(body,"activated",true)||!truth(body,"locked",false)||!status||
       (strcmp(status,"ok")&&strcmp(status,"already_applied")&&strcmp(status,"unchanged")&&strcmp(status,"restore_required")&&strcmp(status,"revision_conflict"))||
       (!truth(body,"restore_required",true)&&!truth(body,"restore_required",false))||
       truth(body,"restore_required",true)!=(!strcmp(status,"restore_required"))||
       !cJSON_IsObject(save)||cJSON_GetArraySize(save)!=5||!num(save,"revision",&revision)||!num(save,"schema",&schema)||schema!=4||
       !num(save,"record_version",&record_version)||record_version!=2||!hex(str(save,"sha256"),64)||
       decode(str(save,"data"),bytes,164)!=164||!hash_full(r,bytes,digest)||strcmp(digest,str(save,"sha256"))||
       !resource_selection(r,body))return false;
    if(!h2h_fullsave_reply(f,r->pocket,r->sent_full,r->sent_full_revision,revision,bytes,
            !strcmp(status,"revision_conflict"),truth(body,"restore_required",true),r->ops.full_io))return false;
    if(f->conflict){fail(r,"save_conflict_protected");return true;}
    if(f->dirty||f->restore_ack)return start_full_sync(r);
    return resource_start(r);
}
static bool apply_sync(h2h_relay_t *r,const cJSON *body){
    if(r->version==2)return apply_full_sync(r,body);
    h2h_pocket_reply_t reply={0};uint64_t account=0,schema;const char *status=str(body,"save_status");
    if(!num(body,"ownership_epoch",&reply.epoch))return false;
    if(r->sent.ownership.pending_unbind[0]) {
        const char *ack=str(body,"pending_unbind_ack"),*rejected=str(body,"pending_unbind_rejected");
        bool superseded=status&&!strcmp(status,"pending_unbind_superseded");
        bool settled=status&&(!strcmp(status,"ownership_changed")||superseded);
        if(!settled||cJSON_GetArraySize(body)!=8||reply.epoch<r->server_epoch||
           !truth(body,"activated",false)||!truth(body,"locked",true)||
           !cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(body,"account_id"))||
           !cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(body,"save"))||
           (superseded?(!rejected||ack||!cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(body,"pending_unbind_ack"))):
                       (!ack||rejected||!cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(body,"pending_unbind_rejected")))))return false;
        if(!h2h_pocket_settle_unbind(r->pocket,&r->sent,reply.epoch,superseded?rejected:ack,r->ops.write,r->ops.write_context))return false;
        r->gate=true;r->pair_after_sync=false;r->active=false;
        fail(r,superseded?"pending_unbind_superseded":"pending_unbind_settled");return true;
    }
    if(r->pending_only||reply.epoch!=r->server_epoch)return false;
    if(truth(body,"activated",false)&&truth(body,"locked",true)&&status&&!strcmp(status,"ownership_changed")&&cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(body,"account_id"))&&(!cJSON_GetObjectItemCaseSensitive(body,"save")||cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(body,"save")))) {
        reply.unbind_ack=str(body,"pending_unbind_ack");reply.unbind_rejected=str(body,"pending_unbind_rejected");
        if(r->server_active&&!r->owner_authorized){
            if(!h2h_pocket_lock_owner(r->pocket,r->ops.write,r->ops.write_context))return false;
            fail(r,"ownership_changed");return true;
        }
        if(!h2h_pocket_reply(r->pocket,&r->sent,&reply,r->ops.write,r->ops.write_context))return false;
        if(r->pocket->record.ownership.pending_unbind[0]){fail(r,"unbind_pending");return true;}
        if(r->pair_after_sync&&!r->server_active){r->pair_after_sync=false;r->pair_epoch=reply.epoch;char b[80];snprintf(b,sizeof(b),"{\"expected_epoch\":%" PRIu64 "}",r->pair_epoch);return request(r,"begin",b,H2H_RELAY_BEGIN);}
        fail(r,"ownership_changed");return true;
    }
    if(!num(body,"account_id",&account)||account!=r->account||!truth(body,"activated",true)||!truth(body,"locked",false)||!status)return false;
    if(r->confirmed_epoch&&reply.epoch!=r->confirmed_epoch)return false;
    if(strcmp(status,"ok")&&strcmp(status,"already_applied")&&strcmp(status,"unchanged")&&strcmp(status,"restore_required")&&strcmp(status,"revision_conflict"))return false;
    reply.account_id=(uint32_t)account;reply.activated=true;reply.conflict=strcmp(status,"revision_conflict")==0;reply.restore_required=truth(body,"restore_required",true);
    reply.unbind_ack=str(body,"pending_unbind_ack");reply.unbind_rejected=str(body,"pending_unbind_rejected");
    const cJSON *save=cJSON_GetObjectItemCaseSensitive(body,"save");if(save&&!cJSON_IsNull(save)){
        const char *b64=str(save,"data"),*h=str(save,"sha256");char digest[65];if(!num(save,"schema",&schema)||schema!=1||!num(save,"revision",&reply.revision)||!reply.revision||!hex(h,64)||decode(b64,(uint8_t *)&reply.save,sizeof(reply.save))!=sizeof(reply.save)||!hash_save(r,&reply.save,digest)||strcmp(h,digest))return false;reply.has_save=true;
    }
    if(reply.conflict&&!reply.has_save)return false;
    /* A restore signal must never silently replace newer unsent device progress. */
    if(reply.restore_required&&r->pocket->record.dirty){reply.conflict=true;reply.restore_required=false;}
    if(!h2h_pocket_reply(r->pocket,&r->sent,&reply,r->ops.write,r->ops.write_context))return false;
    if(r->pocket->record.conflict){fail(r,"save_conflict_protected");return true;}
    if(!h2h_pocket_can_play(r->pocket)||r->pocket->record.ownership.account_id!=r->account){fail(r,"ownership_locked");return true;}
    if(!r->pocket->record.revision&&!h2h_pocket_require_upload(r->pocket,r->ops.write,r->ops.write_context))return false;
    if(r->pocket->record.dirty||r->pocket->record.restore_ack)return start_sync(r);
    return start_ack(r);
}
static bool deliver(h2h_relay_t *r,const cJSON *e,uint32_t now){
    cJSON *body=verify(r,e,"response");if(!body)return false;
    if(!strcmp(str(e,"session_id"),r->session)&&!strcmp(str(e,"request_id"),r->last_request)&&!strcmp(str(e,"mac"),r->last_response_mac)){cJSON_Delete(body);return true;}
    uint64_t envelope_version=0;bool ok=num(e,"version",&envelope_version)&&envelope_version==r->version&&strcmp(str(e,"session_id"),r->session)==0&&strcmp(str(e,"request_id"),r->request)==0&&strcmp(str(e,"operation"),r->operation)==0;
    if(!ok){cJSON_Delete(body);return false;}
    const char *error=str(body,"error");if(error){fail(r,r->stage==H2H_RELAY_RESOURCE_CHUNK&&!strcmp(error,"prefix_mismatch")?"resource_restart_required":"backend_rejected");cJSON_Delete(body);return true;}
    char delivered_id[33],delivered_mac[65];strcpy(delivered_id,str(e,"request_id"));strcpy(delivered_mac,str(e,"mac"));
    uint64_t epoch=0,account=0;char next[200];
    switch(r->stage){
    case H2H_RELAY_CAPABILITIES:{
        uint64_t chunk;const char *profile=str(body,"save_profile");
        const char *resource=str(body,"resource_profile"),*transport=str(body,"transport");
        bool none=cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(body,"resource_profile"))&&cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(body,"transport"));
        bool selected=resource&&!strcmp(resource,H2H_RESOURCE_PROFILE)&&transport&&
            (!strcmp(transport,"ble")||(!strcmp(transport,"wifi")&&r->resource_caps.wifi_usable));
        if(!none&&(!selected||!r->resource_offered||!r->resource_ticket_offer)){fail(r,"resource_unsupported");break;}
        ok=cJSON_GetArraySize(body)==5&&num(body,"ownership_epoch",&epoch)&&epoch==r->server_epoch&&
            profile&&!strcmp(profile,H2H_FULL_PROFILE)&&(none||selected)&&num(body,"max_chunk_bytes",&chunk)&&chunk>0&&chunk<=r->max_chunk;
        r->resource_selected=selected;r->resource_wifi=selected&&!strcmp(transport,"wifi");
        if(ok){r->max_chunk=(unsigned)chunk;r->full_selected=true;ok=start_full_sync(r);}break;
    }
    case H2H_RELAY_BEGIN:{const char *code=str(body,"code");ok=hex(code,32)&&num(body,"ownership_epoch",&epoch)&&epoch==r->pair_epoch;if(ok){strcpy(r->pair_code,code);snprintf(next,sizeof(next),"{\"code\":\"%s\",\"expected_epoch\":%" PRIu64 ",\"account_id\":%" PRIu32 "}",code,epoch,r->account);ok=request(r,"claim",next,H2H_RELAY_CLAIM);}break;}
    case H2H_RELAY_CLAIM:ok=num(body,"account_id",&account)&&account==r->account&&str(body,"status")&&!strcmp(str(body,"status"),"awaiting_device_confirmation");if(ok){r->stage=H2H_RELAY_CARD_CONFIRM;r->request_json[0]=0;}break;
    case H2H_RELAY_CONFIRM:ok=truth(body,"activated",true)&&num(body,"account_id",&account)&&account==r->account&&num(body,"ownership_epoch",&epoch)&&epoch>r->pair_epoch;if(ok){r->confirmed_epoch=epoch;r->server_epoch=epoch;r->server_active=true;r->owner_authorized=true;ok=start_sync(r);}break;
    case H2H_RELAY_SYNC:ok=apply_sync(r,body);break;
    case H2H_RELAY_RESOURCE_MANIFEST:ok=resource_manifest(r,body,now);break;
    case H2H_RELAY_RESOURCE_CHUNK:ok=resource_chunk(r,body);break;
    case H2H_RELAY_RESOURCE_ACK:{
        cJSON *expected=parse(r->resource_ack_json,strlen(r->resource_ack_json));const char *status=str(body,"status");
        ok=status&&!strcmp(status,"applied");cJSON_DeleteItemFromObjectCaseSensitive(body,"status");
        ok=ok&&expected&&cJSON_Compare(expected,body,1);cJSON_Delete(expected);
        if(ok){r->resource_ack_confirmed=true;ok=resource_job(r,H2H_RESOURCE_JOB_RECEIPT);}break;
    }
    case H2H_RELAY_ACK:{
        cJSON *expected=parse(r->ack_json,strlen(r->ack_json));ok=truth(body,"completed",true);cJSON_DeleteItemFromObjectCaseSensitive(body,"completed");ok=ok&&expected&&cJSON_Compare(expected,body,1);cJSON_Delete(expected);
        if(ok){if(!h2h_pocket_can_play(r->pocket)||r->pocket->record.ownership.account_id!=r->account)ok=false;
            else if(r->version==2&&(!r->ops.full||!r->ops.full->healthy||r->ops.full->generation!=r->ack_full_generation||memcmp(r->ops.full->local,r->ack_full,164)||r->ops.full->dirty||r->ops.full->conflict||r->ops.full->restore_ack))ok=start_sync(r);
            else if(r->version==1&&(!same_save(&r->ack_record,&r->pocket->record)||r->pocket->record.dirty||r->pocket->record.restore_ack))ok=start_sync(r);
            else if(r->version==2&&r->resource_applied){r->resource_complete_pending=true;ok=resource_job(r,H2H_RESOURCE_JOB_RECEIPT);}
            else{r->stage=H2H_RELAY_COMPLETE;r->gate=false;r->request_json[0]=0;}}break;
    }
    default:ok=false;
    }
    if(ok){strcpy(r->last_request,delivered_id);strcpy(r->last_response_mac,delivered_mac);}
    cJSON_Delete(body);return ok;
}
static size_t result(h2h_relay_t *r,char *out){
    cJSON *o=cJSON_CreateObject();
    if(r->stage==H2H_RELAY_ERROR){cJSON_AddStringToObject(o,"state","error");cJSON_AddStringToObject(o,"code",r->error);}
    else if(r->stage==H2H_RELAY_COMPLETE){
        cJSON_Delete(o);o=parse(r->ack_json,strlen(r->ack_json));if(!o)return 0;cJSON_DeleteItemFromObjectCaseSensitive(o,"durable");cJSON_DeleteItemFromObjectCaseSensitive(o,"activated");cJSON_DeleteItemFromObjectCaseSensitive(o,"locked");cJSON_AddStringToObject(o,"state","complete");
        if(r->version==1)cJSON_AddNumberToObject(o,"save_schema",1);
        char b[221];const void *save=r->version==2?(const void *)r->ack_full:(const void *)&r->ack_record.save;size_t length=r->version==2?164:sizeof(r->ack_record.save);
        if(!encode(save,length,b,sizeof(b))){cJSON_Delete(o);return 0;}cJSON_AddStringToObject(o,"save_data",b);
    }else if(r->request_json[0]){cJSON_AddStringToObject(o,"state","request");cJSON *e=parse(r->request_json,strlen(r->request_json));if(!e){cJSON_Delete(o);return 0;}cJSON_AddItemToObject(o,"envelope",e);}
    else{cJSON_AddStringToObject(o,"state","waiting");cJSON_AddStringToObject(o,"stage",r->stage==H2H_RELAY_CARD_CONFIRM?"card_confirmation":"working");}
    if(r->version==2&&r->resource_selected&&r->stage==H2H_RELAY_COMPLETE){
        cJSON_AddStringToObject(o,"resource_status",r->resource_applied?"applied":r->resource_selected&&r->resource_id[0]?"pending":r->resource_offered?"not_selected":"unsupported");
        if(r->stage==H2H_RELAY_COMPLETE&&r->resource_applied){cJSON *proof=parse(r->resource_ack_json,strlen(r->resource_ack_json));if(!proof){cJSON_Delete(o);return 0;}cJSON_AddStringToObject(proof,"status","applied");cJSON_AddItemToObject(o,"resource_ack",proof);}
    }
    char *s=cJSON_PrintUnformatted(o);cJSON_Delete(o);size_t n=s?strlen(s):0;if(n>H2H_RELAY_RPC_MAX)n=0;if(n)memcpy(out,s,n+1);cJSON_free(s);return n;
}
void h2h_relay_init(h2h_relay_t *r,h2h_pocket_t *p,const char device[33],const uint8_t *key,const h2h_relay_ops_t *ops){memset(r,0,sizeof(*r));r->pocket=p;r->ops=*ops;r->version=1;strcpy(r->device,device);if(key){memcpy(r->key,key,32);r->enrolled=true;}}
void h2h_relay_disconnect(h2h_relay_t *r){
    r->active=false;fail(r,"connection_retired");
    r->ack_json[0]=0;memset(&r->ack_record,0,sizeof(r->ack_record));
    r->last_request[0]=0;r->last_response_mac[0]=0;
    r->resource_job.kind=H2H_RESOURCE_JOB_NONE;r->resource_job_taken=false;r->resource_applied=false;
    /* Retain the retired ticket identity so it cannot reopen this connection.
     * A persisted but unacknowledged binding stays gated until a fresh sync. */
}
bool h2h_relay_resource_take(h2h_relay_t *r,h2h_resource_job_t *out){
    if(!out||!r->active||r->stage!=H2H_RELAY_RESOURCE_WORK||r->resource_job_taken||
       r->resource_job.kind==H2H_RESOURCE_JOB_NONE||!r->ops.resource_work)return false;
    *out=r->resource_job;r->resource_job_taken=true;return true;
}
bool h2h_relay_resource_done(h2h_relay_t *r,const h2h_resource_job_t *job,
                            h2h_resource_work_status_t status,const h2h_resource_progress_t *progress,uint32_t now){
    if(!job||!progress||r->stage!=H2H_RELAY_RESOURCE_WORK||!r->resource_job_taken||job->kind!=r->resource_job.kind||
       job->offset!=r->resource_job.offset||job->length!=r->resource_job.length||job->revision!=r->resource_revision||
       strcmp(job->session,r->session)||strcmp(job->work_id,r->resource_job.work_id))return false;
    r->resource_job_taken=false;
    if(!live(r,now)||!h2h_pocket_can_play(r->pocket)||job->epoch!=r->pocket->record.ownership.epoch||
       job->model_generation!=r->resource_model_generation||job->model_generation!=r->pocket->model_generation||r->account!=r->pocket->record.ownership.account_id){fail(r,"resource_scope_changed");return false;}
    if(status==H2H_RESOURCE_WORK_TRANSPORT_FAILED&&job->kind==H2H_RESOURCE_JOB_WIFI){r->resource_wifi=false;return resource_next(r);}
    if(status==H2H_RESOURCE_WORK_WAIT&&job->kind==H2H_RESOURCE_JOB_RECEIPT)return resource_job(r,H2H_RESOURCE_JOB_RECEIPT);
    if(status!=H2H_RESOURCE_WORK_OK){
        fail(r,status==H2H_RESOURCE_WORK_UNSUPPORTED?"resource_unsupported":status==H2H_RESOURCE_WORK_RESTART_REQUIRED?"resource_restart_required":"resource_failed");return false;
    }
    char message[512];unsigned char manifest_sha[32];size_t n=h2h_resource_manifest_message(&job->manifest,message);
    bool ok=n&&r->ops.sha(message,n,manifest_sha)&&!memcmp(manifest_sha,progress->manifest_sha256,32)&&progress->cursor<=H2H_RESOURCE_BYTES;
    if(job->kind==H2H_RESOURCE_JOB_BEGIN)ok=ok&&(progress->cursor%4096==0||progress->cursor==H2H_RESOURCE_BYTES);
    else if(job->kind==H2H_RESOURCE_JOB_CHUNK||job->kind==H2H_RESOURCE_JOB_WIFI)ok=ok&&progress->cursor==job->offset+job->length;
    else ok=ok&&progress->cursor==H2H_RESOURCE_BYTES;
    if(!ok){fail(r,"resource_progress_rejected");return false;}
    if(job->kind==H2H_RESOURCE_JOB_RECEIPT){
        if(!progress->durable_visible||!progress->active_generation||
           (r->resource_ack_confirmed&&progress->active_generation!=r->resource_progress.active_generation)){fail(r,"resource_changed");return false;}
        r->resource_progress=*progress;r->resource_job.kind=H2H_RESOURCE_JOB_NONE;
        if(!r->resource_ack_confirmed){
            char hash[65];tohex(progress->manifest_sha256,32,hash);
            snprintf(r->resource_ack_json,sizeof(r->resource_ack_json),"{\"ownership_epoch\":%" PRIu64 ",\"resource_revision\":%" PRIu32 ",\"sha256\":\"%s\",\"manifest_sha256\":\"%s\",\"active_generation\":%" PRIu32 "}",r->server_epoch,r->resource_revision,r->resource_sha,hash,progress->active_generation);
            return request(r,"resource_ack",r->resource_ack_json,H2H_RELAY_RESOURCE_ACK);
        }
        r->resource_applied=true;
        h2h_fullsave_t *f=r->ops.full;
        if(r->resource_complete_pending){
            r->resource_complete_pending=false;
            if(f->generation!=r->ack_full_generation||memcmp(f->local,r->ack_full,164)||f->dirty||f->conflict||f->restore_ack)return start_full_sync(r);
            r->stage=H2H_RELAY_COMPLETE;r->gate=false;r->request_json[0]=0;return true;
        }
        return f->dirty||f->restore_ack?start_full_sync(r):start_ack(r);
    }
    r->resource_progress=*progress;
    if(job->kind==H2H_RESOURCE_JOB_FINISH||progress->durable_visible||(job->kind==H2H_RESOURCE_JOB_BEGIN&&progress->active_generation))return resource_job(r,H2H_RESOURCE_JOB_RECEIPT);
    return resource_next(r);
}
void h2h_relay_stars(h2h_relay_t *r,uint64_t epoch,uint64_t generation,uint32_t stars){r->stars=stars;r->stars_epoch=epoch;r->stars_generation=generation;r->stars_valid=true;}
bool h2h_relay_confirm(h2h_relay_t *r,uint32_t now){if(!live(r,now)||r->stage!=H2H_RELAY_CARD_CONFIRM||r->pocket->record.ownership.epoch!=r->pair_epoch||r->pocket->record.ownership.pending_unbind[0])return false;char body[200];snprintf(body,sizeof(body),"{\"code\":\"%s\",\"expected_epoch\":%" PRIu64 ",\"account_id\":%" PRIu32 "}",r->pair_code,r->pair_epoch,r->account);return request(r,"confirm",body,H2H_RELAY_CONFIRM);}
size_t h2h_relay_rpc(h2h_relay_t *r,const char *input,size_t length,char out[H2H_RELAY_RPC_MAX+1],uint32_t now){
    cJSON *rpc=length<=H2H_RELAY_RPC_MAX?parse(input,length):NULL;const char *action=rpc?str(rpc,"action"):NULL;bool resource_query=false;
    if(!r->enrolled)fail(r,"relay_enrollment_required");
    else if(!action)fail(r,"invalid_rpc");
    else if(!strcmp(action,"open")){
        const cJSON *e=cJSON_GetObjectItemCaseSensitive(rpc,"session");cJSON *body=verify(r,e,"session");uint64_t account=0,ttl=0,server_epoch=0,version=0,chunk=0;const char *mode=body?str(body,"mode"):NULL;
        bool ok=body&&num(e,"version",&version)&&cJSON_GetArraySize(rpc)==2&&cJSON_GetArraySize(body)==(version==2?9:6)&&!strcmp(str(e,"operation"),"session")&&num(body,"account_id",&account)&&account>0&&account<=UINT32_MAX&&num(body,"ttl",&ttl)&&ttl==300&&num(body,"ownership_epoch",&server_epoch)&&(truth(body,"device_activated",true)||truth(body,"device_activated",false))&&(truth(body,"owner_authorized",true)||truth(body,"owner_authorized",false))&&mode&&(!strcmp(mode,"pair")||!strcmp(mode,"sync"));
        if(ok&&version==2)ok=!strcmp(mode,"sync")&&truth(body,"device_activated",true)&&truth(body,"owner_authorized",true)&&
            profile_list(body,"save_profiles",H2H_FULL_PROFILE)&&profile_list(body,"resource_profiles",NULL)&&
            num(body,"max_chunk_bytes",&chunk)&&chunk>0&&chunk<=1024&&r->ops.full&&r->ops.full_io&&r->ops.full->healthy;
        if(ok&&!strcmp(str(e,"session_id"),r->session)){
            ok=r->active&&account==r->account&&!strcmp(str(e,"mac"),r->session_mac)&&r->stage!=H2H_RELAY_IDLE&&r->stage!=H2H_RELAY_ERROR;
            if(ok){r->active=true;ok=live(r,now);} /* Exact receipt resumes original stage; original TTL never extends. */
        }
        else if(ok){
            ok=r->pocket->healthy&&(!r->active||r->stage==H2H_RELAY_COMPLETE||r->stage==H2H_RELAY_ERROR);
            bool pairing=!strcmp(mode,"pair");bool server_active=truth(body,"device_activated",true),authorized=truth(body,"owner_authorized",true);
            bool pending=r->pocket->record.ownership.pending_unbind[0]!=0;
            bool pending_only=version==1&&pending;
            ok=ok&&server_epoch>=r->pocket->record.ownership.epoch&&(pending_only||(pairing?(!server_active&&!authorized):((server_active&&authorized)||(!authorized&&server_epoch>r->pocket->record.ownership.epoch))));
            if(ok&&version==2)ok=!pending&&h2h_pocket_can_play(r->pocket)&&server_epoch==r->pocket->record.ownership.epoch&&r->pocket->record.ownership.account_id==account;
            if(ok&&version==1&&!pending_only&&r->ops.full&&r->ops.full->server_full&&server_epoch==r->pocket->record.ownership.epoch){fail(r,"upgrade_required");ok=false;}
            if(ok&&!pending_only&&server_epoch==r->pocket->record.ownership.epoch&&r->pocket->record.ownership.activated)ok=server_active&&authorized&&r->pocket->record.ownership.account_id==account;
            if(ok){r->version=(unsigned)version;r->full_selected=false;r->max_chunk=(unsigned)chunk;r->active=true;r->started=now;r->ttl_ms=300000;r->account=(uint32_t)account;strcpy(r->session,str(e,"session_id"));strcpy(r->session_mac,str(e,"mac"));r->pair_epoch=r->pocket->record.ownership.epoch;r->confirmed_epoch=0;r->server_epoch=server_epoch;r->server_active=server_active;r->owner_authorized=authorized;r->pending_only=pending_only;r->pair_after_sync=pairing&&!pending_only&&server_epoch>r->pair_epoch;r->error[0]=0;r->request_json[0]=0;r->last_request[0]=0;r->last_response_mac[0]=0;r->gate=pairing||pending_only||r->gate;
                r->resource_caps=r->ops.resources;r->resource_selected=r->resource_wifi=r->resource_started=r->resource_applied=r->resource_job_taken=r->resource_ack_confirmed=r->resource_complete_pending=false;
                r->resource_ticket_offer=version==2&&profile_list(body,"resource_profiles",H2H_RESOURCE_PROFILE);
                r->resource_id[0]=r->resource_sha[0]=r->resource_ack_json[0]=0;r->resource_revision=0;r->resource_model_generation=0;
                memset(&r->resource_job,0,sizeof(r->resource_job));memset(&r->resource_progress,0,sizeof(r->resource_progress));
                if(version==2)ok=start_capabilities(r);
                else if(pairing&&!pending_only&&!r->pair_after_sync){char b[80];snprintf(b,sizeof(b),"{\"expected_epoch\":%" PRIu64 "}",r->pair_epoch);ok=request(r,"begin",b,H2H_RELAY_BEGIN);}else ok=start_sync(r);
            }
        }
        cJSON_Delete(body);if(!ok&&r->stage!=H2H_RELAY_ERROR)fail(r,"session_rejected");
    }else if(!live(r,now)){if(r->stage!=H2H_RELAY_ERROR)fail(r,"session_required");}
    else if(r->version==2&&(!h2h_pocket_can_play(r->pocket)||r->pocket->record.ownership.epoch!=r->server_epoch||r->pocket->record.ownership.account_id!=r->account||
            (r->resource_started&&r->resource_model_generation!=r->pocket->model_generation)))fail(r,"resource_scope_changed");
    else if(!strcmp(action,"resource_status")){
        if(r->version!=2||cJSON_GetArraySize(rpc)!=1)fail(r,"invalid_rpc");else resource_query=true;
    }
    else if(!strcmp(action,"resource_restart")){
        if(cJSON_GetArraySize(rpc)!=1||r->stage!=H2H_RELAY_ERROR||strcmp(r->error,"resource_restart_required")||!r->resource_selected)fail(r,"resource_restart_rejected");
        else {r->resource_job.restart=true;r->resource_applied=r->resource_ack_confirmed=r->resource_complete_pending=false;memset(&r->resource_progress,0,sizeof(r->resource_progress));(void)resource_job(r,H2H_RESOURCE_JOB_BEGIN);}
    }
    else if(!strcmp(action,"poll")){if(cJSON_GetArraySize(rpc)!=1)fail(r,"invalid_rpc");}
    else if(!strcmp(action,"deliver")){if(cJSON_GetArraySize(rpc)!=2)fail(r,"invalid_rpc");else if(!deliver(r,cJSON_GetObjectItemCaseSensitive(rpc,"envelope"),now)&&r->stage!=H2H_RELAY_ERROR)fail(r,"response_rejected");}
    else fail(r,"invalid_rpc");
    cJSON_Delete(rpc);
    if(resource_query){
        const char *status=r->resource_applied?"applied":r->resource_selected&&r->resource_id[0]?"pending":r->resource_offered?"not_selected":"unsupported";
        int n=snprintf(out,H2H_RELAY_RPC_MAX+1,"{\"state\":\"resource\",\"status\":\"%s\"}",status);return n>0?(size_t)n:0;
    }
    return result(r,out);
}
