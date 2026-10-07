#include "h2h_fullsave.h"
#include <string.h>
#include <stdlib.h>

#define JOURNAL_MAGIC UINT32_C(0x50464a32)
#define POINTER_MAGIC UINT32_C(0x50465032)
static uint32_t checksum(const uint8_t *p,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void put64(uint8_t *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(8*i));}
static uint32_t get32(const uint8_t *p){uint32_t n=0;for(unsigned i=0;i<4;i++)n|=(uint32_t)p[i]<<(8*i);return n;}
static uint64_t get64(const uint8_t *p){uint64_t n=0;for(unsigned i=0;i<8;i++)n|=(uint64_t)p[i]<<(8*i);return n;}
void h2h_fullsave_init(h2h_fullsave_t *f){memset(f,0,sizeof(*f));f->healthy=true;f->bank=-1;}
bool h2h_fullsave_valid(const void *bytes,size_t length,uint64_t epoch){
    stayc_game_record_t record;h2h_model_t base;
    if(length!=164||!stayc_game_record_decode(&record,bytes,length)||record.version!=2||
       !epoch||epoch>H2H_POCKET_MAX_INTEGER||record.epoch!=epoch||!stayc_game_save_valid(&record.gameplay,true))return false;
    h2h_init(&base,12,6,1);return h2h_load(&base,&record.cloud_base,sizeof(record.cloud_base));
}
static void encode(const h2h_fullsave_t *f,const h2h_pocket_record_t *p,uint8_t out[1024]){
    memset(out,0,1024);put32(out,JOURNAL_MAGIC);put32(out+4,1);put64(out+8,f->generation);
    put64(out+16,f->revision);put64(out+24,f->remote_revision);
    put32(out+32,(f->has_local?1u:0u)|(f->dirty?2u:0u)|(f->conflict?4u:0u)|(f->restore_ack?8u:0u)|
                    (f->has_remote?16u:0u)|(f->has_recovery?32u:0u)|(f->server_full?64u:0u));
    h2h_pocket_encode(p,out+40);memcpy(out+424,f->local,164);memcpy(out+588,f->remote,164);memcpy(out+752,f->recovery,164);
    put32(out+1020,checksum(out,1020));
}
static bool decode(h2h_fullsave_t *f,h2h_pocket_t *p,const uint8_t in[1024]){
    if(get32(in)!=JOURNAL_MAGIC||get32(in+4)!=1||get32(in+1020)!=checksum(in,1020)||get32(in+36))return false;
    for(unsigned i=916;i<1020;i++)if(in[i])return false;
    unsigned flags=get32(in+32);if(flags&~127u)return false;
    h2h_pocket_t owner=*p;if(!h2h_pocket_decode(&owner,in+40,384))return false;
    h2h_fullsave_t n={.generation=get64(in+8),.revision=get64(in+16),.remote_revision=get64(in+24),.healthy=true};
    n.has_local=flags&1;n.dirty=flags&2;n.conflict=flags&4;n.restore_ack=flags&8;
    n.has_remote=flags&16;n.has_recovery=flags&32;n.server_full=flags&64;
    memcpy(n.local,in+424,164);memcpy(n.remote,in+588,164);memcpy(n.recovery,in+752,164);
    uint64_t epoch=owner.record.ownership.epoch;
    if(!n.generation||n.generation>H2H_POCKET_MAX_INTEGER||n.revision>H2H_POCKET_MAX_INTEGER||n.remote_revision>H2H_POCKET_MAX_INTEGER||
       (n.has_local&&!h2h_fullsave_valid(n.local,164,epoch))||
       (n.has_remote&&(!n.remote_revision||!h2h_fullsave_valid(n.remote,164,epoch)))||
       (n.has_recovery&&!h2h_fullsave_valid(n.recovery,164,epoch))||
       ((n.conflict||n.restore_ack)&&!n.has_remote)||(n.dirty&&!n.has_local))return false;
    if(n.has_local){stayc_game_record_t local;stayc_game_record_decode(&local,n.local,164);
        if(memcmp(&local.cloud_base,&owner.record.save,48))return false;}
    *f=n;*p=owner;return true;
}
static bool pointer(const uint8_t b[32],int *bank,uint64_t *generation){
    if(get32(b)!=POINTER_MAGIC||get32(b+4)!=1||get32(b+8)>1||get32(b+12)||get32(b+24)||get32(b+28)!=checksum(b,28))return false;
    *bank=(int)get32(b+8);*generation=get64(b+16);return *generation&&*generation<=H2H_POCKET_MAX_INTEGER;
}
bool h2h_fullsave_load(h2h_fullsave_t *f,h2h_pocket_t *p,const h2h_fullsave_io_t *io){
    uint8_t ptr[32];int result=io->read(2,ptr,32,io->context);h2h_fullsave_init(f);
    if(result==0)return true;
    int bank;uint64_t generation;uint8_t *bytes=malloc(1024);
    bool ok=result==1&&bytes&&pointer(ptr,&bank,&generation)&&io->read(bank,bytes,1024,io->context)==1&&decode(f,p,bytes)&&f->generation==generation;
    free(bytes);if(!ok){f->healthy=false;p->healthy=false;return false;}f->bank=bank;return true;
}
bool h2h_fullsave_commit(h2h_fullsave_t *f,h2h_pocket_t *p,const h2h_fullsave_t *next,
                        const h2h_pocket_record_t *owner,const h2h_fullsave_io_t *io,bool changed){
    if(!f->healthy||!p->healthy||!io||!io->write||!io->read||f->generation>=H2H_POCKET_MAX_INTEGER)return false;
    /* Main autosave and the BLE worker both enter here with live caller frames.
     * Keep transaction copies beside the already bounded readback buffer,
     * rather than adding three large records to the nested task stack. */
    struct workspace { h2h_fullsave_t next,check;h2h_pocket_t owner_check;uint8_t bytes[2048]; };
    _Static_assert(sizeof(struct workspace)<=4096,"Full-save heap workspace exceeds budget");
    struct workspace *w=malloc(sizeof(*w));if(!w)return false;
    w->next=*next;w->next.generation=f->generation+1;w->next.bank=f->bank==0?1:0;w->next.healthy=true;
    w->owner_check=*p;encode(&w->next,owner,w->bytes);
    bool ok=decode(&w->check,&w->owner_check,w->bytes)&&io->write(w->next.bank,w->bytes,1024,io->context)&&
        io->read(w->next.bank,w->bytes+1024,1024,io->context)==1&&!memcmp(w->bytes,w->bytes+1024,1024);
    uint8_t ptr[32]={0},verify[32];put32(ptr,POINTER_MAGIC);put32(ptr+4,1);put32(ptr+8,(uint32_t)w->next.bank);put64(ptr+16,w->next.generation);put32(ptr+28,checksum(ptr,28));
    if(ok)ok=io->write(2,ptr,32,io->context)&&io->read(2,verify,32,io->context)==1&&!memcmp(ptr,verify,32);
    if(ok){*f=w->next;p->record=*owner;if(changed)++p->model_generation;}
    free(w);
    if(!ok){f->healthy=false;p->healthy=false;return false;}
    return true;
}
static bool make_record(const stayc_game_save_t *game,uint64_t epoch,uint32_t settled,uint8_t out[164],h2h_save_t *base){
    if(!stayc_game_save_valid(game,true)||settled>63||(settled&game->journey_claims)!=game->journey_claims)return false;
    if(!stayc_game_project_save(game,base))return false;
    stayc_game_record_t record={.magic=0x53544734u,.version=2,.epoch=epoch,.cloud_base=*base,.gameplay=*game,.settled_claims=settled};
    stayc_game_record_encode(&record,out);return h2h_fullsave_valid(out,164,epoch);
}
bool h2h_fullsave_local(h2h_fullsave_t *f,h2h_pocket_t *p,const stayc_game_save_t *game,uint32_t settled,const h2h_fullsave_io_t *io){
    if(!f->healthy||!h2h_pocket_can_play(p)||f->conflict)return false;
    h2h_fullsave_t n=*f;h2h_pocket_record_t owner=p->record;
    if(f->has_local){stayc_game_record_t old;if(!stayc_game_record_decode(&old,f->local,164))return false;settled|=old.settled_claims;}
    if(!make_record(game,owner.ownership.epoch,settled,n.local,&owner.save))return false;
    if(f->has_local&&!memcmp(n.local,f->local,164))return true;
    n.has_local=n.dirty=true;n.restore_ack=false;
    owner.dirty=owner.dirty||memcmp(&owner.save,&p->record.save,48)!=0;
    if(!f->has_local)n.revision=owner.revision;
    return h2h_fullsave_commit(f,p,&n,&owner,io,false);
}
bool h2h_fullsave_seed(h2h_fullsave_t *f,h2h_pocket_t *p,const void *bytes,size_t length,const h2h_fullsave_io_t *io){
    if(f->has_local)return true;
    stayc_game_record_t r;if(!stayc_game_record_decode(&r,bytes,length)||r.epoch!=p->record.ownership.epoch||
        !h2h_pocket_can_play(p))return false;
    /* Reuse the guarded local migration: validate BOTH inner saves and retain
     * an already-restored basic record while preserving the actual wallet. */
    stayc_game_t model;stayc_game_store_t store;stayc_game_init(&model,p->tracks,p->songs,1);
    stayc_game_store_load(&store,&model,r.epoch,&p->record.save,STAYC_GAME_READ_OK,bytes,length);
    if(!store.healthy||store.blocked)return false;
    stayc_game_save_t game;stayc_game_save_snapshot(&model,&game);
    return h2h_fullsave_local(f,p,&game,model.settled_claims,io);
}
bool h2h_fullsave_pocket_write(h2h_fullsave_t *f,h2h_pocket_t *p,const void *bytes,size_t length,const h2h_fullsave_io_t *io){
    h2h_pocket_t decoded=*p;if(!h2h_pocket_decode(&decoded,bytes,length))return false;
    h2h_fullsave_t n=*f;bool new_owner=decoded.record.ownership.epoch!=p->record.ownership.epoch||
        decoded.record.ownership.account_id!=p->record.ownership.account_id;
    if(!new_owner){
        if(decoded.record.revision<p->record.revision)return false;
        if(f->server_full&&decoded.record.revision!=p->record.revision)return false;
        /* Promotion shares the current basic CAS lineage. A legacy sync may
         * advance it after the extension was first journaled. */
        if(!f->server_full)n.revision=decoded.record.revision;
    }
    if(new_owner){h2h_fullsave_init(&n);}else if(f->has_local&&memcmp(&decoded.record.save,&p->record.save,48)){
        if(f->server_full)return false; /* A legacy writer cannot discard full cloud progress. */
        stayc_game_record_t old;stayc_game_t model;stayc_game_init(&model,12,8,1);
        if(!stayc_game_record_decode(&old,f->local,164)||!stayc_game_load(&model,&old.gameplay,92)||
           !stayc_game_import_base(&model,&decoded.record.save,true))return false;
        stayc_game_save_t game;stayc_game_save_snapshot(&model,&game);
        h2h_save_t projected;if(!make_record(&game,old.epoch,old.settled_claims,n.local,&projected)||memcmp(&projected,&decoded.record.save,48))return false;
        n.dirty=true;
    }
    return h2h_fullsave_commit(f,p,&n,&decoded.record,io,false);
}
bool h2h_fullsave_reply(h2h_fullsave_t *f,h2h_pocket_t *p,const uint8_t sent[164],uint64_t sent_revision,
                       uint64_t revision,const uint8_t remote[164],bool conflict,bool restore,const h2h_fullsave_io_t *io){
    if(!f->healthy||!h2h_pocket_can_play(p)||p->record.ownership.pending_unbind[0]||
       !revision||revision>H2H_POCKET_MAX_INTEGER||revision<f->revision||sent_revision!=f->revision||
       !h2h_fullsave_valid(remote,164,p->record.ownership.epoch))return false;
    h2h_fullsave_t n=*f;h2h_pocket_record_t owner=p->record;bool changed=false;n.server_full=true;
    if(f->has_local&&!memcmp(sent,remote,164)&&!conflict){
        n.revision=revision;n.dirty=memcmp(f->local,sent,164)!=0;n.restore_ack=false;n.server_full=true;
        owner.revision=revision;owner.dirty=n.dirty;
    }else if(f->has_local&&(conflict||f->dirty||memcmp(f->local,sent,164))){
        memcpy(n.remote,remote,164);n.has_remote=n.conflict=true;n.remote_revision=revision;
        stayc_game_record_t record;stayc_game_record_decode(&record,remote,164);
        owner.conflict=true;owner.remote_save=record.cloud_base;owner.remote_revision=revision;
    }else{
        if(f->has_local){memcpy(n.recovery,f->local,164);n.has_recovery=true;owner.recovery_save=owner.save;owner.recovery=true;}
        memcpy(n.local,remote,164);n.has_local=true;n.dirty=n.conflict=false;n.restore_ack=restore;n.server_full=true;n.revision=revision;
        if(restore){memcpy(n.remote,remote,164);n.has_remote=true;n.remote_revision=revision;}
        stayc_game_record_t record;stayc_game_record_decode(&record,remote,164);
        owner.save=record.cloud_base;owner.revision=revision;owner.dirty=owner.conflict=false;changed=true;
    }
    return h2h_fullsave_commit(f,p,&n,&owner,io,changed);
}
bool h2h_fullsave_restore(h2h_fullsave_t *f,h2h_pocket_t *p,const h2h_fullsave_io_t *io){
    if(!f->healthy||!p->healthy||!f->conflict||!f->has_remote||p->record.ownership.pending_unbind[0]||
       !h2h_fullsave_valid(f->remote,164,p->record.ownership.epoch))return false;
    h2h_fullsave_t n=*f;h2h_pocket_record_t owner=p->record;
    memcpy(n.recovery,n.local,164);n.has_recovery=n.has_local;memcpy(n.local,n.remote,164);
    n.has_local=true;n.revision=n.remote_revision;n.dirty=n.conflict=false;n.restore_ack=n.server_full=true;
    stayc_game_record_t record;stayc_game_record_decode(&record,n.local,164);
    owner.recovery_save=owner.save;owner.recovery=true;owner.save=record.cloud_base;owner.revision=n.revision;
    owner.dirty=owner.conflict=false;return h2h_fullsave_commit(f,p,&n,&owner,io,true);
}
