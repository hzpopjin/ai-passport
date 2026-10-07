/* Real relay + resource/runtime + ESP-TLS adapter; only SDK/flash/network are
 * simulated. The published0..31 fixture seed is never a production provision. */
#define _POSIX_C_SOURCE 200809L
#define main previous_relay_test_main
#include "test_h2h_relay.c"
#undef main
#include "h2h_resource_runtime.h"
#include "h2h_resource_tls.h"
#include "pocket_sdk.h"
#include <sys/wait.h>
#include <unistd.h>

static unsigned char payload[H2H_RESOURCE_BYTES],flash[2][H2H_RESOURCE_SLOT_BYTES],public_fixture[32];
static unsigned char journal[1536],journal_stage[1536],resume[160],resume_stage[160];
static bool journal_exists,resume_exists,journal_pending,resume_pending,fail_nvs,fail_map,keys_enabled=true;
static bool scope_enabled=true,connected_fixture=true,model_mutex;
static uint32_t clock_ms=10,connection_generation=7;
static unsigned slot_writes,slot_erases,maps[2],tls_calls,tls_mode,tls_fail_offset=UINT32_MAX;
static EVP_PKEY *signer;
static const esp_partition_t partitions[]={{.address=0x10000,.size=0x770000},
    {.address=0x780000,.size=0x40000},{.address=0x7c0000,.size=0x40000}};
