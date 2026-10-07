/* Actual native epoch-local extension: codec, durable-write and restore faults.
 * No cloud48/pocket384 layout changes and no hardware/NVS emulation claims. */
#include "stayc_game_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MAGIC UINT32_C(0x53544734)
static uint8_t disk[STAYC_GAME_RECORD_BYTES];
static unsigned writes;
static bool fail_write;
static uint64_t owner_epoch=1,owner_generation=1;
typedef struct { uint64_t epoch,generation; } guard_t;
static uint32_t hash(const void *bytes,size_t length){
    const uint8_t *p=bytes;uint32_t value=2166136261u;
    for(size_t i=0;i<length;i++)value=(value^p[i])*16777619u;
    return value;
}
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static void fix_hash(uint8_t *bytes,size_t length){put32(bytes+length-4,hash(bytes,length-4));}
static bool writer(const void *bytes,size_t length,void *context){
    const guard_t *guard=context;assert(length==sizeof(disk));
    if(fail_write||guard->epoch!=owner_epoch||guard->generation!=owner_generation)return false;
    memcpy(disk,bytes,length);writes++;return true;
}
static void reset_disk(void){memset(disk,0,sizeof(disk));writes=0;fail_write=false;owner_epoch=owner_generation=1;}
static h2h_save_t progress(unsigned hearts){
    h2h_model_t model;h2h_save_t base;h2h_init(&model,12,6,1);
    h2h_listened(&model,hearts*120000u);h2h_save_snapshot(&model,&base);return base;
}
static void missing(stayc_game_store_t *store,stayc_game_t *model,const h2h_save_t *base){
    stayc_game_init(model,12,6,1);stayc_game_store_load(store,model,1,base,STAYC_GAME_READ_MISSING,NULL,0);
    assert(store->healthy&&!store->blocked&&!store->has_record);
}
static void save(stayc_game_store_t *store,stayc_game_t *model,uint64_t epoch,const h2h_save_t *base){
    stayc_game_save_t snapshot;stayc_game_save_snapshot(model,&snapshot);
    /* Main transfers same-epoch paid tombstones before extension writes. */
    store->settled_claims|=model->settled_claims;
    guard_t guard={epoch,owner_generation};assert(stayc_game_store_write(store,&snapshot,epoch,base,writer,&guard));
}
static void unhex(uint8_t *bytes,const char *text,size_t length){
    for(size_t i=0;i<length;i++){unsigned value;assert(sscanf(text+2*i,"%2x",&value)==1);bytes[i]=(uint8_t)value;}
}

static void test_codec_and_dedup(void){
    reset_disk();h2h_save_t base=progress(0);stayc_game_t model;stayc_game_store_t store;missing(&store,&model,&base);
    stayc_game_save_t game;stayc_game_save_snapshot(&model,&game);
    /* Fixed bytes independently captured from cloud48 and genuine public v15. */
    static const char base_hex[]="50483248010000000000000000000000000000000000000000000000230000000000000000000000080000008db2baf1";
    static const char game_hex[]="504832480400000000000000000000000000000000000000000000002300000000000000000000000800000000000000000000000000000000000000000000000000000000000000000000000100000001000000777700008e8b1fad";
    uint8_t expected[STAYC_GAME_RECORD_BYTES]={0};
    expected[0]=0x34;expected[1]=0x47;expected[2]=0x54;expected[3]=0x53;expected[4]=2;
    for(unsigned i=0;i<8;i++)expected[8+i]=(uint8_t)(UINT64_C(0x0102030405060708)>>(8*i));
    unhex(expected+16,base_hex,48);unhex(expected+64,game_hex,92);fix_hash(expected,sizeof(expected));
    stayc_game_record_t record;memset(&record,0xa5,sizeof(record));
    record.magic=MAGIC;record.version=2;record.epoch=UINT64_C(0x0102030405060708);
    record.cloud_base=base;record.gameplay=game;record.settled_claims=0;
    uint8_t encoded[sizeof(expected)];stayc_game_record_encode(&record,encoded);assert(!memcmp(encoded,expected,sizeof(expected)));
    stayc_game_record_t decoded;assert(stayc_game_record_decode(&decoded,encoded,sizeof(encoded)));
    assert(decoded.epoch==record.epoch&&!memcmp(&decoded.cloud_base,&base,48)&&!memcmp(&decoded.gameplay,&game,92));
    save(&store,&model,1,&base);assert(writes==1);
    /* In-memory padding and cached checksum are excluded from durable dedup. */
    memset((uint8_t *)&store.persisted+164,0xa5,sizeof(store.persisted)-164);store.persisted.checksum^=1;
    save(&store,&model,1,&base);assert(writes==1);
    model.save.star_balance=1;save(&store,&model,1,&base);assert(writes==2);
    assert(sizeof(record)==168&&sizeof(encoded)==164);
    puts("Store codec exact LE bytes, struct padding exclusion and named-field dedup: PASS");
}

