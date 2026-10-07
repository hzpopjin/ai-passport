#include "pocket_log_ring.h"
#include <string.h>

#define RECORD_MAGIC UINT32_C(0x524c4b50)
#define SECTOR_MAGIC UINT32_C(0x534c4b50)
_Static_assert(sizeof(pocket_log_snapshot_t)==32,"Public log snapshot budget changed");
_Static_assert(sizeof(pocket_log_ring_t)<=160,"Log ring state exceeds RAM budget");
static uint32_t crc(const uint8_t *p,size_t n) {
    uint32_t v=UINT32_MAX;
    for(size_t i=0;i<n;i++){v^=p[i];for(unsigned bit=0;bit<8;bit++)v=(v>>1)^(UINT32_C(0xedb88320)&(0u-(v&1u)));}
    return ~v;
}
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint16_t get16(const uint8_t *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)p[i]<<(i*8);return v;}
static uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static bool zero(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return false;return true;}
static bool erased(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i]!=0xff)return false;return true;}
static bool range(uint32_t offset,size_t n){return offset<=POCKET_LOG_BYTES&&n<=POCKET_LOG_BYTES-offset;}
static bool read_at(const pocket_log_io_t *io,uint32_t offset,void *p,size_t n){return range(offset,n)&&io->read(offset,p,n,io->context);}