SemaphoreHandle_t xSemaphoreCreateMutex(void){return calloc(1,sizeof(unsigned));}
int xSemaphoreTake(SemaphoreHandle_t h,uint32_t timeout){(void)timeout;assert(h&&*(unsigned *)h==0);*(unsigned *)h=1;return pdTRUE;}
int xSemaphoreGive(SemaphoreHandle_t h){assert(h&&*(unsigned *)h==1);*(unsigned *)h=0;return pdTRUE;}
TickType_t xTaskGetTickCount(void){return clock_ms;}
void vTaskDelay(uint32_t n){clock_ms+=n;}
bool h2h_ble_connected(void){return connected_fixture;}
uint32_t h2h_ble_relay_connection_generation(void){return connection_generation;}
bool h2h_cloud_can_play_at(uint64_t epoch,uint64_t generation){assert(!model_mutex);return scope_enabled&&h2h_pocket_can_play(&p)&&p.record.ownership.epoch==epoch&&p.model_generation==generation;}
bool h2h_cloud_guarded_write(uint64_t epoch,uint64_t generation,bool (*write)(const void *,size_t,void *),const void *bytes,size_t n,void *ctx){
    if(!h2h_cloud_can_play_at(epoch,generation))return false;
    model_mutex=true;bool ok=write(bytes,n,ctx);model_mutex=false;return ok;
}
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h){(void)mode;assert(!strcmp(ns,"h2h_resources"));*h=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *name,void *out,size_t *n){
    assert(h==1);bool full_state=!strcmp(name,"visual_v2");assert(full_state||!strcmp(name,"resume_v2"));
    if(!(full_state?journal_exists:resume_exists))return ESP_ERR_NVS_NOT_FOUND;
    size_t wanted=full_state?1536:160;assert(*n>=wanted);memcpy(out,full_state?journal:resume,wanted);*n=wanted;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *name,const void *bytes,size_t n){
    assert(h==1&&model_mutex);if(!strcmp(name,"visual_v2")){assert(n==1536);memcpy(journal_stage,bytes,n);journal_pending=true;}
    else{assert(!strcmp(name,"resume_v2")&&n==160);memcpy(resume_stage,bytes,n);resume_pending=true;}return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h){
    assert(h==1&&model_mutex);if(fail_nvs){journal_pending=resume_pending=false;return ESP_FAIL;}
    if(journal_pending){memcpy(journal,journal_stage,1536);journal_exists=true;journal_pending=false;}
    if(resume_pending){memcpy(resume,resume_stage,160);resume_exists=true;resume_pending=false;}return ESP_OK;
}
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *name){(void)type;(void)subtype;return !strcmp(name,"factory")?partitions:!strcmp(name,"pocket_a")?partitions+1:!strcmp(name,"pocket_b")?partitions+2:NULL;}
static unsigned partition_index(const esp_partition_t *part){assert(part==partitions+1||part==partitions+2);return part==partitions+1?0:1;}
esp_err_t esp_partition_read(const esp_partition_t *part,size_t off,void *out,size_t n){unsigned i=partition_index(part);assert(off+n<=sizeof(flash[i]));memcpy(out,flash[i]+off,n);return ESP_OK;}
esp_err_t esp_partition_write(const esp_partition_t *part,size_t off,const void *bytes,size_t n){unsigned i=partition_index(part);assert(!model_mutex&&!maps[i]&&off+n<=sizeof(flash[i]));slot_writes++;memcpy(flash[i]+off,bytes,n);return ESP_OK;}
esp_err_t esp_partition_erase_range(const esp_partition_t *part,size_t off,size_t n){unsigned i=partition_index(part);assert(!model_mutex&&!maps[i]&&off%4096==0&&n%4096==0&&off+n<=sizeof(flash[i]));slot_erases++;memset(flash[i]+off,255,n);return ESP_OK;}
esp_err_t esp_partition_mmap(const esp_partition_t *part,size_t off,size_t n,int kind,const void **out,esp_partition_mmap_handle_t *handle){(void)kind;if(fail_map)return ESP_FAIL;unsigned i=partition_index(part);assert(off+n<=sizeof(flash[i]));*out=flash[i]+off;*handle=i+1;maps[i]++;return ESP_OK;}
void esp_partition_munmap(esp_partition_mmap_handle_t handle){assert(handle>=1&&handle<=2&&maps[handle-1]);maps[handle-1]--;}
bool h2h_resource_key_at(unsigned i,char id[33],unsigned char out[32]){if(i||!keys_enabled)return false;strcpy(id,"fixture");memcpy(out,public_fixture,32);return true;}
int mbedtls_sha256(const unsigned char *data,size_t n,unsigned char out[32],int sha224){assert(!sha224);return SHA256(data,n,out)?0:-1;}
void mbedtls_sha256_init(mbedtls_sha256_context *c){c->ctx=EVP_MD_CTX_new();assert(c->ctx);}
void mbedtls_sha256_free(mbedtls_sha256_context *c){EVP_MD_CTX_free(c->ctx);}
int mbedtls_sha256_starts(mbedtls_sha256_context *c,int sha224){assert(!sha224);return EVP_DigestInit_ex(c->ctx,EVP_sha256(),NULL)==1?0:-1;}
int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *s,size_t n){return EVP_DigestUpdate(c->ctx,s,n)==1?0:-1;}
int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char out[32]){unsigned n;return EVP_DigestFinal_ex(c->ctx,out,&n)==1&&n==32?0:-1;}
esp_err_t esp_crt_bundle_attach(void *cfg){(void)cfg;return ESP_OK;}
struct esp_tls {char request[1024];size_t sent,cursor,total;unsigned char response[4096];unsigned attempts;};
esp_tls_t *esp_tls_init(void){tls_calls++;return calloc(1,sizeof(esp_tls_t));}
int esp_tls_conn_new_async(const char *host,int n,int port,const esp_tls_cfg_t *cfg,esp_tls_t *tls){
    assert(n==(int)strlen("apps.randomdance.cn")&&!strcmp(host,"apps.randomdance.cn")&&port==443&&cfg->non_block&&cfg->timeout_ms==8000&&!cfg->skip_common_name&&cfg->crt_bundle_attach==esp_crt_bundle_attach);
    if(tls_mode==1)return -1;
    if(tls_mode==10)return 0;
    return tls->attempts++<2?0:1;
}
ssize_t esp_tls_conn_write(esp_tls_t *tls,const void *bytes,size_t n){
    if(n>37)n=37;
    assert(tls->sent+n<sizeof(tls->request));memcpy(tls->request+tls->sent,bytes,n);tls->sent+=n;tls->request[tls->sent]=0;
    if(strstr(tls->request,"\r\n\r\n")){
        unsigned start,end;char *range=strstr(tls->request,"Range: bytes=");assert(range&&sscanf(range,"Range: bytes=%u-%u",&start,&end)==2&&end>=start&&end-start<1024);
        assert(!strstr(tls->request,"Authorization")&&!strstr(tls->request,"Cookie")&&!strstr(tls->request,"X-Passport"));
        if(start>=tls_fail_offset)return -1;
        unsigned size=end-start+1,status=tls_mode==2?302:tls_mode==3?200:206;
        int header=snprintf((char *)tls->response,sizeof(tls->response),"HTTP/1.1 %u Test\r\nContent-Length: %u\r\nContent-Range: bytes %u-%u/%u\r\n%s\r\n",status,tls_mode==7?size+1:size,tls_mode==4?start+1:start,end,H2H_RESOURCE_BYTES,
            tls_mode==5?"Content-Encoding: gzip\r\n":tls_mode==6?"Transfer-Encoding: chunked\r\n":tls_mode==8?"Content-Range: duplicate\r\n":"");
        assert(header>0);memcpy(tls->response+header,payload+start,size);tls->total=(size_t)header+(tls_mode==9?size-1:size);
    }
    return (ssize_t)n;
}
ssize_t esp_tls_conn_read(esp_tls_t *tls,void *out,size_t n){if(tls->cursor==tls->total)return 0;size_t remain=tls->total-tls->cursor;if(n>remain)n=remain;memcpy(out,tls->response+tls->cursor,n);tls->cursor+=n;return (ssize_t)n;}
int esp_tls_conn_destroy(esp_tls_t *tls){free(tls);return 0;}
static uint32_t test_clock(void *ctx){(void)ctx;return clock_ms;}
static bool test_scope(const h2h_resource_job_t *job,void *ctx){(void)ctx;return connected_fixture&&job->connection_generation==connection_generation&&h2h_cloud_can_play_at(job->epoch,job->model_generation);}
static bool test_tls(const h2h_resource_job_t *job,void *out,void *ctx){return h2h_resource_tls_get(job,out,test_clock,test_scope,ctx);}
static h2h_resource_work_status_t actual_worker(const h2h_resource_job_t *job,h2h_resource_progress_t *progress,void *ctx){assert(!model_mutex);return h2h_resource_runtime_execute(job,progress,test_tls,ctx);}
static h2h_resource_manifest_t artifact;
static void sign_artifact(unsigned version){
    memset(&artifact,0,sizeof(artifact));strcpy(artifact.id,"h2h-ian-visual");strcpy(artifact.key_id,"fixture");artifact.kind=1;artifact.version=version;artifact.min_firmware=artifact.max_firmware=1;SHA256(payload,sizeof(payload),artifact.sha256);
    char message[512],digest[65];tohex(artifact.sha256,32,digest);
    int n=snprintf(message,sizeof(message),"[\"pocket-visual-v2\",\"member_visual\",\"esp32c3-pocket-8m\",2,\"h2h-ian-visual\",%u,1,1,233984,15360,\"%s\",\"fixture\"]",version,digest);
    char actual[512];assert(h2h_resource_manifest_message(&artifact,actual)==(size_t)n&&!strcmp(actual,message));
    EVP_MD_CTX *ctx=EVP_MD_CTX_new();size_t size=64;assert(EVP_DigestSignInit(ctx,NULL,NULL,NULL,signer)==1&&EVP_DigestSign(ctx,artifact.signature,&size,(unsigned char *)message,(size_t)n)==1&&size==64);EVP_MD_CTX_free(ctx);
}
static void new_owner(void){
    setup(true);p.record.ownership.epoch=6;p.record.ownership.account_id=314;p.record.ownership.activated=true;p.record.revision=7;
    memset(full_present,0,sizeof(full_present));h2h_fullsave_init(&full);r.ops.full=&full;r.ops.full_io=&full_ops;r.ops.resource_work=actual_worker;
    stayc_game_t model;stayc_game_init(&model,12,8,1);model.save.star_balance=123;stayc_game_save_t game;stayc_game_save_snapshot(&model,&game);assert(h2h_fullsave_local(&full,&p,&game,0,&full_ops));
    h2h_resource_runtime_capabilities(&r.ops.resources,true);
}
static void sync_with_resource(unsigned revision){
    char data[221],hash[65];assert(encode(r.sent_full,164,data,sizeof(data))&&hash_full(&r,r.sent_full,hash));
    cJSON *body=cJSON_CreateObject(),*save=cJSON_CreateObject(),*selection=cJSON_CreateObject();
    cJSON_AddNumberToObject(body,"account_id",314);cJSON_AddNumberToObject(body,"ownership_epoch",6);cJSON_AddBoolToObject(body,"activated",true);cJSON_AddBoolToObject(body,"locked",false);cJSON_AddStringToObject(body,"save_status","ok");cJSON_AddBoolToObject(body,"restore_required",false);
    cJSON_AddNumberToObject(save,"revision",8);cJSON_AddNumberToObject(save,"schema",4);cJSON_AddNumberToObject(save,"record_version",2);cJSON_AddStringToObject(save,"data",data);cJSON_AddStringToObject(save,"sha256",hash);cJSON_AddItemToObject(body,"save",save);
    tohex(artifact.sha256,32,hash);cJSON_AddNumberToObject(body,"resource_revision",revision);cJSON_AddStringToObject(selection,"id",artifact.id);cJSON_AddStringToObject(selection,"sha256",hash);cJSON_AddItemToObject(body,"resource_selection",selection);respond_object(body);assert(r.stage==H2H_RELAY_RESOURCE_MANIFEST);
}
static void manifest_response(bool wifi){
    cJSON *body=cJSON_CreateObject(),*m=cJSON_CreateObject(),*transport=cJSON_CreateObject();char sha[65],signature[89];tohex(artifact.sha256,32,sha);assert(encode(artifact.signature,64,signature,sizeof(signature)));
    cJSON_AddNumberToObject(body,"ownership_epoch",6);cJSON_AddNumberToObject(body,"resource_revision",r.resource_revision);
    cJSON_AddStringToObject(m,"kind","member_visual");cJSON_AddStringToObject(m,"model","esp32c3-pocket-8m");cJSON_AddNumberToObject(m,"format",2);cJSON_AddStringToObject(m,"id",artifact.id);cJSON_AddNumberToObject(m,"version",artifact.version);cJSON_AddNumberToObject(m,"min_firmware",1);cJSON_AddNumberToObject(m,"max_firmware",1);cJSON_AddNumberToObject(m,"size",H2H_RESOURCE_BYTES);cJSON_AddNumberToObject(m,"frame_bytes",15360);cJSON_AddStringToObject(m,"sha256",sha);cJSON_AddStringToObject(m,"key_id","fixture");cJSON_AddStringToObject(m,"signature",signature);cJSON_AddItemToObject(body,"manifest",m);
    cJSON_AddStringToObject(transport,"kind",wifi?"wifi":"ble");
    if(wifi){char url[512];snprintf(url,sizeof(url),"https://apps.randomdance.cn/api/v1/passport/relay/v2/resources/%s/blob?ticket=published_fixture_locator_ticket",sha);cJSON_AddStringToObject(transport,"url",url);cJSON_AddNumberToObject(transport,"ttl",300);}
    cJSON_AddItemToObject(body,"transport",transport);respond_object(body);assert(r.stage==H2H_RELAY_RESOURCE_WORK);
}
static h2h_resource_work_status_t work(void){
    h2h_resource_job_t job;assert(h2h_relay_resource_take(&r,&job));job.connection_generation=connection_generation;h2h_resource_progress_t progress;
    h2h_resource_work_status_t status=actual_worker(&job,&progress,NULL);(void)h2h_relay_resource_done(&r,&job,status,&progress,clock_ms);return status;
}
static void chunk_response(bool corrupt){
    uint32_t offset=r.resource_job.offset,length=r.resource_job.length;unsigned char hash[32];SHA256(payload+offset,length,hash);
    char data[1369],sha[65],digest[65],body[1800];assert(encode(payload+offset,length,data,sizeof(data)));tohex(hash,32,digest);tohex(artifact.sha256,32,sha);if(corrupt)digest[0]=digest[0]=='a'?'b':'a';
    snprintf(body,sizeof(body),"{\"ownership_epoch\":6,\"resource_revision\":%u,\"sha256\":\"%s\",\"offset\":%u,\"total_size\":233984,\"data\":\"%s\",\"chunk_sha256\":\"%s\"}",r.resource_revision,sha,offset,data,digest);response(body);assert(r.stage==H2H_RELAY_RESOURCE_WORK);
}
static void open_resource(bool wifi,unsigned revision,unsigned chunk){
    new_owner();char session[33];snprintf(session,sizeof(session),"%032x",rng++);v2_open(session);
    char body[220];snprintf(body,sizeof(body),"{\"ownership_epoch\":6,\"save_profile\":\"stayc-epoch-v2-164\",\"resource_profile\":\"pocket-visual-v2\",\"transport\":\"%s\",\"max_chunk_bytes\":%u}",wifi?"wifi":"ble",chunk);response(body);assert(r.stage==H2H_RELAY_SYNC);
    sync_with_resource(revision);manifest_response(wifi);
}
static void render(void){(void)h2h_resource_runtime_view_begin(6,p.model_generation);h2h_resource_runtime_view_end();}
static void finish_transfer(void){
    unsigned loops=0;while(r.stage==H2H_RELAY_RESOURCE_WORK||r.stage==H2H_RELAY_RESOURCE_CHUNK){
        assert(++loops<1200);if(r.stage==H2H_RELAY_RESOURCE_CHUNK)chunk_response(false);
        h2h_resource_job_kind_t kind=r.resource_job.kind;if(kind==H2H_RESOURCE_JOB_RECEIPT)render();work();
    }
    assert(r.stage==H2H_RELAY_RESOURCE_ACK&&!r.resource_applied);cJSON *ack=parse(r.resource_ack_json,strlen(r.resource_ack_json));cJSON_AddStringToObject(ack,"status","applied");respond_object(ack);assert(work()==H2H_RESOURCE_WORK_OK&&r.stage==H2H_RELAY_ACK);
    ack_response();assert(r.stage==H2H_RELAY_RESOURCE_WORK&&r.resource_complete_pending);assert(work()==H2H_RESOURCE_WORK_OK&&r.stage==H2H_RELAY_COMPLETE);
    const char poll[]="{\"action\":\"poll\"}";assert(h2h_relay_rpc(&r,poll,sizeof(poll)-1,output,clock_ms));assert(strstr(output,"\"resource_status\":\"applied\"")&&strstr(output,"\"resource_ack\"")&&strstr(output,"\"game_stars\":123"));
    assert(h2h_resource_runtime_installed(6,r.resource_revision,artifact.sha256));
}
static void tls_fault_tests(void){
    h2h_resource_job_t job={.kind=H2H_RESOURCE_JOB_WIFI,.epoch=6,.model_generation=p.model_generation,.revision=1,.connection_generation=connection_generation,.length=700,.locator_started_ms=clock_ms,.locator_ttl_ms=300000,.session_started_ms=clock_ms,.session_ttl_ms=300000};job.manifest=artifact;
    char sha[65];tohex(artifact.sha256,32,sha);snprintf(job.locator,sizeof(job.locator),"https://apps.randomdance.cn/api/v1/passport/relay/v2/resources/%s/blob?ticket=published_fixture_locator_ticket",sha);
    unsigned char bytes[1024];tls_mode=0;assert(test_tls(&job,bytes,NULL)&&!memcmp(bytes,payload,700));
    for(tls_mode=1;tls_mode<=10;tls_mode++){job.locator_started_ms=job.session_started_ms=clock_ms;assert(!test_tls(&job,bytes,NULL));}
    tls_mode=0;job.locator_started_ms=clock_ms-300000;unsigned before=tls_calls;assert(!test_tls(&job,bytes,NULL)&&tls_calls==before);
    job.locator_started_ms=job.session_started_ms=clock_ms;char *ticket=strstr(job.locator,"ticket=");assert(ticket);ticket[7]='.';assert(!test_tls(&job,bytes,NULL)&&tls_calls==before);
    puts("TLS actual adapter: CA/hostname/nonblocking configuration, bounded range, redirect/status/range/encoding/length/duplicates/truncation, deadline and locator rejection PASS");
}
static void state_file(FILE *file,bool saving){
    unsigned char flags[2]={(unsigned char)journal_exists,(unsigned char)resume_exists};
    if(saving){assert(fwrite(flags,1,2,file)==2&&fwrite(flash,1,sizeof(flash),file)==sizeof(flash)&&fwrite(journal,1,sizeof(journal),file)==sizeof(journal)&&fwrite(resume,1,sizeof(resume),file)==sizeof(resume));}
    else{assert(fread(flags,1,2,file)==2&&fread(flash,1,sizeof(flash),file)==sizeof(flash)&&fread(journal,1,sizeof(journal),file)==sizeof(journal)&&fread(resume,1,sizeof(resume),file)==sizeof(resume));journal_exists=flags[0];resume_exists=flags[1];}
}
static void real_process_reboot(const char *program,const char *image){
    char path[]="/tmp/pocket-resource-rpc-reboot-XXXXXX";int fd=mkstemp(path);assert(fd>=0);FILE *file=fdopen(fd,"wb");assert(file);state_file(file,true);assert(fclose(file)==0);
    fflush(NULL);pid_t child=fork();assert(child>=0);
    if(!child){execl(program,program,"--resume-case",image,path,(char *)NULL);_exit(127);}
    int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);assert(unlink(path)==0);
}
int main(int argc,char **argv){
    bool reboot=argc==4&&!strcmp(argv[1],"--resume-case");assert(argc==2||reboot);const char *image=reboot?argv[2]:argv[1];
    FILE *input=fopen(image,"rb");assert(input&&fread(payload,1,sizeof(payload),input)==sizeof(payload)&&fgetc(input)==EOF);fclose(input);
    for(unsigned i=0;i<32;i++)key[i]=(uint8_t)i;
    signer=EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519,NULL,key,32);assert(signer);size_t n=32;assert(EVP_PKEY_get_raw_public_key(signer,public_fixture,&n)==1&&n==32);
    if(reboot){FILE *state=fopen(argv[3],"rb");assert(state);state_file(state,false);fclose(state);}
    else memset(flash,255,sizeof(flash));
    assert(h2h_resource_runtime_start()==ESP_OK);
    if(reboot){sign_artifact(4);open_resource(false,4,1024);assert(work()==H2H_RESOURCE_WORK_OK&&r.resource_progress.cursor==4096);render();finish_transfer();EVP_PKEY_free(signer);puts("Actual fresh-process runtime reboot: loaded durable NVS, rehashed staged4096 prefix, re-erased incomplete sector and completed current signed ACK PASS");return 0;}
    sign_artifact(1);
    open_resource(false,1,700);assert(work()==H2H_RESOURCE_WORK_OK);chunk_response(false);assert(work()==H2H_RESOURCE_WORK_OK);finish_transfer();assert(!memcmp(flash[0]+4096,payload,sizeof(payload)));
    unsigned char active_sha[32];SHA256(flash[0],sizeof(flash[0]),active_sha);
    sign_artifact(2);open_resource(true,2,700);tls_fail_offset=4096;assert(work()==H2H_RESOURCE_WORK_OK);
    while(r.stage==H2H_RELAY_RESOURCE_WORK&&r.resource_wifi)work();
    assert(r.stage==H2H_RELAY_RESOURCE_CHUNK&&r.resource_progress.cursor==4096&&!r.resource_wifi);
    unsigned char prefix[32];SHA256(payload,4096,prefix);assert(!memcmp(prefix,r.resource_progress.prefix_sha256,32));finish_transfer();assert(!memcmp(flash[1]+4096,payload,sizeof(payload)));tls_fail_offset=UINT32_MAX;
    unsigned char after[32];SHA256(flash[0],sizeof(flash[0]),after);assert(!memcmp(after,active_sha,32));tls_fault_tests();
    /* Unknown/mutated signature and bad chunk hashes cannot touch slots. */
    sign_artifact(3);open_resource(false,3,1024);r.resource_job.manifest.signature[0]^=1;unsigned writes_before=slot_writes,erases_before=slot_erases;assert(work()==H2H_RESOURCE_WORK_FAILED&&r.stage==H2H_RELAY_ERROR&&slot_writes==writes_before&&slot_erases==erases_before);
    /* Explicit restart only after matching authenticated owner and a concrete
     * old journal mismatch. It never edits the active/mapped bank. */
    open_resource(false,3,1024);assert(work()==H2H_RESOURCE_WORK_OK);h2h_relay_disconnect(&r);
    sign_artifact(4);open_resource(false,4,1024);assert(work()==H2H_RESOURCE_WORK_RESTART_REQUIRED&&!strcmp(r.error,"resource_restart_required"));
    const char restart[]="{\"action\":\"resource_restart\"}";assert(h2h_relay_rpc(&r,restart,sizeof(restart)-1,output,clock_ms));assert(work()==H2H_RESOURCE_WORK_OK);
    writes_before=slot_writes;chunk_response(true);assert(work()==H2H_RESOURCE_WORK_FAILED&&slot_writes==writes_before);
    open_resource(false,4,1024);assert(work()==H2H_RESOURCE_WORK_OK);for(unsigned i=0;i<3;i++){chunk_response(false);assert(work()==H2H_RESOURCE_WORK_OK);}fail_nvs=true;chunk_response(false);assert(work()==H2H_RESOURCE_WORK_FAILED);fail_nvs=false;
    open_resource(false,4,1024);assert(work()==H2H_RESOURCE_WORK_OK&&r.resource_progress.cursor==0);
    for(unsigned i=0;i<6;i++){chunk_response(false);assert(work()==H2H_RESOURCE_WORK_OK);}assert(r.resource_progress.cursor==6144);real_process_reboot(argv[0],image);
    open_resource(false,4,1024);assert(work()==H2H_RESOURCE_WORK_OK&&r.resource_progress.cursor==4096);
    while(r.stage==H2H_RELAY_RESOURCE_CHUNK){chunk_response(false);assert(work()==H2H_RESOURCE_WORK_OK);}assert(r.resource_job.kind==H2H_RESOURCE_JOB_FINISH);assert(work()==H2H_RESOURCE_WORK_OK);
    fail_nvs=true;render();fail_nvs=false;assert(work()==H2H_RESOURCE_WORK_FAILED&&r.stage==H2H_RELAY_ERROR&&!r.resource_applied&&!r.resource_ack_json[0]);
    open_resource(false,4,1024);assert(work()==H2H_RESOURCE_WORK_OK);h2h_resource_job_t old;assert(h2h_relay_resource_take(&r,&old));old.connection_generation=connection_generation;h2h_relay_disconnect(&r);connection_generation++;h2h_resource_progress_t progress;assert(actual_worker(&old,&progress,NULL)==H2H_RESOURCE_WORK_FAILED);assert(!h2h_relay_resource_done(&r,&old,H2H_RESOURCE_WORK_OK,&progress,clock_ms)&&!r.resource_applied);
    /* A restored model between signed chunk requests cannot adopt the new
     * generation into this frozen selection. No worker or flash write starts. */
    open_resource(false,4,1024);assert(work()==H2H_RESOURCE_WORK_OK);p.model_generation++;
    writes_before=slot_writes;const char poll[]="{\"action\":\"poll\"}";
    assert(h2h_relay_rpc(&r,poll,sizeof(poll)-1,output,clock_ms)&&!strcmp(r.error,"resource_scope_changed")&&slot_writes==writes_before);
    cJSON *error=parse(output,strlen(output));assert(error&&cJSON_GetArraySize(error)==2);cJSON_Delete(error);
    keys_enabled=false;new_owner();v2_open("dddddddddddddddddddddddddddddddd");response("{\"ownership_epoch\":6,\"save_profile\":\"stayc-epoch-v2-164\",\"resource_profile\":\"pocket-visual-v2\",\"transport\":\"ble\",\"max_chunk_bytes\":1024}");assert(!strcmp(r.error,"resource_unsupported"));
    error=parse(output,strlen(output));assert(error&&cJSON_GetArraySize(error)==2);cJSON_Delete(error);
    const char status_query[]="{\"action\":\"resource_status\"}";assert(h2h_relay_rpc(&r,status_query,sizeof(status_query)-1,output,clock_ms)&&strstr(output,"\"status\":\"unsupported\""));
    EVP_PKEY_free(signer);puts("Signed v2 resource RPC -> actual runtime/flash/NVS/render -> current resource ACK -> full-save completion; TLS/BLE handover, prefix, explicit restart, checkpoint failure, active-bank protection and disconnect PASS");return 0;
}