static void test_protected_records(void){
    reset_disk();h2h_save_t base=progress(0);stayc_game_t model;stayc_game_store_t store;missing(&store,&model,&base);save(&store,&model,1,&base);
    uint8_t good[sizeof(disk)];memcpy(good,disk,sizeof(good));unsigned before=writes;
    stayc_game_save_t snapshot;stayc_game_save_snapshot(&model,&snapshot);guard_t guard={1,1};
    for(unsigned fault=0;fault<15;fault++){
        uint8_t invalid[sizeof(disk)];memcpy(invalid,good,sizeof(invalid));size_t length=sizeof(invalid);stayc_game_read_t result=STAYC_GAME_READ_OK;
        switch(fault){
            case 0:invalid[8]^=1;break; /* invalid outer checksum */
            case 1:put32(invalid+4,99);fix_hash(invalid,length);break; /* future wrapper */
            case 2:length--;break;
            case 3:result=STAYC_GAME_READ_ERROR;break;
            case 4:put32(invalid+68,5);put32(invalid+152,hash(invalid+64,88));fix_hash(invalid,length);break; /* future raw V4 */
            case 5:invalid[152]^=1;fix_hash(invalid,length);break; /* invalid inner checksum */
            case 6:put32(invalid+156,64);fix_hash(invalid,length);break; /* tombstone out of bounds */
            case 7:put32(invalid+64+60,1);put32(invalid+152,hash(invalid+64,88));fix_hash(invalid,length);break; /* paid mask omits raw claim */
            case 8:put32(invalid,MAGIC+1);fix_hash(invalid,length);break;
            case 9:length=0;break;
            case 10:invalid[60]^=1;fix_hash(invalid,length);break; /* invalid cloud48 checksum */
            case 11:put32(invalid+20,99);put32(invalid+60,hash(invalid+16,44));fix_hash(invalid,length);break;
            case 12:put32(invalid+64+72,1000000);put32(invalid+152,hash(invalid+64,88));fix_hash(invalid,length);break;
            case 13:put32(invalid+64+84,1);put32(invalid+152,hash(invalid+64,88));fix_hash(invalid,length);break;
            case 14:length=160;put32(invalid+4,3);fix_hash(invalid,length);break; /* future 160B wrapper */
        }
        stayc_game_init(&model,12,6,1);stayc_game_store_load(&store,&model,1,&base,result,invalid,length);assert(store.blocked);
        assert(!stayc_game_store_write(&store,&snapshot,1,&base,writer,&guard));assert(writes==before&&!memcmp(disk,good,sizeof(good)));
        /* Future/corrupt data cannot be overwritten just because account changed. */
        stayc_game_init(&model,12,6,1);stayc_game_store_load(&store,&model,2,&base,result,invalid,length);assert(store.blocked);
    }
    assert(!stayc_game_record_decode(&(stayc_game_record_t){0},NULL,164));
    puts("Store corrupt/future/length/read-error/tombstone records preserve committed bytes: PASS");
}

