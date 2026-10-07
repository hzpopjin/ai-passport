#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include "../main/h2h/h2h_relay.c"
#include "h2h_relay_transport.h"
static uint8_t key[32],disk[H2H_POCKET_RECORD_SIZE];static unsigned writes,rng=1;static bool write_ok=true;
static bool hm(const uint8_t k[32],const char *s,size_t n,uint8_t out[32]){unsigned size;return HMAC(EVP_sha256(),k,32,(const unsigned char *)s,n,out,&size)&&size==32;}
static bool sh(const void *s,size_t n,uint8_t out[32]){return SHA256(s,n,out)!=NULL;}
static void random_bytes(void *s,size_t n){uint8_t *p=s;while(n--)*p++=(uint8_t)rng++;}
static bool wr(const unsigned char *s,size_t n,void *ctx){(void)ctx;assert(n==H2H_POCKET_RECORD_SIZE);writes++;if(write_ok)memcpy(disk,s,n);return write_ok;}
static h2h_relay_t r;static h2h_pocket_t p;static char output[4097];static cJSON *fixtures;
static void setup(bool enrolled){h2h_pocket_init(&p,12,8);p.record.ownership.epoch=5;h2h_relay_ops_t ops={.hmac=hm,.sha=sh,.random=random_bytes,.write=wr};h2h_relay_init(&r,&p,"0123456789abcdef0123456789abcdef",enrolled?key:NULL,&ops);writes=0;write_ok=true;}
static cJSON *envelope(const char *direction,const char *operation,const char *body,const char *session,const char *request_id){
 char b[2733];assert(encode(body,strlen(body),b,sizeof(b)));cJSON *e=cJSON_CreateObject();cJSON_AddNumberToObject(e,"version",r.version);cJSON_AddStringToObject(e,"device_id",r.device);cJSON_AddStringToObject(e,"session_id",session);cJSON_AddStringToObject(e,"request_id",request_id);cJSON_AddStringToObject(e,"operation",operation);cJSON_AddStringToObject(e,"body_b64",b);cJSON_AddStringToObject(e,"mac","0000000000000000000000000000000000000000000000000000000000000000");char h[65];assert(mac(&r,direction,e,h));cJSON_ReplaceItemInObjectCaseSensitive(e,"mac",cJSON_CreateString(h));return e;
}
static void rpc(cJSON *o,uint32_t now){char *s=cJSON_PrintUnformatted(o);assert(h2h_relay_rpc(&r,s,strlen(s),output,now)>0);cJSON_free(s);cJSON_Delete(o);cJSON *result=parse(output,strlen(output));assert(result);cJSON_Delete(result);}
static void open(const char *mode,uint32_t account,uint32_t now,const char *session){char b[250];snprintf(b,sizeof(b),"{\"account_id\":%u,\"mode\":\"%s\",\"ttl\":300,\"ownership_epoch\":%u,\"device_activated\":%s,\"owner_authorized\":%s}",account,mode,!strcmp(mode,"pair")?5u:6u,!strcmp(mode,"pair")?"false":"true",!strcmp(mode,"pair")?"false":"true");cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"action","open");cJSON_AddItemToObject(o,"session",envelope("session","session",b,session,"11111111111111111111111111111111"));rpc(o,now);}
static void response(const char *body){cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"action","deliver");cJSON_AddItemToObject(o,"envelope",envelope("response",r.operation,body,r.session,r.request));rpc(o,10);}
static void pair_to_sync(void){open("pair",314,0,"abcdefabcdefabcdefabcdefabcdefab");assert(r.stage==H2H_RELAY_BEGIN&&r.gate);assert(!h2h_relay_confirm(&r,1));response("{\"code\":\"55555555555555555555555555555555\",\"ownership_epoch\":5}");assert(r.stage==H2H_RELAY_CLAIM);response("{\"account_id\":314,\"status\":\"awaiting_device_confirmation\"}");assert(r.stage==H2H_RELAY_CARD_CONFIRM&&writes==0);assert(h2h_relay_confirm(&r,5));assert(!h2h_relay_confirm(&r,6));response("{\"account_id\":314,\"ownership_epoch\":6,\"activated\":true}");assert(r.stage==H2H_RELAY_SYNC&&!h2h_pocket_can_play(&p));}
static char *sync_body(const char *status,unsigned revision,bool restore){char *s=malloc(900);snprintf(s,900,"{\"account_id\":314,\"ownership_epoch\":6,\"activated\":true,\"locked\":false,\"save_status\":\"%s\",\"restore_required\":%s,\"save\":{\"revision\":%u,\"schema\":1,\"data\":\"%s\",\"sha256\":\"%s\"}}",status,restore?"true":"false",revision,str(fixtures,"save48_b64"),str(fixtures,"save48_sha256"));return s;}
static void ack_response(void){cJSON *o=parse(r.ack_json,strlen(r.ack_json));cJSON_AddBoolToObject(o,"completed",true);char *s=cJSON_PrintUnformatted(o);response(s);cJSON_free(s);cJSON_Delete(o);}
static void fixture_tests(void){cJSON *vectors=cJSON_GetObjectItemCaseSensitive(fixtures,"vectors");for(int i=0;i<cJSON_GetArraySize(vectors);i++){cJSON *v=cJSON_GetArrayItem(vectors,i),*e=cJSON_GetObjectItemCaseSensitive(v,"envelope");char h[65];assert(mac(&r,str(v,"direction"),e,h));assert(!strcmp(h,str(e,"mac")));cJSON *body=verify(&r,e,str(v,"direction"));assert(body);cJSON_Delete(body);}assert(decode("Zh==",(uint8_t *)output,2)==0);assert(decode("Zg==",(uint8_t *)output,2)==1);}
static void frame_tests(void){h2h_relay_transport_t t={0};cJSON *f=cJSON_GetObjectItemCaseSensitive(fixtures,"rpc"),*frames=cJSON_GetObjectItemCaseSensitive(f,"input_frames_hex");for(int i=0;i<cJSON_GetArraySize(frames);i++){const char *s=cJSON_GetArrayItem(frames,i)->valuestring;uint8_t bytes[20];size_t n=strlen(s)/2;for(size_t j=0;j<n;j++){char q[3]={s[j*2],s[j*2+1],0};bytes[j]=(uint8_t)strtoul(q,NULL,16);}assert(n<=20&&h2h_relay_transport_write(&t,bytes,n));}
 uint32_t g;char *b=h2h_relay_transport_take(&t,&g);assert(b&&!strcmp(b,str(f,"input_json_utf8")));uint8_t bad[]={0x10,0x34,0x12,1,0};assert(!h2h_relay_transport_write(&t,bad,5));strcpy(b,"{\"state\":\"waiting\"}");assert(h2h_relay_transport_finish(&t,g,strlen(b)));uint8_t selection[]={0x13,0x34,0x12,0,0},out1[20],out2[20];assert(h2h_relay_transport_write(&t,selection,5));assert(h2h_relay_transport_read(&t,out1)==20);assert(h2h_relay_transport_read(&t,out2)==20&&!memcmp(out1,out2,20));assert(out1[1]==1&&out1[2]==0x34&&out1[3]==0x12);
 h2h_relay_transport_reset(&t);assert(h2h_relay_transport_write(&t,bad,5));uint8_t data[]={0x11,0x34,0x12,1,0,'a'};assert(!h2h_relay_transport_write(&t,data,6));data[3]=0;assert(h2h_relay_transport_write(&t,data,6));uint8_t exe[]={0x12,0x34,0x12};assert(h2h_relay_transport_write(&t,exe,3));b=h2h_relay_transport_take(&t,&g);assert(b);h2h_relay_transport_reset(&t);assert(!h2h_relay_transport_finish(&t,g,1));
}