bool pocket_log_snapshot_valid(const pocket_log_snapshot_t *s) {
    return s&&(s->reset_reason<=15||s->reset_reason==255)&&(s->page<=31||s->page==255)&&
        s->game<=2&&s->key<=3&&s->event<=5&&(s->cloud_state<=8||s->cloud_state==255)&&s->result<=2&&s->error<=7;
}
bool pocket_log_record_encode(const pocket_log_record_t *r,uint8_t b[64]) {
    if(!r||!b||!r->sequence||!r->boot||r->kind<POCKET_LOG_BOOT||r->kind>POCKET_LOG_ERROR||!pocket_log_snapshot_valid(&r->snapshot))return false;
    const pocket_log_snapshot_t *s=&r->snapshot;memset(b,0,64);
    put32(b,RECORD_MAGIC);put16(b+4,1);b[6]=r->kind;put64(b+8,r->sequence);put32(b+16,r->boot);
    put32(b+20,s->uptime_seconds);memcpy(b+24,s->firmware_elf_prefix,8);
    b[32]=s->reset_reason;b[33]=s->page;b[34]=s->game;b[35]=s->key;b[36]=s->event;b[37]=s->cloud_state;b[38]=s->result;b[39]=s->error;
    put16(b+40,s->heap_kib);put16(b+42,s->minimum_kib);put16(b+44,s->largest_kib);
    put16(b+46,s->main_stack_64b);put16(b+48,s->log_stack_64b);put16(b+50,s->errors);put32(b+60,crc(b,60));return true;
}
bool pocket_log_record_decode(pocket_log_record_t *r,const uint8_t b[64]) {
    if(!r||!b||get32(b)!=RECORD_MAGIC||get16(b+4)!=1||b[7]||!zero(b+52,8)||get32(b+60)!=crc(b,60))return false;
    pocket_log_record_t out={.sequence=get64(b+8),.boot=get32(b+16),.kind=b[6]};pocket_log_snapshot_t *s=&out.snapshot;
    s->uptime_seconds=get32(b+20);memcpy(s->firmware_elf_prefix,b+24,8);
    s->reset_reason=b[32];s->page=b[33];s->game=b[34];s->key=b[35];s->event=b[36];s->cloud_state=b[37];s->result=b[38];s->error=b[39];
    s->heap_kib=get16(b+40);s->minimum_kib=get16(b+42);s->largest_kib=get16(b+44);
    s->main_stack_64b=get16(b+46);s->log_stack_64b=get16(b+48);s->errors=get16(b+50);
    if(!out.sequence||!out.boot||out.kind<1||out.kind>3||!pocket_log_snapshot_valid(s))return false;
    *r=out;return true;
}
static void header(uint8_t b[64],uint64_t sequence,uint64_t first) {
    memset(b,0,64);put32(b,SECTOR_MAGIC);put16(b+4,1);put16(b+6,64);put64(b+8,sequence);put64(b+16,first);put32(b+60,crc(b,60));
}
static bool decode_header(const uint8_t b[64],uint64_t *sequence,uint64_t *first) {
    if(get32(b)!=SECTOR_MAGIC||get16(b+4)!=1||get16(b+6)!=64||!zero(b+24,36)||get32(b+60)!=crc(b,60))return false;
    uint64_t seq=get64(b+8),start=get64(b+16);
    if(!seq||!start)return false;
    *sequence=seq;*first=start;return true;
}
bool pocket_log_ring_open(pocket_log_ring_t *r,const pocket_log_io_t *io) {
    if(r)memset(r,0,sizeof(*r));
    if(!r||!io||!io->read||!io->write||!io->erase||io->bytes!=POCKET_LOG_BYTES)return false;
    r->io=*io;r->active=-1;
    uint8_t b[64];
    for(unsigned sector=0;sector<POCKET_LOG_SECTORS;sector++) {
        uint32_t base=sector*POCKET_LOG_SECTOR_BYTES;uint64_t sequence,first;
        if(!read_at(io,base,b,64))return false;
        if(!decode_header(b,&sequence,&first))continue;
        unsigned slot=0;bool blocked=false;
        for(;slot<POCKET_LOG_SLOTS;slot++) {
            pocket_log_record_t record;
            if(!read_at(io,base+64*(slot+1),b,64))return false;
            if(erased(b,64))break;
            if(first>UINT64_MAX-slot||!pocket_log_record_decode(&record,b)||record.sequence!=first+slot){blocked=true;break;}
            if(record.sequence>r->last_sequence){r->last_sequence=record.sequence;r->last=record;r->has_last=true;}
        }
        if(sequence>r->sector_sequence){r->sector_sequence=sequence;r->active=(int)sector;r->slot=slot;r->blocked=blocked;}
    }
    r->healthy=true;return true;
}
static bool write_verified(pocket_log_ring_t *r,uint32_t offset,const uint8_t b[64]) {
    uint8_t check[64];
    return range(offset,64)&&r->io.write(offset,b,64,r->io.context)&&read_at(&r->io,offset,check,64)&&!memcmp(b,check,64);
}
bool pocket_log_ring_append(pocket_log_ring_t *r,uint8_t kind,uint32_t boot,const pocket_log_snapshot_t *snapshot) {
    if(!r||!r->healthy||r->last_sequence==UINT64_MAX||!boot||!pocket_log_snapshot_valid(snapshot)||kind<1||kind>3)return false;
    uint8_t b[64];
    if(r->active<0||r->blocked||r->slot==POCKET_LOG_SLOTS) {
        if(r->sector_sequence==UINT64_MAX)return false;
        unsigned next=r->active<0?0u:((unsigned)r->active+1)%POCKET_LOG_SECTORS;
        uint32_t offset=next*POCKET_LOG_SECTOR_BYTES;
        /* Preserve the latest sector until the next header is durable. Never
         * rewrite a global head pointer or erase the active committed sector. */
        header(b,r->sector_sequence+1,r->last_sequence+1);
        if(!r->io.erase(offset,POCKET_LOG_SECTOR_BYTES,r->io.context)||!write_verified(r,offset,b)){r->healthy=false;return false;}
        r->active=(int)next;++r->sector_sequence;r->slot=0;r->blocked=false;
    }
    pocket_log_record_t record={.sequence=r->last_sequence+1,.boot=boot,.kind=kind,.snapshot=*snapshot};
    if(!pocket_log_record_encode(&record,b)||!write_verified(r,(uint32_t)r->active*POCKET_LOG_SECTOR_BYTES+64*(r->slot+1),b)){r->healthy=false;return false;}
    ++r->slot;r->last_sequence=record.sequence;r->last=record;r->has_last=true;return true;
}
bool pocket_log_ring_visit(const pocket_log_ring_t *r,pocket_log_visit_fn visit,void *context) {
    if(!r||!r->healthy||!visit)return false;
    uint64_t sequences[32]={0},firsts[32]={0};uint8_t b[64];
    for(unsigned i=0;i<32;i++){if(!read_at(&r->io,i*4096,b,64))return false;(void)decode_header(b,&sequences[i],&firsts[i]);}
    uint64_t previous=0;
    for(unsigned order=0;order<32;order++) {
        unsigned index=32;
        for(unsigned i=0;i<32;i++)if(sequences[i]&&(index==32||sequences[i]<sequences[index]))index=i;
        if(index==32)break;
        for(unsigned slot=0;slot<63;slot++) {
            pocket_log_record_t record;
            if(!read_at(&r->io,index*4096+64*(slot+1),b,64))return false;
            if(firsts[index]>UINT64_MAX-slot||!pocket_log_record_decode(&record,b)||record.sequence!=firsts[index]+slot)break;
            if(record.sequence<=previous)return false;
            previous=record.sequence;if(!visit(&record,context))return false;
        }
        sequences[index]=0;
    }
    return true;
}
