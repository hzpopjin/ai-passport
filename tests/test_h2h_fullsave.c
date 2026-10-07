#include "h2h_fullsave.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned allocation_calls,fail_allocation,outstanding_allocations;
void *fullsave_probe_malloc(size_t size){
 assert(size<=4096);
 if(++allocation_calls==fail_allocation)return NULL;
 void *p=malloc(size);if(p)++outstanding_allocations;return p;
}
void fullsave_probe_free(void *p){if(p){assert(outstanding_allocations);--outstanding_allocations;}free(p);}
static uint8_t banks[2][1024],ptr[32];static bool exists[3];static unsigned writes,reads,fail_write,fail_read;
static int read_bank(int bank,void *bytes,size_t length,void *ctx){(void)ctx;if(++reads==fail_read)return -1;if(!exists[bank])return 0;memcpy(bytes,bank==2?ptr:banks[bank],length);return 1;}
static bool write_bank(int bank,const void *bytes,size_t length,void *ctx){(void)ctx;if(++writes==fail_write)return false;memcpy(bank==2?ptr:banks[bank],bytes,length);exists[bank]=true;return true;}
static const h2h_fullsave_io_t io={read_bank,write_bank,NULL};
static h2h_pocket_t p;static h2h_fullsave_t f;static stayc_game_t game;
static uint32_t fnv(const uint8_t *b,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}
static void put32(uint8_t *b,uint32_t n){for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(n>>(8*i));}
static void setup(void){
 assert(!outstanding_allocations);allocation_calls=fail_allocation=0;
 memset(exists,0,sizeof(exists));memset(banks,0,sizeof(banks));memset(ptr,0,sizeof(ptr));writes=reads=fail_write=fail_read=0;
 h2h_pocket_init(&p,12,8);p.record.ownership.epoch=12;p.record.ownership.account_id=7;p.record.ownership.activated=true;p.record.revision=5;
 h2h_fullsave_init(&f);stayc_game_init(&game,12,8,1);game.save.star_balance=100;
}
static stayc_game_save_t snapshot(void){stayc_game_save_t save;stayc_game_save_snapshot(&game,&save);return save;}
static void local(void){stayc_game_save_t save=snapshot();assert(h2h_fullsave_local(&f,&p,&save,game.settled_claims,&io));}
static void reboot(h2h_fullsave_t *out,h2h_pocket_t *owner){h2h_pocket_init(owner,12,8);h2h_fullsave_init(out);reads=fail_read=0;assert(h2h_fullsave_load(out,owner,&io));}
int main(void){
 /* Bounded transaction allocation failure never writes or changes authority. */
 setup();h2h_fullsave_t untouched=f;h2h_pocket_t original=p;
 stayc_game_save_t initial=snapshot();fail_allocation=1;
 assert(!h2h_fullsave_local(&f,&p,&initial,0,&io));
 assert(!writes&&!reads&&!outstanding_allocations&&!memcmp(&f,&untouched,sizeof(f))&&!memcmp(&p,&original,sizeof(p)));
 fail_allocation=0;local();assert(!outstanding_allocations);
 /* Projection preserves paired full/chorus and countdown bits, without a model copy. */
 for(unsigned mode=0;mode<4;mode++){
  setup();p.songs=6;game.song_count=6;game.save.countdown=mode;stayc_game_save_t save=snapshot();h2h_save_t base,expected;
  stayc_game_project(&game,&expected);assert(stayc_game_project_save(&save,&base)&&!memcmp(&base,&expected,sizeof(base)));
  local();assert(h2h_fullsave_valid(f.local,164,12));
  save.checksum^=1;assert(!stayc_game_project_save(&save,&base)&&!memcmp(&base,&expected,sizeof(base)));
 }
 setup();assert(h2h_fullsave_load(&f,&p,&io)&&f.bank==-1);local();assert(f.has_local&&f.dirty&&f.revision==5&&f.bank==0);
 stayc_game_record_t record;assert(stayc_game_record_decode(&record,f.local,164)&&record.gameplay.star_balance==100&&record.epoch==12);
 h2h_fullsave_t recovered;h2h_pocket_t owner;reboot(&recovered,&owner);assert(!memcmp(recovered.local,f.local,164)&&owner.record.ownership.account_id==7);
 uint8_t corrupt[164];memcpy(corrupt,f.local,164);corrupt[136]^=1;assert(!h2h_fullsave_valid(corrupt,164,12));assert(!h2h_fullsave_valid(f.local,164,13));
 /* Legacy160 may seed only a real guarded local record, never the wire. */
 uint8_t legacy[160];memcpy(legacy,f.local,156);put32(legacy+4,1);put32(legacy+156,fnv(legacy,156));
 setup();assert(!h2h_fullsave_valid(legacy,160,12));assert(h2h_fullsave_seed(&f,&p,legacy,160,&io));assert(stayc_game_record_decode(&record,f.local,164)&&record.version==2&&record.gameplay.star_balance==100);
 /* A later v1 CAS receipt becomes the base for initial v2 promotion. */
 uint8_t legacy_owner[384];h2h_pocket_record_t advanced=p.record;advanced.revision=6;
 h2h_pocket_encode(&advanced,legacy_owner);assert(h2h_fullsave_pocket_write(&f,&p,legacy_owner,384,&io)&&f.revision==6&&p.record.revision==6&&f.dirty);
 /* Legacy migration validates the base checksum and honors a basic restore. */
 setup();stayc_game_record_decode(&record,legacy,160);record.version=2;record.cloud_base.checksum^=1;
 stayc_game_record_encode(&record,corrupt);assert(!h2h_fullsave_seed(&f,&p,corrupt,164,&io)&&!writes);
 setup();p.record.save.volume=45;p.record.save.checksum=fnv((uint8_t *)&p.record.save,44);
 assert(h2h_fullsave_seed(&f,&p,legacy,160,&io));assert(stayc_game_record_decode(&record,f.local,164)&&record.gameplay.volume==45&&record.gameplay.star_balance==100);
 for(unsigned failure=1;failure<=3;failure++){
  setup();local();uint8_t old[164];memcpy(old,f.local,164);game.save.star_balance=76;stayc_game_save_t save=snapshot();
  if(failure<=2)fail_write=writes+failure;else fail_read=reads+2;
  assert(!h2h_fullsave_local(&f,&p,&save,0,&io)&&!f.healthy&&!p.healthy&&!memcmp(f.local,old,164));
  reboot(&recovered,&owner);assert(stayc_game_record_decode(&record,recovered.local,164));
  assert(record.gameplay.star_balance==(failure==3?76u:100u));
 }
 /* Torn inactive generation cannot replace the committed pointer. */
 setup();local();banks[1][0]=0xff;exists[1]=true;reboot(&recovered,&owner);assert(recovered.bank==0);
 ptr[31]^=1;reads=0;assert(!h2h_fullsave_load(&recovered,&owner,&io)&&!owner.healthy);
 /* A receipt for immutable older bytes advances CAS, keeps newer wallet dirty. */
 setup();local();uint8_t sent[164];memcpy(sent,f.local,164);game.save.star_balance=101;local();
 assert(h2h_fullsave_reply(&f,&p,sent,5,6,sent,false,false,&io)&&f.revision==6&&f.dirty);
 assert(stayc_game_record_decode(&record,f.local,164)&&record.gameplay.star_balance==101);
 /* Conflict saves both complete records; explicit restore preserves recovery. */
 uint8_t remote[164];memcpy(remote,sent,164);uint64_t generation=p.model_generation;
 assert(h2h_fullsave_reply(&f,&p,f.local,6,7,remote,true,false,&io)&&f.conflict&&p.record.conflict&&!h2h_pocket_can_play(&p));
 uint8_t newer[164];memcpy(newer,f.local,164);assert(h2h_fullsave_restore(&f,&p,&io)&&f.restore_ack&&f.server_full&&p.model_generation>generation);
 assert(!memcmp(f.local,remote,164)&&!memcmp(f.recovery,newer,164));
 assert(h2h_fullsave_reply(&f,&p,f.local,7,7,f.local,false,false,&io)&&!f.restore_ack&&!f.dirty);
 h2h_pocket_record_t old_basic=p.record;old_basic.revision++;h2h_pocket_encode(&old_basic,legacy_owner);
 assert(!h2h_fullsave_pocket_write(&f,&p,legacy_owner,384,&io));
 /* A basic writer cannot replace the full wallet. Owner epoch change clears it. */
 uint8_t basic[384];h2h_pocket_record_t next=p.record;next.save.hearts++;next.save.checksum=fnv((uint8_t *)&next.save,44);h2h_pocket_encode(&next,basic);
 assert(!h2h_fullsave_pocket_write(&f,&p,basic,384,&io));
 next=p.record;next.ownership.epoch=13;next.ownership.account_id=8;h2h_pocket_encode(&next,basic);
 assert(h2h_fullsave_pocket_write(&f,&p,basic,384,&io)&&!f.has_local&&!f.server_full&&p.record.ownership.epoch==13);
 reboot(&recovered,&owner);assert(!recovered.has_local&&owner.record.ownership.account_id==8);
 assert(!outstanding_allocations);
 puts("Full164 journal: allocation failure/no leak, paired projection, actual legacy migration, coherent reboot, inactive-bank/pointer faults, immutable in-flight receipt, full conflict/recovery, downgrade and owner epoch isolation: PASS");
}
