#include "h2h_pocket.h"
#include <string.h>

#define MAGIC UINT32_C(0x32524b50)
#define VERSION 2u
_Static_assert(sizeof(h2h_save_t)==48, "Update wire codec for new save schema");
static void put32(unsigned char *p, uint32_t value) { for(unsigned i=0;i<4;i++) p[i]=(unsigned char)(value>>(8*i)); }
static void put64(unsigned char *p, uint64_t value) { for(unsigned i=0;i<8;i++) p[i]=(unsigned char)(value>>(8*i)); }
static uint32_t get32(const unsigned char *p) { uint32_t value=0; for(unsigned i=0;i<4;i++) value|=(uint32_t)p[i]<<(8*i); return value; }
static uint64_t get64(const unsigned char *p) { uint64_t value=0; for(unsigned i=0;i<8;i++) value|=(uint64_t)p[i]<<(8*i); return value; }
static uint32_t checksum(const unsigned char *p, size_t size) { uint32_t v=2166136261u; for(size_t i=0;i<size;i++) v=(v^p[i])*16777619u; return v; }
static void save_encode(const h2h_save_t *s, unsigned char *out) {
    uint32_t fields[12]; memcpy(fields,s,sizeof(fields)); for(unsigned i=0;i<12;i++) put32(out+4*i,fields[i]);
}
static void save_decode(h2h_save_t *s, const unsigned char *in) {
    uint32_t fields[12]; for(unsigned i=0;i<12;i++) fields[i]=get32(in+4*i); memcpy(s,fields,sizeof(fields));
}
static h2h_save_t defaults(const h2h_pocket_t *p) { h2h_model_t model; h2h_save_t save; h2h_init(&model,p->tracks,p->songs,1); h2h_save_snapshot(&model,&save); return save; }
static bool valid_save(const h2h_pocket_t *p, const h2h_save_t *s) { h2h_model_t model; h2h_init(&model,p->tracks,p->songs,1); return h2h_load(&model,s,sizeof(*s)); }
static bool valid_id(const char *id) {
    for(unsigned i=0;i<32;i++) if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f'))) return false;
    return id[32]==0;
}
void h2h_pocket_init(h2h_pocket_t *p, unsigned tracks, unsigned songs) {
    memset(p,0,sizeof(*p)); p->tracks=tracks; p->songs=songs; p->record.save=defaults(p); p->model_generation=1; p->healthy=true;
}
void h2h_pocket_encode(const h2h_pocket_record_t *r, unsigned char out[H2H_POCKET_RECORD_SIZE]) {
    memset(out,0,H2H_POCKET_RECORD_SIZE); put32(out,MAGIC); put32(out+4,VERSION);
    put64(out+8,r->ownership.epoch); put64(out+16,r->ownership.pending_epoch); put64(out+24,r->revision);
    put32(out+32,r->ownership.account_id);
    uint32_t flags=(r->ownership.activated?1u:0u)|(r->ownership.reset_required?2u:0u)|(r->dirty?4u:0u)|
                   (r->conflict?8u:0u)|(r->restore_ack?16u:0u)|(r->recovery?32u:0u);
    put32(out+36,flags); memcpy(out+40,r->ownership.pending_unbind,33); put64(out+80,r->remote_revision);
    save_encode(&r->save,out+88); save_encode(&r->remote_save,out+140); save_encode(&r->recovery_save,out+192);
    put32(out+380,checksum(out,380));
}
bool h2h_pocket_decode(h2h_pocket_t *p, const void *bytes, size_t length) {
    p->healthy=false;
    if(!bytes || length!=H2H_POCKET_RECORD_SIZE) return false;
    const unsigned char *in=bytes;
    if(get32(in)!=MAGIC || get32(in+4)!=VERSION || get32(in+380)!=checksum(in,380)) return false;
    h2h_pocket_record_t r={0}; uint32_t flags=get32(in+36);
    if(flags&~63u) return false;
    r.ownership.epoch=get64(in+8); r.ownership.pending_epoch=get64(in+16); r.revision=get64(in+24);
    r.ownership.account_id=get32(in+32); memcpy(r.ownership.pending_unbind,in+40,33); r.remote_revision=get64(in+80);
    r.ownership.activated=(flags&1)!=0; r.ownership.reset_required=(flags&2)!=0;
    r.dirty=(flags&4)!=0; r.conflict=(flags&8)!=0; r.restore_ack=(flags&16)!=0; r.recovery=(flags&32)!=0;
    save_decode(&r.save,in+88); save_decode(&r.remote_save,in+140); save_decode(&r.recovery_save,in+192);
    if(r.ownership.epoch>H2H_POCKET_MAX_INTEGER || r.revision>H2H_POCKET_MAX_INTEGER || r.remote_revision>H2H_POCKET_MAX_INTEGER ||
       (r.ownership.activated && (!r.ownership.epoch || !r.ownership.account_id)) ||
       (r.ownership.pending_unbind[0] && (!valid_id(r.ownership.pending_unbind)||!r.ownership.pending_epoch||r.ownership.pending_epoch>r.ownership.epoch)) ||
       (!r.ownership.pending_unbind[0] && r.ownership.pending_epoch) || !valid_save(p,&r.save) ||
       ((r.conflict || r.restore_ack) && (!r.remote_revision || !valid_save(p,&r.remote_save))) || (r.recovery && !valid_save(p,&r.recovery_save))) return false;
    p->record=r; p->healthy=true; ++p->model_generation; return true;
}
static bool persist(h2h_pocket_t *p, const h2h_pocket_record_t *next, bool model_changed, h2h_pocket_write_fn write, void *ctx) {
    unsigned char bytes[H2H_POCKET_RECORD_SIZE], current[H2H_POCKET_RECORD_SIZE]; h2h_pocket_encode(next,bytes);
    h2h_pocket_encode(&p->record,current);
    if(p->healthy && memcmp(bytes,current,sizeof(bytes))==0) return true;
    // A failed commit is never reported as accepted, and locks this runtime.
    if(!write || !write(bytes,sizeof(bytes),ctx)) { p->healthy=false; return false; }
    p->record=*next; if(model_changed) ++p->model_generation; return true;
}
bool h2h_pocket_can_play(const h2h_pocket_t *p) { return p && p->healthy && !p->record.conflict && h2h_ownership_can_play(&p->record.ownership); }
bool h2h_pocket_local_save(h2h_pocket_t *p, uint64_t epoch, const h2h_save_t *save, h2h_pocket_write_fn write, void *ctx) {
    if(!h2h_pocket_can_play(p) || epoch!=p->record.ownership.epoch || !save || !valid_save(p,save)) return false;
    if(memcmp(save,&p->record.save,sizeof(*save))==0) return true;
    h2h_pocket_record_t next=p->record; next.save=*save; next.dirty=true;
    return persist(p,&next,false,write,ctx);
}
bool h2h_pocket_unbind(h2h_pocket_t *p, const char operation[33], h2h_pocket_write_fn write, void *ctx) {
    if(!p->healthy) return false;
    h2h_pocket_record_t next=p->record;
    if(!h2h_ownership_unbind(&next.ownership,operation)) return false;
    return persist(p,&next,false,write,ctx);
}
bool h2h_pocket_reply(h2h_pocket_t *p, const h2h_pocket_record_t *sent, const h2h_pocket_reply_t *r, h2h_pocket_write_fn write, void *ctx) {
    if(!p->healthy || !sent || !r || sent->ownership.epoch!=p->record.ownership.epoch ||
       strcmp(sent->ownership.pending_unbind,p->record.ownership.pending_unbind)!=0 || r->revision>H2H_POCKET_MAX_INTEGER) return false;
    h2h_pocket_record_t next=p->record;
    uint64_t old_epoch=next.ownership.epoch;
    const char *settled=r->unbind_ack?r->unbind_ack:r->unbind_rejected;
    if(!h2h_ownership_sync(&next.ownership,r->epoch,r->account_id,r->activated,settled)) return false;
    bool changed=r->epoch!=old_epoch;
    if(changed) {
        next.save=defaults(p); next.revision=0; next.dirty=false;
        // Only the current server-owned period can supply a replacement. The
        // previous period is discarded before any new owner's data is loaded.
        if(r->activated && r->has_save) {
            if(!r->revision || !valid_save(p,&r->save)) return false;
            next.save=r->save; next.revision=r->revision;
        }
        next.conflict=false; next.restore_ack=false; next.recovery=false; next.remote_revision=0;
        memset(&next.remote_save,0,sizeof(next.remote_save)); memset(&next.recovery_save,0,sizeof(next.recovery_save));
        if(r->restore_required && r->activated && r->has_save) {
            next.restore_ack=true; next.remote_save=r->save; next.remote_revision=r->revision;
        }
        // Reset and acknowledgment are persisted in the SAME record as ownership.
        if(!next.ownership.pending_unbind[0]) (void)h2h_ownership_reset_done(&next.ownership,r->epoch);
    }
    if(!changed && r->activated && !next.ownership.pending_unbind[0]) {
        if(r->has_save && (r->revision<next.revision || !r->revision || !valid_save(p,&r->save))) return false;
        if(r->restore_required && r->has_save) {
            if(r->revision<next.revision) return false;
            if(!(next.restore_ack && r->revision==next.revision && memcmp(&next.save,&r->save,sizeof(r->save))==0)) {
                if(next.dirty) { next.recovery_save=next.save; next.recovery=true; }
                next.save=r->save; next.revision=r->revision; next.remote_save=r->save; next.remote_revision=r->revision;
                next.dirty=false; next.conflict=false; next.restore_ack=true; changed=true;
            }
        } else if(next.restore_ack && r->has_save && r->revision==next.remote_revision &&
                  memcmp(&next.remote_save,&r->save,sizeof(r->save))==0) {
            next.restore_ack=false;
        } else if(r->conflict && r->has_save) {
            next.conflict=true; next.remote_save=r->save; next.remote_revision=r->revision;
        } else if(r->has_save) {
            // Never use a delayed response to lower revision or overwrite newer local input.
            if(r->revision<next.revision || sent->revision!=next.revision) return false;
            if(memcmp(&sent->save,&r->save,sizeof(r->save))==0) {
                next.revision=r->revision; next.dirty=memcmp(&next.save,&sent->save,sizeof(sent->save))!=0; next.restore_ack=false;
            } else if(!next.dirty) { next.save=r->save; next.revision=r->revision; next.restore_ack=false; changed=true; }
            else { next.conflict=true; next.remote_save=r->save; next.remote_revision=r->revision; }
        } else if(next.restore_ack) return false;
    }
    return persist(p,&next,changed,write,ctx);
}
bool h2h_pocket_restore_cloud(h2h_pocket_t *p, h2h_pocket_write_fn write, void *ctx) {
    if(!p->healthy || !p->record.conflict || p->record.ownership.pending_unbind[0]) return false;
    h2h_pocket_record_t next=p->record; next.recovery_save=next.save; next.recovery=true;
    next.save=next.remote_save; next.revision=next.remote_revision; next.dirty=false; next.conflict=false;
    return persist(p,&next,true,write,ctx);
}