static const char *op="cccccccccccccccccccccccccccccccc";
static void authority(unsigned account,unsigned epoch,bool active,bool authorized,const char *session) {
 char b[250];snprintf(b,sizeof(b),"{\"account_id\":%u,\"mode\":\"sync\",\"ttl\":300,\"ownership_epoch\":%u,\"device_activated\":%s,\"owner_authorized\":%s}",account,epoch,active?"true":"false",authorized?"true":"false");
 cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"action","open");cJSON_AddItemToObject(o,"session",envelope("session","session",b,session,"11111111111111111111111111111111"));rpc(o,0);
}
static void pending_setup(void) {
 setup(true);p.record.ownership.account_id=314;p.record.ownership.activated=true;p.record.revision=2;
 assert(h2h_pocket_unbind(&p,op,wr,NULL));
 h2h_pocket_init(&p,12,8);assert(h2h_pocket_decode(&p,disk,sizeof(disk)));
 assert(!h2h_pocket_can_play(&p));
 authority(315,5,true,false,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
 assert(r.stage==H2H_RELAY_SYNC&&r.pending_only&&r.gate&&!h2h_relay_confirm(&r,1));
 cJSON *e=parse(r.request_json,strlen(r.request_json));char b[2049];size_t n=decode(str(e,"body_b64"),(uint8_t *)b,2048);b[n]=0;
 cJSON *body=parse(b,n),*proof=cJSON_GetObjectItemCaseSensitive(body,"pending_unbind");uint64_t epoch;
 assert(cJSON_GetArraySize(body)==2&&!cJSON_GetObjectItemCaseSensitive(body,"capabilities"));
  assert(proof&&!strcmp(str(proof,"operation_id"),op)&&num(proof,"expected_epoch",&epoch)&&epoch==5);
 assert(!cJSON_GetObjectItemCaseSensitive(body,"save")&&!cJSON_GetObjectItemCaseSensitive(body,"save_ack"));
 cJSON_Delete(body);cJSON_Delete(e);
}
static cJSON *minimal(unsigned epoch,bool superseded,const char *receipt) {
 cJSON *b=cJSON_CreateObject();cJSON_AddNumberToObject(b,"ownership_epoch",epoch);cJSON_AddNullToObject(b,"account_id");
 cJSON_AddBoolToObject(b,"activated",false);cJSON_AddBoolToObject(b,"locked",true);cJSON_AddStringToObject(b,"save_status",superseded?"pending_unbind_superseded":"ownership_changed");cJSON_AddNullToObject(b,"save");
 if(superseded){cJSON_AddNullToObject(b,"pending_unbind_ack");cJSON_AddStringToObject(b,"pending_unbind_rejected",receipt);}
 else{cJSON_AddStringToObject(b,"pending_unbind_ack",receipt);cJSON_AddNullToObject(b,"pending_unbind_rejected");}
 return b;
}
static void respond_object(cJSON *body) {char *b=cJSON_PrintUnformatted(body);response(b);cJSON_free(b);cJSON_Delete(body);}
static void owner_snapshot(unsigned account,unsigned epoch,unsigned revision) {
 char *s=sync_body("unchanged",revision,false);cJSON *b=parse(s,strlen(s));free(s);
 cJSON_ReplaceItemInObjectCaseSensitive(b,"account_id",cJSON_CreateNumber(account));cJSON_ReplaceItemInObjectCaseSensitive(b,"ownership_epoch",cJSON_CreateNumber(epoch));respond_object(b);
}
static void transition_tests(void) {
 pending_setup();h2h_save_t old=p.record.save;respond_object(minimal(6,false,op));
 assert(r.stage==H2H_RELAY_ERROR&&!strcmp(r.error,"pending_unbind_settled")&&r.gate&&!p.record.ownership.pending_unbind[0]&&p.record.ownership.epoch==5&&!memcmp(&old,&p.record.save,sizeof(old))&&!h2h_pocket_can_play(&p));
 h2h_pocket_t reboot;h2h_pocket_init(&reboot,12,8);assert(h2h_pocket_decode(&reboot,disk,sizeof(disk))&&!reboot.record.ownership.pending_unbind[0]&&!h2h_pocket_can_play(&reboot));
 pending_setup();respond_object(minimal(9,true,op));assert(r.stage==H2H_RELAY_ERROR&&!strcmp(r.error,"pending_unbind_superseded")&&p.record.ownership.epoch==5&&!p.record.ownership.pending_unbind[0]);
 authority(999,9,true,true,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");assert(r.stage==H2H_RELAY_SYNC&&!r.pending_only);owner_snapshot(999,9,7);assert(r.stage==H2H_RELAY_ACK&&p.record.ownership.account_id==999&&p.record.ownership.epoch==9);ack_response();assert(r.stage==H2H_RELAY_COMPLETE&&!r.gate);
 for(unsigned bad=0;bad<6;bad++) {
  pending_setup();h2h_pocket_record_t before=p.record;unsigned count=writes;cJSON *b=minimal(bad==1?5:6,false,bad==0?"dddddddddddddddddddddddddddddddd":op);
  if(bad==2)cJSON_ReplaceItemInObjectCaseSensitive(b,"pending_unbind_rejected",cJSON_CreateString(op));
  if(bad==3)cJSON_ReplaceItemInObjectCaseSensitive(b,"account_id",cJSON_CreateNumber(999));
  if(bad==4)cJSON_ReplaceItemInObjectCaseSensitive(b,"save",cJSON_CreateObject());
  if(bad==5)cJSON_AddStringToObject(b,"resources","forbidden");
  respond_object(b);assert(r.stage==H2H_RELAY_ERROR&&writes==count&&!memcmp(&before,&p.record,sizeof(before)));
 }
 pending_setup();p.record.ownership.epoch=9;p.record.ownership.account_id=999;p.record.ownership.activated=true;
 h2h_pocket_record_t newer=p.record;unsigned count=writes;respond_object(minimal(9,true,op));assert(writes==count&&!memcmp(&newer,&p.record,sizeof(newer)));
 pending_setup();write_ok=false;respond_object(minimal(6,false,op));assert(!p.healthy&&p.record.ownership.pending_unbind[0]&&r.gate);
 setup(true);p.record.ownership.account_id=314;p.record.ownership.activated=true;h2h_pocket_record_t before=p.record;
 authority(315,5,true,false,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");assert(r.stage==H2H_RELAY_ERROR&&!memcmp(&before,&p.record,sizeof(before))&&!writes);
 /* A carried pending proof may settle without changing the newer owner/save. */
 pending_setup();h2h_relay_disconnect(&r);p.record.ownership.epoch=9;p.record.ownership.account_id=999;p.record.ownership.activated=true;p.record.ownership.reset_required=true;
 old=p.record.save;authority(315,9,true,false,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");assert(r.pending_only&&r.stage==H2H_RELAY_SYNC);respond_object(minimal(9,true,op));
 assert(p.record.ownership.epoch==9&&p.record.ownership.account_id==999&&p.record.ownership.activated&&p.record.ownership.reset_required&&!memcmp(&old,&p.record.save,sizeof(old))&&!h2h_pocket_can_play(&p));
 authority(999,9,true,true,"dddddddddddddddddddddddddddddddd");owner_snapshot(999,9,7);assert(r.stage==H2H_RELAY_ACK&&!p.record.ownership.reset_required);ack_response();assert(r.stage==H2H_RELAY_COMPLETE);
 /* Identical snapshots in fresh sessions still need a new signed ACK. */
 setup(true);pair_to_sync();char *s=sync_body("unchanged",7,false);response(s);free(s);char old_ack[768],old_nonce[33],old_session[33];strcpy(old_ack,r.ack_json);strcpy(old_nonce,r.request);strcpy(old_session,r.session);ack_response();
 authority(314,6,true,true,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");assert(r.stage==H2H_RELAY_SYNC&&!r.ack_json[0]&&!r.ack_record.revision);owner_snapshot(314,6,7);assert(r.stage==H2H_RELAY_ACK&&strcmp(old_nonce,r.request));ack_response();assert(r.stage==H2H_RELAY_COMPLETE);
 authority(314,6,true,true,"dddddddddddddddddddddddddddddddd");assert(r.stage==H2H_RELAY_SYNC&&!r.ack_json[0]);
 cJSON *b=parse(old_ack,strlen(old_ack));cJSON_AddBoolToObject(b,"completed",true);s=cJSON_PrintUnformatted(b);cJSON_Delete(b);cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"action","deliver");cJSON_AddItemToObject(o,"envelope",envelope("response","ack",s,old_session,old_nonce));cJSON_free(s);rpc(o,10);assert(r.stage==H2H_RELAY_ERROR&&!strstr(output,"\"state\":\"complete\""));
 h2h_relay_disconnect(&r);authority(314,6,true,true,"dddddddddddddddddddddddddddddddd");assert(r.stage==H2H_RELAY_ERROR&&!r.active);
 puts("cloud-2 R7 persisted proof/nonowner-only admission, higher/superseded receipts, new-owner isolation, write failure, fresh sessions and ACK cycles: PASS");
}

static uint8_t full_banks[2][1024],full_ptr[32];static bool full_present[3];
static int full_read_test(int bank,void *out,size_t n,void *ctx){(void)ctx;if(!full_present[bank])return 0;memcpy(out,bank==2?full_ptr:full_banks[bank],n);return 1;}
static bool full_write_test(int bank,const void *in,size_t n,void *ctx){(void)ctx;if(!write_ok)return false;memcpy(bank==2?full_ptr:full_banks[bank],in,n);full_present[bank]=true;return true;}
static const h2h_fullsave_io_t full_ops={full_read_test,full_write_test,NULL};static h2h_fullsave_t full;
static void v2_open(const char *session){
 r.version=2;cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"action","open");
 cJSON_AddItemToObject(o,"session",envelope("session","session","{\"account_id\":314,\"mode\":\"sync\",\"ttl\":300,\"ownership_epoch\":6,\"device_activated\":true,\"owner_authorized\":true,\"save_profiles\":[\"cloud48-v1\",\"stayc-epoch-v2-164\"],\"resource_profiles\":[\"pocket-visual-v2\"],\"max_chunk_bytes\":1024}",session,"11111111111111111111111111111111"));rpc(o,0);
}
static void v2_capabilities(void){response("{\"ownership_epoch\":6,\"save_profile\":\"stayc-epoch-v2-164\",\"resource_profile\":null,\"transport\":null,\"max_chunk_bytes\":1024}");}
static void v2_snapshot(const uint8_t bytes[164],unsigned revision){
 char data[221],hash[65],body[900];assert(encode(bytes,164,data,sizeof(data))&&hash_full(&r,bytes,hash));
 snprintf(body,sizeof(body),"{\"account_id\":314,\"ownership_epoch\":6,\"activated\":true,\"locked\":false,\"save_status\":\"ok\",\"restore_required\":false,\"save\":{\"revision\":%u,\"schema\":4,\"record_version\":2,\"data\":\"%s\",\"sha256\":\"%s\"}}",revision,data,hash);response(body);
}
static void v2_tests(void){
 setup(true);p.record.ownership.epoch=6;p.record.ownership.account_id=314;p.record.ownership.activated=true;p.record.revision=7;
 memset(full_present,0,sizeof(full_present));h2h_fullsave_init(&full);r.ops.full=&full;r.ops.full_io=&full_ops;
 stayc_game_t model;stayc_game_init(&model,12,8,1);model.save.star_balance=123;stayc_game_save_t game;stayc_game_save_snapshot(&model,&game);
 assert(h2h_fullsave_local(&full,&p,&game,0,&full_ops));
 v2_open("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");assert(r.stage==H2H_RELAY_CAPABILITIES&&!r.full_selected&&!strstr(output,"\"state\":\"complete\""));
 cJSON *e=parse(r.request_json,strlen(r.request_json));cJSON *cap=verify(&r,e,"request");assert(cap&&cJSON_GetArraySize(cap)==9);cJSON_Delete(cap);cJSON_Delete(e);
 v2_capabilities();assert(r.stage==H2H_RELAY_SYNC&&r.full_selected);uint8_t sent[164];memcpy(sent,r.sent_full,164);char round[33];strcpy(round,r.request);
 /* A later complete local transaction survives an older immutable HTTP reply. */
 model.save.star_balance=124;stayc_game_save_snapshot(&model,&game);assert(h2h_fullsave_local(&full,&p,&game,0,&full_ops));
 v2_snapshot(sent,8);assert(r.stage==H2H_RELAY_SYNC&&full.dirty&&full.revision==8&&strcmp(r.request,round));
 memcpy(sent,r.sent_full,164);strcpy(round,r.request);v2_snapshot(sent,9);assert(r.stage==H2H_RELAY_ACK&&!full.dirty&&full.server_full);
 assert(strstr(r.ack_json,round)&&strstr(r.ack_json,"\"game_stars\":124")&&strstr(r.ack_json,"\"extended_save_cloud_synced\":true"));cJSON *proof=parse(r.ack_json,strlen(r.ack_json));assert(cJSON_GetArraySize(proof)==17);cJSON_Delete(proof);ack_response();assert(r.stage==H2H_RELAY_COMPLETE&&!r.gate);
 cJSON *complete=parse(output,strlen(output));uint8_t actual[164];uint64_t schema;assert(cJSON_GetArraySize(complete)==16&&num(complete,"save_schema",&schema)&&schema==4&&decode(str(complete,"save_data"),actual,164)==164&&!memcmp(actual,sent,164));cJSON_Delete(complete);
 char old_nonce[33],old_session[33],old_ack[1050];strcpy(old_nonce,r.request);strcpy(old_session,r.session);strcpy(old_ack,r.ack_json);
 v2_open("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");v2_capabilities();assert(!r.ack_json[0]);v2_snapshot(sent,9);assert(r.stage==H2H_RELAY_ACK&&strcmp(old_nonce,r.request));
 cJSON *b=parse(old_ack,strlen(old_ack));cJSON_AddBoolToObject(b,"completed",true);char *text=cJSON_PrintUnformatted(b);cJSON_Delete(b);
 cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"action","deliver");cJSON_AddItemToObject(o,"envelope",envelope("response","ack",text,old_session,old_nonce));cJSON_free(text);rpc(o,10);assert(r.stage==H2H_RELAY_ERROR&&!strstr(output,"\"state\":\"complete\""));
 r.version=1;open("sync",314,0,"cccccccccccccccccccccccccccccccc");assert(r.stage==H2H_RELAY_ERROR&&!strcmp(r.error,"upgrade_required"));
 v2_open("dddddddddddddddddddddddddddddddd");assert(r.stage==H2H_RELAY_CAPABILITIES);response("{\"ownership_epoch\":6,\"save_profile\":\"cloud48-v1\",\"resource_profile\":null,\"transport\":null,\"max_chunk_bytes\":1024}");assert(r.stage==H2H_RELAY_ERROR);
 /* D1 also admits a signed pair ticket; proof-only body remains exactly2keys. */
 pending_setup();h2h_relay_disconnect(&r);cJSON *pair=cJSON_CreateObject();cJSON_AddStringToObject(pair,"action","open");cJSON_AddItemToObject(pair,"session",envelope("session","session","{\"account_id\":315,\"mode\":\"pair\",\"ttl\":300,\"ownership_epoch\":5,\"device_activated\":true,\"owner_authorized\":false}","eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee","11111111111111111111111111111111"));rpc(pair,0);
 assert(r.pending_only&&r.stage==H2H_RELAY_SYNC&&!h2h_relay_confirm(&r,1));respond_object(minimal(6,false,op));assert(!r.active&&!r.ack_json[0]&&r.gate);
 puts("cloud-3 v2 domain/capabilities/full164 promotion, wallet, durable ACK current-sync identity, in-flight progress, stale ACK and v1 downgrade gate; D1 pair proof-only terminal: PASS");
}

int main(int argc,char **argv){assert(argc==2);FILE *f=fopen(argv[1],"r");assert(f);char bytes[20000];size_t n=fread(bytes,1,sizeof(bytes)-1,f);fclose(f);bytes[n]=0;fixtures=parse(bytes,n);assert(fixtures);for(unsigned i=0;i<32;i++)key[i]=(uint8_t)i;
 setup(true);fixture_tests();frame_tests();
 setup(false);open("pair",314,0,"abcdefabcdefabcdefabcdefabcdefab");assert(!strcmp(r.error,"relay_enrollment_required")&&writes==0);
 setup(true);pair_to_sync();h2h_relay_stars(&r,6,p.model_generation+1,23);char *s=sync_body("unchanged",7,false);response(s);free(s);assert(r.stage==H2H_RELAY_ACK&&r.gate&&writes==1);assert(strstr(r.ack_json,"\"game_stars\":23")&&strstr(r.ack_json,"\"game_hearts\":40"));ack_response();assert(r.stage==H2H_RELAY_COMPLETE&&!r.gate&&strstr(output,"\"save_revision\":7")&&strstr(output,"\"extended_save_cloud_synced\":false"));
 h2h_relay_disconnect(&r);open("sync",315,0,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");assert(r.stage==H2H_RELAY_ERROR&&p.record.ownership.account_id==314);
 setup(true);pair_to_sync();s=sync_body("unchanged",7,false);write_ok=false;response(s);free(s);assert(r.stage==H2H_RELAY_ERROR&&!p.healthy&&r.gate);
 setup(true);pair_to_sync();s=sync_body("unchanged",7,false);response(s);free(s);cJSON *a=parse(r.ack_json,strlen(r.ack_json));cJSON_AddBoolToObject(a,"completed",true);cJSON_ReplaceItemInObjectCaseSensitive(a,"account_id",cJSON_CreateNumber(315));s=cJSON_PrintUnformatted(a);response(s);cJSON_free(s);cJSON_Delete(a);assert(r.stage==H2H_RELAY_ERROR&&r.gate);
 setup(true);pair_to_sync();s=sync_body("unchanged",7,false);response(s);free(s);h2h_save_t progress=p.record.save;progress.hearts++;h2h_model_t model;h2h_init(&model,12,8,1);assert(h2h_load(&model,&p.record.save,48));model.save.hearts++;h2h_save_snapshot(&model,&progress);assert(h2h_pocket_local_save(&p,6,&progress,wr,NULL));ack_response();assert(r.stage==H2H_RELAY_SYNC&&r.gate&&p.record.dirty);
 s=sync_body("revision_conflict",8,true);response(s);free(s);assert(r.stage==H2H_RELAY_ERROR&&p.record.conflict&&p.record.save.hearts==41&&p.record.remote_save.hearts==40);
 setup(true);open("pair",314,UINT32_MAX-1000,"abcdefabcdefabcdefabcdefabcdefab");assert(r.stage==H2H_RELAY_BEGIN);assert(!live(&r,299000)&&r.stage==H2H_RELAY_ERROR);
 setup(true);open("pair",314,0,"abcdefabcdefabcdefabcdefabcdefab");cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"action","deliver");cJSON_AddItemToObject(o,"envelope",envelope("response","begin","{\"code\":\"55555555555555555555555555555555\",\"ownership_epoch\":5}",r.session,"ffffffffffffffffffffffffffffffff"));rpc(o,5);assert(r.stage==H2H_RELAY_ERROR&&writes==0);
 setup(true);pair_to_sync();response("{\"account_id\":314,\"ownership_epoch\":6,\"activated\":true,\"locked\":false,\"save_status\":\"ok\"}");assert(r.stage==H2H_RELAY_SYNC&&p.record.dirty&&!p.record.revision&&r.gate);
 setup(true);assert(h2h_relay_rpc(&r,"{\"action\":\"poll\",\"action\":\"poll\"}",33,output,0)>0);assert(r.stage==H2H_RELAY_ERROR);
 /* cloud-2 retires tickets after disconnect; only a live exact retry resumes. */
 setup(true);open("pair",314,0,"abcdefabcdefabcdefabcdefabcdefab");char pending[2300];strcpy(pending,r.request_json);open("pair",314,30,"abcdefabcdefabcdefabcdefabcdefab");assert(r.stage==H2H_RELAY_BEGIN&&!strcmp(pending,r.request_json)&&r.started==0);
 h2h_relay_disconnect(&r);open("pair",314,40,"abcdefabcdefabcdefabcdefabcdefab");assert(r.stage==H2H_RELAY_ERROR&&!r.active&&!r.request_json[0]&&!r.ack_json[0]);
 open("pair",314,50,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");assert(r.stage==H2H_RELAY_BEGIN&&r.started==50);
 /* Old actor is locked without learning new-owner save or consuming the new epoch. */
 setup(true);p.record.ownership.account_id=314;p.record.ownership.activated=true;p.record.revision=2;
 cJSON *session_rpc=cJSON_CreateObject();cJSON_AddStringToObject(session_rpc,"action","open");cJSON_AddItemToObject(session_rpc,"session",envelope("session","session","{\"account_id\":314,\"mode\":\"sync\",\"ttl\":300,\"ownership_epoch\":6,\"device_activated\":true,\"owner_authorized\":false}","bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","11111111111111111111111111111111"));rpc(session_rpc,0);assert(r.stage==H2H_RELAY_SYNC);
 response("{\"ownership_epoch\":6,\"account_id\":null,\"activated\":false,\"locked\":true,\"save_status\":\"ownership_changed\",\"save\":null}");assert(r.stage==H2H_RELAY_ERROR&&!p.record.ownership.activated&&p.record.ownership.epoch==5&&p.record.revision==2);
 transition_tests();v2_tests();cJSON_Delete(fixtures);puts("Relay HMAC fixtures, physical confirmation, epoch/CAS/ACK, persistence, conflict, timeout and byte lease tests: PASS");return 0;}
