#include "h2h_phone.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void encode64(const uint8_t b[164],char out[221]){
    for(unsigned i=0,j=0;i<164;i+=3,j+=4){unsigned v=(unsigned)b[i]<<16;if(i+1<164)v|=(unsigned)b[i+1]<<8;if(i+2<164)v|=b[i+2];
        out[j]=alphabet[v>>18];out[j+1]=alphabet[(v>>12)&63];out[j+2]=i+1<164?alphabet[(v>>6)&63]:'=';out[j+3]=i+2<164?alphabet[v&63]:'=';}
    out[220]=0;
}
static bool decode64(const char *s,uint8_t out[164]){
    if(!s||strlen(s)!=220||s[219]!='=')return false;
    size_t n=0;for(unsigned i=0;i<220;i+=4){unsigned v=0;for(unsigned j=0;j<4;j++){
        const char *p=s[i+j]=='='?NULL:strchr(alphabet,s[i+j]);if(!p&&!(i==216&&j==3&&s[i+j]=='='))return false;
        v=(v<<6)|(p?(unsigned)(p-alphabet):0);}
        out[n++]=(uint8_t)(v>>16);out[n++]=(uint8_t)(v>>8);if(i!=216)out[n++]=(uint8_t)v;}
    char canonical[221];encode64(out,canonical);return n==164&&!strcmp(s,canonical);
}
static const char *text(const cJSON *o,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(v)?v->valuestring:NULL;}
static bool integer(const cJSON *o,const char *key,uint64_t *out){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>9007199254740991.0||floor(v->valuedouble)!=v->valuedouble)return false;
    *out=(uint64_t)v->valuedouble;return true;
}
static bool hex(const char *s,size_t n){if(!s||strlen(s)!=n)return false;for(size_t i=0;i<n;i++)if(!strchr("0123456789abcdef",s[i]))return false;return true;}
static bool unique(const cJSON *o){if(!cJSON_IsObject(o))return false;for(const cJSON *a=o->child;a;a=a->next){if(!a->string)return false;for(const cJSON *b=a->next;b;b=b->next)if(!strcmp(a->string,b->string))return false;}return true;}
static bool hash(h2h_phone_t *p,const uint8_t bytes[164],char out[65]){uint8_t b[32];if(!p->sha||!p->sha(bytes,164,b))return false;for(unsigned i=0;i<32;i++){out[2*i]="0123456789abcdef"[b[i]>>4];out[2*i+1]="0123456789abcdef"[b[i]&15];}out[64]=0;return true;}
static cJSON *snapshot(h2h_phone_t *p){if(!p->full->has_local)return cJSON_CreateNull();char data[221],sha[65];if(!hash(p,p->full->local,sha))return NULL;
    encode64(p->full->local,data);cJSON *o=cJSON_CreateObject();cJSON_AddNumberToObject(o,"schema",4);cJSON_AddNumberToObject(o,"record_version",2);cJSON_AddStringToObject(o,"data",data);cJSON_AddStringToObject(o,"sha256",sha);return o;
}
static bool profile_owner(h2h_phone_t *p){return h2h_pocket_can_play(p->pocket)&&!p->pocket->record.ownership.pending_unbind[0];}
static cJSON *profile_status(h2h_phone_t *p){
    cJSON *o=cJSON_CreateObject();pocket_profile_t *s=p->profile;
    cJSON_AddStringToObject(o,"protocol",POCKET_PROFILE_PROTOCOL);cJSON_AddBoolToObject(o,"enabled",s&&s->enabled);
    cJSON_AddStringToObject(o,"format","rgb565-le");cJSON_AddNumberToObject(o,"width",64);cJSON_AddNumberToObject(o,"height",64);cJSON_AddNumberToObject(o,"bytes",8192);
    bool owned=profile_owner(p)&&pocket_profile_owned(s,p->pocket->record.ownership.account_id,p->pocket->record.ownership.epoch);
    bool pending=s&&profile_owner(p)&&s->pending_bank>=0&&s->pending.account==p->pocket->record.ownership.account_id&&s->pending.epoch==p->pocket->record.ownership.epoch;
    cJSON_AddStringToObject(o,"sha256",owned?s->current.sha256:"");cJSON_AddNumberToObject(o,"revision",owned?(double)s->revision:0);
    cJSON_AddStringToObject(o,"upload_sha256",pending?s->pending.sha256:"");cJSON_AddNumberToObject(o,"next_offset",pending?s->next:0);
    cJSON_AddStringToObject(o,"username",owned?s->current.username:"");cJSON_AddStringToObject(o,"uid",owned?s->current.uid:"");
    cJSON_AddStringToObject(o,"points_status",owned&&s->current.points_available?"fresh":"unavailable");
    if(owned&&s->current.points_available)cJSON_AddNumberToObject(o,"points",s->current.points);else cJSON_AddNullToObject(o,"points");return o;
}
static const char *profile_rpc(h2h_phone_t *p,const cJSON *rpc,const char *action){
    uint64_t epoch,account;const cJSON *v=cJSON_GetObjectItemCaseSensitive(rpc,"profile");
    if(!p->profile||!p->profile->enabled)return "profile_unavailable";
    if(!integer(rpc,"expected_epoch",&epoch)||epoch!=p->pocket->record.ownership.epoch||!profile_owner(p))return "profile_owner_changed";
    if(!unique(v)||!integer(v,"account_id",&account)||account!=p->pocket->record.ownership.account_id||!hex(text(v,"sha256"),64))return "profile_invalid_fields";
    if(!strcmp(action,"profile_begin")){
        uint64_t width,height,bytes,points=0;
        const char *name=text(v,"username"),*uid=text(v,"uid"),*status=text(v,"points_status");
        const cJSON *point=cJSON_GetObjectItemCaseSensitive(v,"points");
        if(cJSON_GetArraySize(v)!=10||!name||strlen(name)>63||!uid||strlen(uid)>31||!text(v,"format")||strcmp(text(v,"format"),"rgb565-le")||
           !integer(v,"width",&width)||width!=64||!integer(v,"height",&height)||height!=64||!integer(v,"bytes",&bytes)||bytes!=8192||!status||
           (strcmp(status,"fresh")&&strcmp(status,"unavailable"))||
           (!strcmp(status,"fresh")?(!integer(v,"points",&points)||points>UINT32_MAX):!cJSON_IsNull(point)))return "profile_invalid_fields";
        pocket_profile_meta_t m={.epoch=epoch,.account=(uint32_t)account,.points=(uint32_t)points,.points_available=!strcmp(status,"fresh")};
        strcpy(m.username,name);strcpy(m.uid,uid);strcpy(m.sha256,text(v,"sha256"));
        if(!pocket_profile_meta_valid(&m))return "profile_invalid_fields";
        return pocket_profile_begin(p->profile,&m)?NULL:"profile_storage_failed";
    }
    if(p->profile->pending_bank>=0&&(p->profile->pending.account!=account||p->profile->pending.epoch!=epoch))return "profile_owner_changed";
    if(!strcmp(action,"profile_chunk")){
        uint64_t offset;const char *data=text(v,"data");
        if(cJSON_GetArraySize(v)!=4||!integer(v,"offset",&offset)||offset>7680||!hex(data,1024))return "profile_invalid_fields";
        uint8_t b[512];for(unsigned i=0;i<512;i++){unsigned a=(unsigned)(data[2*i]<='9'?data[2*i]-'0':data[2*i]-'a'+10),n=(unsigned)(data[2*i+1]<='9'?data[2*i+1]-'0':data[2*i+1]-'a'+10);b[i]=(uint8_t)(a*16+n);}
        return pocket_profile_chunk(p->profile,text(v,"sha256"),(uint32_t)offset,b,512)?NULL:"profile_chunk_unconfirmed";
    }
    if(!strcmp(action,"profile_commit")){
        if(cJSON_GetArraySize(v)!=2)return "profile_invalid_fields";
        if(p->profile->pending_bank<0&&!pocket_profile_owned(p->profile,(uint32_t)account,epoch))return "profile_owner_changed";
        return pocket_profile_commit(p->profile,text(v,"sha256"))?NULL:"profile_commit_unconfirmed";
    }
    return "phone_unknown_action";
}
static cJSON *status(h2h_phone_t *p,const char *request,const char *code){cJSON *o=cJSON_CreateObject();
    cJSON_AddStringToObject(o,"protocol",H2H_PHONE_PROTOCOL);cJSON_AddStringToObject(o,"request_id",request?request:"");cJSON_AddStringToObject(o,"device_id",p->device);
    cJSON_AddStringToObject(o,"state",code?"error":"ok");if(code)cJSON_AddStringToObject(o,"code",code);
    cJSON_AddNumberToObject(o,"ownership_epoch",(double)p->pocket->record.ownership.epoch);
    cJSON_AddNumberToObject(o,"account_id",p->pocket->record.ownership.account_id);
    cJSON_AddBoolToObject(o,"activated",p->pocket->record.ownership.activated);
    cJSON_AddNumberToObject(o,"save_generation",(double)p->full->generation);
    cJSON_AddNumberToObject(o,"save_revision",(double)p->full->revision);
    cJSON_AddBoolToObject(o,"dirty",p->full->dirty);cJSON_AddBoolToObject(o,"restore_ack",p->full->restore_ack);
    cJSON_AddBoolToObject(o,"conflict",p->full->conflict);
    cJSON_AddStringToObject(o,"pending_unbind",p->pocket->record.ownership.pending_unbind);
    cJSON_AddNumberToObject(o,"pending_epoch",(double)p->pocket->record.ownership.pending_epoch);
    cJSON_AddBoolToObject(o,"durable",p->pocket->healthy&&p->full->healthy);
    cJSON_AddItemToObject(o,"save",snapshot(p));
    cJSON_AddNumberToObject(o,"resource_capacity_bytes",0);cJSON_AddItemToObject(o,"profile",profile_status(p));return o;
}
static bool make_local(h2h_fullsave_t *n,h2h_pocket_record_t *owner,const h2h_save_t *base){
    stayc_game_t game;stayc_game_init(&game,12,8,1);if(base&&!stayc_game_import_base(&game,base,true))return false;
    stayc_game_record_t record={.magic=0x53544734u,.version=2,.epoch=owner->ownership.epoch};
    stayc_game_save_snapshot(&game,&record.gameplay);stayc_game_project(&game,&record.cloud_base);
    stayc_game_record_encode(&record,n->local);owner->save=record.cloud_base;n->has_local=n->dirty=true;
    return h2h_fullsave_valid(n->local,164,owner->ownership.epoch);
}
static const char *apply(h2h_phone_t *p,const cJSON *rpc){uint64_t generation,epoch,account,revision;const cJSON *a=cJSON_GetObjectItemCaseSensitive(rpc,"authority");
    if(!integer(rpc,"expected_generation",&generation)||!integer(rpc,"expected_epoch",&epoch)||!unique(a)||
       !text(a,"protocol")||strcmp(text(a,"protocol"),H2H_PHONE_PROTOCOL)||!text(a,"device_id")||strcmp(text(a,"device_id"),p->device)||
       !integer(a,"ownership_epoch",&epoch)||!integer(a,"account_id",&account)||account>UINT32_MAX||!account||!epoch||
       !integer(a,"save_revision",&revision)||!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a,"activated")))return "phone_invalid_authority";
    uint64_t expected;if(!integer(rpc,"expected_epoch",&expected))return "phone_invalid_fields";
    if(expected!=p->pocket->record.ownership.epoch||generation!=p->full->generation)return "phone_local_changed";
    if(p->pocket->record.ownership.pending_unbind[0])return "phone_unbind_pending";
    if(epoch<p->pocket->record.ownership.epoch)return "phone_epoch_rollback";
    if(epoch==p->pocket->record.ownership.epoch&&p->pocket->record.ownership.account_id&&account!=p->pocket->record.ownership.account_id)return "phone_owner_mismatch";
    const cJSON *save=cJSON_GetObjectItemCaseSensitive(a,"save");bool has_save=!cJSON_IsNull(save);uint8_t remote[164];
    if(has_save){uint64_t schema,version;char sha[65];if(!unique(save)||!integer(save,"schema",&schema)||schema!=4||!integer(save,"record_version",&version)||version!=2||
        !hex(text(save,"sha256"),64)||!decode64(text(save,"data"),remote)||!h2h_fullsave_valid(remote,164,epoch)||!hash(p,remote,sha)||strcmp(sha,text(save,"sha256"))||!revision)return "phone_invalid_save";}
    if(epoch!=p->pocket->record.ownership.epoch||!p->pocket->record.ownership.activated){
        h2h_pocket_t fresh;h2h_pocket_init(&fresh,p->pocket->tracks,p->pocket->songs);
        fresh.record.ownership=(h2h_ownership_t){.epoch=epoch,.account_id=(uint32_t)account,.activated=true};
        fresh.record.revision=revision;h2h_fullsave_t n;h2h_fullsave_init(&n);n.revision=revision;
        if(has_save){memcpy(n.local,remote,164);n.has_local=n.server_full=true;stayc_game_record_t record;stayc_game_record_decode(&record,remote,164);fresh.record.save=record.cloud_base;}
        else if(!make_local(&n,&fresh.record,NULL))return "phone_invalid_default";
        fresh.record.dirty=n.dirty;
        if(!h2h_fullsave_commit(p->full,p->pocket,&n,&fresh.record,p->io,true))return "phone_storage_failed";
        return NULL;
    }
    if(p->full->conflict)return "phone_save_conflict";
    if(!p->full->has_local&&!has_save){h2h_fullsave_t n=*p->full;h2h_pocket_record_t owner=p->pocket->record;
        if(!make_local(&n,&owner,&owner.save)||!h2h_fullsave_commit(p->full,p->pocket,&n,&owner,p->io,true))return "phone_storage_failed";}
    if(has_save){const char *s=text(a,"save_status");bool conflict=s&&!strcmp(s,"revision_conflict");bool restore=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a,"restore_required"));
        if(!h2h_fullsave_reply(p->full,p->pocket,p->full->local,p->full->revision,revision,remote,conflict,restore,p->io))return "phone_storage_failed";
        if(p->full->conflict)return "phone_save_conflict";
    }else if(revision!=p->full->revision){return "phone_revision_mismatch";}
    return NULL;
}
static const char *unbind(h2h_phone_t *p,const cJSON *rpc){
    uint64_t generation,expected,epoch;const cJSON *a=cJSON_GetObjectItemCaseSensitive(rpc,"authority");
    if(!integer(rpc,"expected_generation",&generation)||generation!=p->full->generation||
       !integer(rpc,"expected_epoch",&expected)||expected!=p->pocket->record.ownership.epoch)return "phone_local_changed";
    if(!unique(a)||!text(a,"protocol")||strcmp(text(a,"protocol"),H2H_PHONE_PROTOCOL)||
       !text(a,"device_id")||strcmp(text(a,"device_id"),p->device)||!hex(text(a,"operation_id"),32)||
       !integer(a,"expected_epoch",&expected)||expected!=p->pocket->record.ownership.pending_epoch||
       !integer(a,"ownership_epoch",&epoch)||epoch!=expected+1||!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a,"acknowledged"))||
       strcmp(text(a,"operation_id"),p->pocket->record.ownership.pending_unbind))return "phone_invalid_unbind";
    h2h_pocket_t fresh;h2h_pocket_init(&fresh,p->pocket->tracks,p->pocket->songs);fresh.record.ownership.epoch=epoch;
    h2h_fullsave_t n;h2h_fullsave_init(&n);
    return h2h_fullsave_commit(p->full,p->pocket,&n,&fresh.record,p->io,true)?NULL:"phone_storage_failed";
}
size_t h2h_phone_rpc(h2h_phone_t *p,const char *input,size_t length,char out[H2H_PHONE_RPC_MAX+1]){
    if(!p||!p->pocket||!p->full||!p->io||!hex(p->device,32)||!input||!length||length>H2H_PHONE_RPC_MAX)return 0;
    char *copy=malloc(length+1);if(!copy)return 0;memcpy(copy,input,length);copy[length]=0;
    bool embedded_nul=false;for(size_t i=0;i+5<length;i++)if(copy[i]=='\\'){if(!memcmp(copy+i+1,"u0000",5))embedded_nul=true;i++;}
    const char *end=NULL;cJSON *rpc=cJSON_ParseWithLengthOpts(copy,length+1,&end,true);free(copy);const char *request=rpc?text(rpc,"request_id"):NULL;
    const char *code=NULL,*action=rpc?text(rpc,"action"):NULL;
    if(!unique(rpc)||!hex(request,32)||!text(rpc,"protocol")||strcmp(text(rpc,"protocol"),H2H_PHONE_PROTOCOL)||!action)code="phone_invalid_fields";
    else if(cJSON_GetArraySize(rpc)!=(!strncmp(action,"profile_",8)?5:(!strcmp(action,"phone_apply")||!strcmp(action,"phone_unbind"))?6:3))code="phone_invalid_fields";
    else if(!strncmp(action,"profile_",8)&&embedded_nul)code="profile_invalid_fields";
    else if(!p->pocket->healthy||!p->full->healthy)code="phone_storage_locked";
    else if(!strcmp(action,"phone_apply"))code=apply(p,rpc);
    else if(!strcmp(action,"phone_unbind"))code=unbind(p,rpc);
    else if(!strncmp(action,"profile_",8))code=profile_rpc(p,rpc,action);
    else if(strcmp(action,"phone_status"))code="phone_unknown_action";
    cJSON *reply=status(p,request,code);char *bytes=cJSON_PrintUnformatted(reply);size_t n=bytes?strlen(bytes):0;if(n>H2H_PHONE_RPC_MAX)n=0;
    if(n) { memcpy(out,bytes,n+1); }
    cJSON_free(bytes);cJSON_Delete(reply);cJSON_Delete(rpc);return n;
}
