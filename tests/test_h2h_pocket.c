#include "h2h_pocket.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char disk[H2H_POCKET_RECORD_SIZE];
static bool fail_write;
static bool write_record(const unsigned char *bytes,size_t length,void *ctx) {
    (void)ctx; assert(length==sizeof(disk)); if(fail_write) return false; memcpy(disk,bytes,length); return true;
}
static h2h_save_t progress(unsigned hearts) {
    h2h_model_t model; h2h_save_t save; h2h_init(&model,2,1,1); h2h_listened(&model,hearts*120000); h2h_save_snapshot(&model,&save); return save;
}
static void activate(h2h_pocket_t *p) {
    h2h_pocket_record_t sent=p->record; h2h_pocket_reply_t reply={.epoch=1,.account_id=7,.activated=true};
    assert(h2h_pocket_reply(p,&sent,&reply,write_record,NULL)); assert(h2h_pocket_can_play(p));
}
int main(void) {
    h2h_pocket_t p; h2h_pocket_init(&p,2,1); assert(!h2h_pocket_can_play(&p)); activate(&p);
    h2h_save_t local=progress(5); assert(h2h_pocket_local_save(&p,1,&local,write_record,NULL));
    h2h_pocket_t reboot; h2h_pocket_init(&reboot,2,1); assert(h2h_pocket_decode(&reboot,disk,sizeof(disk))); assert(h2h_pocket_can_play(&reboot)); assert(reboot.record.save.hearts==5);
    unsigned char corrupted[H2H_POCKET_RECORD_SIZE]; memcpy(corrupted,disk,sizeof(disk)); corrupted[24]^=1;
    assert(!h2h_pocket_decode(&reboot,corrupted,sizeof(corrupted))); assert(!h2h_pocket_can_play(&reboot));
    assert(!h2h_pocket_decode(&reboot,disk,sizeof(disk)-1));
    const char *op="0123456789abcdef0123456789abcdef";
    h2h_pocket_record_t delayed=p.record;
    assert(h2h_pocket_unbind(&p,op,write_record,NULL)); assert(!h2h_pocket_can_play(&p));
    h2h_pocket_init(&reboot,2,1); assert(h2h_pocket_decode(&reboot,disk,sizeof(disk))); assert(!h2h_pocket_can_play(&reboot)); assert(!strcmp(reboot.record.ownership.pending_unbind,op));
    h2h_pocket_reply_t old={.epoch=1,.account_id=7,.activated=true,.has_save=true,.revision=1,.save=local};
    assert(!h2h_pocket_reply(&p,&delayed,&old,write_record,NULL));
    h2h_pocket_record_t sent=p.record;
    h2h_pocket_reply_t unbound={.epoch=2,.unbind_ack=op};
    assert(h2h_pocket_reply(&p,&sent,&unbound,write_record,NULL)); assert(!p.record.ownership.pending_unbind[0]); assert(p.record.save.hearts==0);
    assert(!h2h_pocket_local_save(&p,1,&local,write_record,NULL));
    sent=p.record; h2h_pocket_reply_t new_owner={.epoch=3,.account_id=8,.activated=true};
    assert(h2h_pocket_reply(&p,&sent,&new_owner,write_record,NULL)); assert(h2h_pocket_can_play(&p)); assert(p.record.save.hearts==0);
    assert(!h2h_pocket_reply(&p,&sent,&old,write_record,NULL));
    h2h_pocket_init(&p,2,1); activate(&p); local=progress(5); assert(h2h_pocket_local_save(&p,1,&local,write_record,NULL));
    sent=p.record; h2h_save_t newer=progress(7); assert(h2h_pocket_local_save(&p,1,&newer,write_record,NULL));
    old.save=local; assert(h2h_pocket_reply(&p,&sent,&old,write_record,NULL)); assert(p.record.dirty && p.record.save.hearts==7 && p.record.revision==1);
    sent=p.record; h2h_pocket_reply_t migration={.epoch=1,.account_id=7,.activated=true,.has_save=true,.revision=2,.save=progress(10),.restore_required=true};
    assert(h2h_pocket_reply(&p,&sent,&migration,write_record,NULL));
    assert(p.record.conflict && p.record.save.hearts==7 && p.record.remote_save.hearts==10 && !p.record.restore_ack);
    assert(!h2h_pocket_can_play(&p));
    /* Protected conflict is preserved across reboot, with both snapshots. */
    h2h_pocket_init(&reboot,2,1);assert(h2h_pocket_decode(&reboot,disk,sizeof(disk)));
    assert(reboot.record.conflict&&reboot.record.save.hearts==7&&reboot.record.remote_save.hearts==10);
    sent=p.record; h2h_pocket_reply_t recovered={.epoch=5,.account_id=9,.activated=true,.has_save=true,.revision=10,.save=progress(15)};
    assert(h2h_pocket_reply(&p,&sent,&recovered,write_record,NULL));
    assert(p.record.save.hearts==15 && p.record.revision==10 && !p.record.dirty && !p.record.recovery);
    fail_write=true; assert(!h2h_pocket_unbind(&p,op,write_record,NULL)); assert(!h2h_pocket_can_play(&p));
    puts("Pocket record/reboot/failure/CAS/migration tests: PASS");
    return 0;
}
