#include "stayc_game_store.h"
#include <string.h>

#define RECORD_MAGIC 0x53544734u
#define RECORD_VERSION 2u
_Static_assert(sizeof(h2h_save_t)==48,"Cloud contract changed");
_Static_assert(sizeof(stayc_game_record_t)==168 && offsetof(stayc_game_record_t,gameplay)==64 &&
               offsetof(stayc_game_record_t,checksum)==160,"Local extension layout changed");
static uint32_t hash(const void *data,size_t size) {
    const uint8_t *p=data;uint32_t h=2166136261u;
    for(size_t i=0;i<size;i++)h=(h^p[i])*16777619u;
    return h;
}
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(i*8));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void encode_words(uint8_t *bytes,const void *fields,unsigned count){
    for(unsigned i=0;i<count;i++){uint32_t word;memcpy(&word,(const uint8_t *)fields+i*4,4);put32(bytes+i*4,word);}
}
static void decode_words(void *fields,const uint8_t *bytes,unsigned count){
    for(unsigned i=0;i<count;i++){uint32_t word=get32(bytes+i*4);memcpy((uint8_t *)fields+i*4,&word,4);}
}
void stayc_game_record_encode(const stayc_game_record_t *record,uint8_t bytes[STAYC_GAME_RECORD_BYTES]){
    put32(bytes,record->magic);put32(bytes+4,record->version);
    put32(bytes+8,(uint32_t)record->epoch);put32(bytes+12,(uint32_t)(record->epoch>>32));
    encode_words(bytes+16,&record->cloud_base,12);encode_words(bytes+64,&record->gameplay,23);
    put32(bytes+156,record->settled_claims);put32(bytes+160,hash(bytes,160));
}
bool stayc_game_record_decode(stayc_game_record_t *record,const void *data,size_t size){
    if(!data||(size!=STAYC_GAME_RECORD_BYTES&&size!=160))return false;
    const uint8_t *bytes=data;
    unsigned version=get32(bytes+4);
    if(get32(bytes)!=RECORD_MAGIC||(size==160?version!=1:version!=RECORD_VERSION)||get32(bytes+size-4)!=hash(bytes,size-4))return false;
    memset(record,0,sizeof(*record));record->magic=get32(bytes);record->version=get32(bytes+4);
    record->epoch=(uint64_t)get32(bytes+8)|(uint64_t)get32(bytes+12)<<32;
    decode_words(&record->cloud_base,bytes+16,12);decode_words(&record->gameplay,bytes+64,23);record->settled_claims=size==160?record->gameplay.journey_claims:get32(bytes+156);record->checksum=get32(bytes+size-4);
    if(record->settled_claims>63||(record->settled_claims&record->gameplay.journey_claims)!=record->gameplay.journey_claims)return false;
    return true;
}
void stayc_game_project(const stayc_game_t *m,h2h_save_t *base) {
    stayc_game_save_t save;stayc_game_save_snapshot(m,&save);
    memcpy(base,&save,offsetof(h2h_save_t,checksum));base->version=1;
    base->checksum=hash(base,offsetof(h2h_save_t,checksum));
}
bool stayc_game_project_save(const stayc_game_save_t *save,h2h_save_t *base) {
    if(!base||!stayc_game_save_valid(save,true))return false;
    memcpy(base,save,offsetof(h2h_save_t,checksum));base->version=1;
    base->checksum=hash(base,offsetof(h2h_save_t,checksum));return true;
}
bool stayc_game_import_base(stayc_game_t *m,const h2h_save_t *base,bool preserve) {
    h2h_model_t legacy;h2h_init(&legacy,m->track_count,m->song_count,1);
    if(!base||!h2h_load(&legacy,base,sizeof(*base)))return false;
    if(!preserve)return stayc_game_load(m,base,sizeof(*base));
    uint32_t settled=m->debug_mode?m->debug_saved_settled:m->settled_claims;
    stayc_game_save_t save;stayc_game_save_snapshot(m,&save);
    memcpy(&save,base,offsetof(h2h_save_t,checksum));save.version=4;
    unsigned cards=0;for(unsigned i=0;i<8;i++)cards+=(save.cards>>i)&1u;
    save.owned_outfits|=(1u<<save.outfit)|(cards>=4?7:cards>=2?3:1);save.owned_rooms|=(1u<<save.room)|(cards>=8?7:cards>=6?3:1);
    save.journey_claims&=stayc_game_save_journey_ready(&save);
    save.checksum=hash(&save,offsetof(stayc_game_save_t,checksum));
    if(!stayc_game_load(m,&save,sizeof(save)))return false;
    m->settled_claims=settled;return true;
}
void stayc_game_store_load(stayc_game_store_t *s,stayc_game_t *m,uint64_t epoch,
                           const h2h_save_t *base,stayc_game_read_t result,const void *bytes,size_t size) {
    memset(s,0,sizeof(*s));
    if(result==STAYC_GAME_READ_MISSING){s->healthy=stayc_game_import_base(m,base,false);s->blocked=!s->healthy;return;}
    if(result!=STAYC_GAME_READ_OK||!bytes||(size!=STAYC_GAME_RECORD_BYTES&&size!=160)){s->blocked=true;return;}
    stayc_game_record_t record;
    if(!stayc_game_record_decode(&record,bytes,size)){s->blocked=true;return;}
    if(!stayc_game_load(m,&record.cloud_base,sizeof(record.cloud_base))||
       !stayc_game_load(m,&record.gameplay,sizeof(record.gameplay))){s->blocked=true;return;}
    if(record.epoch!=epoch){
        /* Old-account extension is never exposed to a new epoch. Replacing a
         * recognized record requires a later guarded write for that epoch. */
        s->healthy=stayc_game_import_base(m,base,false);s->blocked=!s->healthy;return;
    }
    h2h_save_t projected;stayc_game_project(m,&projected);
    if(memcmp(base,&record.cloud_base,sizeof(*base))&&memcmp(base,&projected,sizeof(*base)) &&
       !stayc_game_import_base(m,base,true)){s->blocked=true;return;}
    m->settled_claims|=record.settled_claims;s->settled_claims=m->settled_claims;
    s->persisted=record;s->has_record=s->healthy=true;
}
bool stayc_game_store_write(stayc_game_store_t *s,const stayc_game_save_t *save,uint64_t epoch,
                            const h2h_save_t *base,stayc_game_write_t write,void *context) {
    if(s->blocked||!write||!save||!base||!stayc_game_save_valid(save,true)||base->magic!=0x48324850u||base->version!=1||base->checksum!=hash(base,offsetof(h2h_save_t,checksum)))return false;
    stayc_game_record_t next={.magic=RECORD_MAGIC,.version=RECORD_VERSION,.epoch=epoch,.cloud_base=*base,.gameplay=*save,.settled_claims=s->settled_claims|save->journey_claims};
    uint8_t encoded[STAYC_GAME_RECORD_BYTES];stayc_game_record_encode(&next,encoded);next.checksum=get32(encoded+160);
    if(s->has_record&&s->persisted.magic==next.magic&&s->persisted.version==next.version&&s->persisted.epoch==next.epoch&&s->persisted.settled_claims==next.settled_claims&&
       !memcmp(&s->persisted.cloud_base,&next.cloud_base,sizeof(next.cloud_base))&&!memcmp(&s->persisted.gameplay,&next.gameplay,sizeof(next.gameplay)))return true;
    s->healthy=write(encoded,sizeof(encoded),context);
    if(s->healthy){s->persisted=next;s->settled_claims=next.settled_claims;s->has_record=true;}
    return s->healthy;
}