static void test_shop_failures_and_cas(void){
    reset_disk();h2h_save_t base=progress(0);stayc_game_t model;stayc_game_store_t store;missing(&store,&model,&base);model.save.star_balance=100;save(&store,&model,1,&base);
    stayc_game_save_t original;stayc_game_save_snapshot(&model,&original);uint8_t committed[sizeof(disk)];memcpy(committed,disk,sizeof(committed));
    model.page=STAYC_GAME_WARDROBE;
    stayc_game_input(&model,STAYC_GAME_DOWN,STAYC_GAME_PRESS);
    stayc_game_input(&model,STAYC_GAME_OK,STAYC_GAME_PRESS);stayc_game_input(&model,STAYC_GAME_OK,STAYC_GAME_CLICK);
    stayc_game_input(&model,STAYC_GAME_DOWN,STAYC_GAME_PRESS);
    stayc_game_input(&model,STAYC_GAME_OK,STAYC_GAME_PRESS);stayc_game_input(&model,STAYC_GAME_OK,STAYC_GAME_CLICK);
    unsigned generation=model.shop_generation,debug_epoch=model.debug_epoch;stayc_game_save_t candidate;
    assert(stayc_game_shop_prepare(&model,&candidate)==92&&candidate.star_balance==76&&candidate.outfit==1&&candidate.owned_outfits==3);
    assert(stayc_game_shop_validate(&model,&candidate,92,generation,debug_epoch));guard_t guard={1,1};
    fail_write=true;assert(!stayc_game_store_write(&store,&candidate,1,&base,writer,&guard));
    assert(!stayc_game_shop_finish(&model,&candidate,92,0,generation,debug_epoch));
    stayc_game_save_t after;stayc_game_save_snapshot(&model,&after);assert(!memcmp(&after,&original,92)&&!memcmp(disk,committed,sizeof(disk)));
    fail_write=false;guard.generation=0;assert(!stayc_game_store_write(&store,&candidate,1,&base,writer,&guard));
    guard.generation=1;owner_epoch=2;assert(!stayc_game_store_write(&store,&candidate,1,&base,writer,&guard));owner_epoch=1;
    assert(!memcmp(disk,committed,sizeof(disk))&&!memcmp(&store.persisted.gameplay,&original,92));
    assert(!stayc_game_shop_validate(&model,&candidate,92,generation,debug_epoch)); /* failure invalidates stale request */
    save(&store,&model,1,&base);assert(writes==1);
    stayc_game_save_t invalid=original;invalid.version=5;invalid.checksum=hash(&invalid,88);
    assert(!stayc_game_store_write(&store,&invalid,1,&base,writer,&guard));
    invalid=original;invalid.checksum^=1;
    assert(!stayc_game_store_write(&store,&invalid,1,&base,writer,&guard));
    invalid=original;invalid.star_balance=1000000;invalid.checksum=hash(&invalid,88);
    assert(!stayc_game_store_write(&store,&invalid,1,&base,writer,&guard));
    assert(!stayc_game_store_write(&store,NULL,1,&base,writer,&guard));
    assert(writes==1&&!memcmp(disk,committed,sizeof(disk)));
    puts("Store failed commit/stale owner/generation and rejected old shop candidate: PASS");
}

static void test_checkpoints_and_owner_restore(void){
    reset_disk();h2h_save_t base=progress(0);stayc_game_t model;stayc_game_store_t store;missing(&store,&model,&base);
    model.save.star_balance=100;stayc_game_listened(&model,120000);model.save.activity_flags|=4;save(&store,&model,1,&base);
    uint8_t committed[sizeof(disk)];memcpy(committed,disk,sizeof(committed));h2h_save_t projection;stayc_game_project(&model,&projection);
    for(unsigned checkpoint=0;checkpoint<2;checkpoint++){
        stayc_game_t restarted;stayc_game_init(&restarted,12,6,1);stayc_game_store_t restored;
        stayc_game_store_load(&restored,&restarted,1,checkpoint?&projection:&base,STAYC_GAME_READ_OK,committed,sizeof(committed));
        assert(restored.healthy&&!restored.blocked&&restarted.save.star_balance==101&&restarted.save.hearts==1&&(restarted.save.activity_flags&4));
        assert(!restarted.want_play&&!restarted.life_started&&!restarted.reaction_active);
    }
    stayc_game_t new_owner;stayc_game_init(&new_owner,12,6,1);stayc_game_store_t fresh;
    stayc_game_store_load(&fresh,&new_owner,2,&base,STAYC_GAME_READ_OK,committed,sizeof(committed));
    assert(fresh.healthy&&!fresh.has_record&&!new_owner.save.star_balance&&!new_owner.save.activity_flags&&!new_owner.settled_claims&&!fresh.settled_claims);
    assert(!memcmp(disk,committed,sizeof(disk))); /* loading a recognized old owner never writes */
    h2h_save_t remote=progress(40);stayc_game_t restored;stayc_game_init(&restored,12,6,1);stayc_game_store_t extension;
    stayc_game_store_load(&extension,&restored,1,&remote,STAYC_GAME_READ_OK,committed,sizeof(committed));
    assert(extension.healthy&&!extension.blocked&&restored.save.hearts==40&&restored.save.star_balance==101&&(restored.save.activity_flags&4));
    assert(restored.save.owned_outfits==7&&restored.save.owned_rooms==7);
    puts("Store both partial checkpoints, same-owner restore and different-owner isolation: PASS");
}

