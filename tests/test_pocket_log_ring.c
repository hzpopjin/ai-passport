#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/h2h/pocket_log_ring.c"

/* NOR model: exact partition bounds, 1->0 programming, aligned erase and
 * arbitrary byte-cut power loss. No production filesystem or device access. */
typedef struct {
    uint8_t bytes[POCKET_LOG_BYTES];
    unsigned erases[32],writes,reads;
    int fail_read,fail_write,fail_erase,cut;
} flash_t;
static flash_t flash,baseline;
static bool read_flash(uint32_t off,void *out,size_t n,void *ctx) {
    flash_t *f=ctx;assert(off<=sizeof(f->bytes)&&n<=sizeof(f->bytes)-off);
    if(f->fail_read>=0&&(int)f->reads++==f->fail_read)return false;
    memcpy(out,f->bytes+off,n);return true;
}
static bool write_flash(uint32_t off,const void *in,size_t n,void *ctx) {
    flash_t *f=ctx;const uint8_t *p=in;assert(off<=sizeof(f->bytes)&&n<=sizeof(f->bytes)-off);
    unsigned call=f->writes++;size_t count=n;
    bool fail=f->fail_write>=0&&(int)call==f->fail_write;
    if(fail&&f->cut>=0&&(size_t)f->cut<count)count=(size_t)f->cut;
    for(size_t i=0;i<count;i++){assert((f->bytes[off+i]&p[i])==p[i]);f->bytes[off+i]&=p[i];}
    return !fail;
}
static bool erase_flash(uint32_t off,size_t n,void *ctx) {
    flash_t *f=ctx;assert(off%4096==0&&n==4096&&off+n<=sizeof(f->bytes));
    unsigned total=0;for(unsigned i=0;i<32;i++)total+=f->erases[i];
    f->erases[off/4096]++;bool fail=f->fail_erase>=0&&(int)total==f->fail_erase;
    size_t count=fail&&f->cut>=0&&(size_t)f->cut<n?(size_t)f->cut:n;
    memset(f->bytes+off,255,count);return !fail;
}
static pocket_log_io_t io(void){return (pocket_log_io_t){read_flash,write_flash,erase_flash,&flash,POCKET_LOG_BYTES};}
static void clean(void){memset(&flash,0,sizeof(flash));memset(flash.bytes,255,sizeof(flash.bytes));flash.fail_read=flash.fail_write=flash.fail_erase=flash.cut=-1;}
static pocket_log_snapshot_t sample(void){return (pocket_log_snapshot_t){.uptime_seconds=123,.firmware_elf_prefix={1,2,3,4,5,6,7,8},.reset_reason=4,.page=12,.game=2,.key=3,.event=4,.cloud_state=7,.result=1,.error=6,.heap_kib=77,.minimum_kib=70,.largest_kib=64,.main_stack_64b=73,.log_stack_64b=40,.errors=2};}
typedef struct {uint64_t first,last;unsigned count;} visited_t;
static bool visit(const pocket_log_record_t *r,void *ctx){visited_t *v=ctx;if(!v->count)v->first=r->sequence;else assert(r->sequence==v->last+1);v->last=r->sequence;v->count++;return true;}
static pocket_log_ring_t open_ring(void){pocket_log_ring_t r;pocket_log_io_t adapter=io();assert(pocket_log_ring_open(&r,&adapter));return r;}
static void codec(void) {
    pocket_log_record_t r={.sequence=1,.boot=2,.kind=POCKET_LOG_ERROR,.snapshot=sample()},out;
    uint8_t b[64],copy[64];assert(pocket_log_record_encode(&r,b));
    assert(!memcmp(b,"PKLR\1\0\3\0",8));assert(get32(b+20)==123);
    /* Independent Python struct/zlib golden schema fixture. */
    assert(get32(b+60)==UINT32_C(0x10c0ee2b));
    assert(pocket_log_record_decode(&out,b));assert(out.sequence==1&&out.boot==2&&!memcmp(&out.snapshot,&r.snapshot,32));
    for(unsigned bit=0;bit<512;bit++){memcpy(copy,b,64);copy[bit/8]^=(uint8_t)(1u<<(bit%8));assert(!pocket_log_record_decode(&out,copy));}
    const unsigned bad[]={7,32,33,34,35,36,37,38,39,52,59};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){memcpy(copy,b,64);copy[bad[i]]=254;put32(copy+60,crc(copy,60));assert(!pocket_log_record_decode(&out,copy));}
    r.snapshot.page=32;assert(!pocket_log_record_encode(&r,b));
    puts("codec: all 512 single-bit corruptions and invalid public fields rejected");
}
static void wrap(void) {
    clean();pocket_log_ring_t r=open_ring();pocket_log_snapshot_t s=sample();
    assert(!flash.writes);pocket_log_io_t wrong=io();wrong.bytes--;assert(!pocket_log_ring_open(&r,&wrong));r=open_ring();
    for(unsigned i=1;i<=63*32*3+17;i++){s.uptime_seconds=i;assert(pocket_log_ring_append(&r,POCKET_LOG_CHECKPOINT,1,&s));}
    r=open_ring();visited_t v={0};assert(pocket_log_ring_visit(&r,visit,&v));
    assert(v.last==6065&&v.count==1970&&v.first==4096);
    unsigned lo=UINT32_MAX,hi=0;for(unsigned i=0;i<32;i++){if(flash.erases[i]<lo)lo=flash.erases[i];if(flash.erases[i]>hi)hi=flash.erases[i];}assert(hi-lo==1);
    printf("wrap: %u ordered records retained; wear max-min=%u erases\n",v.count,hi-lo);
}
static void power_loss(void) {
    clean();pocket_log_ring_t r=open_ring();pocket_log_snapshot_t s=sample();
    for(unsigned i=0;i<63;i++)assert(pocket_log_ring_append(&r,1,1,&s));
    /* Place latest full sector at31, with an older sector at0 to be erased. */
    memcpy(flash.bytes+31*4096,flash.bytes,4096);header(flash.bytes+31*4096,2,64);
    for(unsigned i=0;i<63;i++){pocket_log_record_t record={.sequence=64+i,.boot=1,.kind=1,.snapshot=s};assert(pocket_log_record_encode(&record,flash.bytes+31*4096+64*(i+1)));}
    baseline=flash;
    unsigned cases=0;
    for(unsigned operation=0;operation<3;operation++) {
        unsigned limit=operation==0?4096:64;
        for(unsigned cut=0;cut<=limit;cut++) {
            flash=baseline;r=open_ring();flash.cut=(int)cut;
            if(operation==0)flash.fail_erase=1;else flash.fail_write=(int)baseline.writes+(int)operation-1;
            assert(!pocket_log_ring_append(&r,2,2,&s));assert(!r.healthy);
            unsigned writes=flash.writes;assert(!pocket_log_ring_append(&r,2,2,&s));assert(writes==flash.writes);
            flash.fail_write=flash.fail_erase=-1;flash.cut=-1;r=open_ring();
            assert(r.last_sequence>=126&&r.last_sequence<=127);
            assert(pocket_log_ring_append(&r,2,2,&s));assert(r.last_sequence>=127&&r.last_sequence<=128);cases++;
        }
    }
    printf("power loss: %u byte-cut erase/header/record cases preserve latest committed record\n",cases);
}
static void failures(void) {
    clean();pocket_log_ring_t r=open_ring();pocket_log_snapshot_t s=sample();assert(pocket_log_ring_append(&r,1,1,&s));
    flash.fail_read=0;flash.reads=0;assert(!pocket_log_ring_append(&r,2,1,&s)&&!r.healthy);
    flash.fail_read=-1;r=open_ring();assert(r.last_sequence==2);
    flash.bytes[3*64+1]=0; /* torn next slot rotates rather than overwrites */
    r=open_ring();assert(r.blocked);assert(pocket_log_ring_append(&r,2,1,&s)&&r.active==1);
    /* CRC-valid but semantically invalid header must not enter visitor. */
    header(flash.bytes+8192,99,1);put64(flash.bytes+8192+16,0);put32(flash.bytes+8192+60,crc(flash.bytes+8192,60));
    r=open_ring();visited_t v={0};assert(pocket_log_ring_visit(&r,visit,&v)&&v.count==3);
    r.last_sequence=UINT64_MAX;unsigned before=flash.writes;assert(!pocket_log_ring_append(&r,2,1,&s)&&before==flash.writes);
    flash.fail_read=0;flash.reads=0;assert(!pocket_log_ring_open(&r,&(pocket_log_io_t){read_flash,write_flash,erase_flash,&flash,POCKET_LOG_BYTES}));assert(!r.healthy);
    puts("faults: readback/torn/invalid-header/overflow failures bounded");
}
int main(void){codec();wrap();power_loss();failures();puts("Pocket log NOR/CRC/recovery: PASS");}