static void test_paid_claim_restore_reboot_and_migration(void){
    reset_disk();h2h_save_t ready=progress(20),empty=progress(0);stayc_game_t model;stayc_game_store_t store;missing(&store,&model,&ready);
    assert(stayc_game_journey_claim(&model,5));unsigned paid_balance=model.save.star_balance;assert(paid_balance==22&&model.settled_claims==32);save(&store,&model,1,&ready);
    /* Old unreleased 160B v1 record migrates the already-paid raw claims. */
    uint8_t v1[160];memcpy(v1,disk,156);put32(v1+4,1);fix_hash(v1,sizeof(v1));
    stayc_game_record_t migrated;assert(stayc_game_record_decode(&migrated,v1,sizeof(v1))&&migrated.settled_claims==32);
    stayc_game_init(&model,12,6,1);stayc_game_store_load(&store,&model,1,&ready,STAYC_GAME_READ_OK,v1,sizeof(v1));
    assert(store.healthy&&model.settled_claims==32);unsigned before=writes;save(&store,&model,1,&ready);assert(writes==before+1);
    assert(stayc_game_record_decode(&migrated,disk,sizeof(disk))&&migrated.version==2&&migrated.settled_claims==32);
    for(unsigned restore=0;restore<2;restore++){
        /* Two cloud restores remove readiness while local money survives. */
        assert(stayc_game_import_base(&model,&empty,true));assert(model.save.star_balance==paid_balance&&!model.save.journey_claims&&model.settled_claims==32);
        save(&store,&model,1,&empty);
        /* Reboot with low progress must retain tombstone outside raw92. */
        stayc_game_init(&model,12,6,1);stayc_game_store_load(&store,&model,1,&empty,STAYC_GAME_READ_OK,disk,sizeof(disk));
        assert(store.healthy&&model.settled_claims==32&&store.settled_claims==32&&!model.save.journey_claims);
        assert(stayc_game_import_base(&model,&ready,true));assert(!stayc_game_journey_claim(&model,5));
        assert(model.save.star_balance==paid_balance&&model.save.journey_claims==32);save(&store,&model,1,&ready);
    }
    /* DEV clears sandbox paid flags and restores the normal paid tombstone. */
    stayc_game_debug_set(&model,true);assert(model.debug_mode&&!model.settled_claims);
    stayc_game_debug_set(&model,false);assert(!model.debug_mode&&model.settled_claims==32&&model.save.star_balance==paid_balance);
    /* New ownership epoch is independently allowed to earn its first stamp. */
    stayc_game_init(&model,12,6,1);stayc_game_store_load(&store,&model,2,&ready,STAYC_GAME_READ_OK,disk,sizeof(disk));
    assert(store.healthy&&!store.has_record&&!model.settled_claims&&!store.settled_claims&&model.save.star_balance==20);
    assert(stayc_game_journey_claim(&model,5)&&model.save.star_balance==22);owner_epoch=2;save(&store,&model,2,&ready);
    assert(stayc_game_record_decode(&migrated,disk,sizeof(disk))&&migrated.epoch==2&&migrated.settled_claims==32);
    puts("Store v1-to-v2 migration, two restore/regain cycles, reboot, DEV and new-epoch paid claims: PASS");
}

int main(void){
    test_codec_and_dedup();test_protected_records();test_shop_failures_and_cas();
    test_checkpoints_and_owner_restore();test_paid_claim_restore_reboot_and_migration();return 0;
}
