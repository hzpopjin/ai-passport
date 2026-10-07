#include "pocket_profile.h"
#include <string.h>
#include <stdio.h>

#define MARKS 0x3000u
#define FOOTER 0x3fc0u
static uint32_t crc(const uint8_t *b,size_t n){uint32_t c=~0u;while(n--){c^=*b++;for(unsigned i=0;i<8;i++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const uint8_t *p){return u32(p)|((uint64_t)u32(p+4)<<32);}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void put64(uint8_t *p,uint64_t v){put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));}
static bool hex(const char *s){if(!s||strlen(s)!=64)return false;for(unsigned i=0;i<64;i++)if(!strchr("0123456789abcdef",s[i]))return false;return true;}
static bool utf8(const char *s){
    if(!s||!s[0]||strlen(s)>63)return false;
    const uint8_t *p=(const uint8_t *)s;
    while(*p){uint32_t cp=*p++;unsigned n=0;uint32_t minimum=0;
        if(cp<0x80){if(cp<32||cp==127)return false;continue;}
        if(cp>=0xc2&&cp<=0xdf){cp&=31;n=1;minimum=0x80;}
        else if(cp>=0xe0&&cp<=0xef){cp&=15;n=2;minimum=0x800;}
        else if(cp>=0xf0&&cp<=0xf4){cp&=7;n=3;minimum=0x10000;}else return false;
        while(n--){if((*p&0xc0)!=0x80)return false;cp=(cp<<6)|(*p++&63);}
        if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||(cp>=0x80&&cp<=0x9f))return false;
    }return true;
}
bool pocket_profile_meta_valid(const pocket_profile_meta_t *m){
    if(!m||!m->account||!m->epoch||m->epoch>9007199254740991ULL||
       !memchr(m->username,0,64)||!memchr(m->uid,0,32)||!memchr(m->sha256,0,65)||!utf8(m->username)||!hex(m->sha256))return false;
    char canonical[32];snprintf(canonical,sizeof(canonical),"%lu",(unsigned long)m->account);
    return !strcmp(canonical,m->uid)&&(!m->points_available?m->points==0:true);
}
static void header(const pocket_profile_meta_t *m,uint64_t revision,uint8_t b[256]){
    memset(b,0,256);memcpy(b,"PPR1",4);put64(b+8,revision);put64(b+16,m->epoch);put32(b+24,m->account);put32(b+28,m->points);b[32]=m->points_available;
    b[33]=64;b[34]=64;b[35]=1;put32(b+36,POCKET_PROFILE_BYTES);
    memcpy(b+40,m->username,64);memcpy(b+104,m->uid,32);memcpy(b+136,m->sha256,65);put32(b+252,crc(b,252));
}
static bool parse_header(const uint8_t b[256],pocket_profile_meta_t *m,uint64_t *revision){
    if(memcmp(b,"PPR1",4)||u32(b+252)!=crc(b,252)||b[32]>1||b[33]!=64||b[34]!=64||b[35]!=1||u32(b+36)!=8192)return false;
    for(unsigned i=4;i<8;i++)if(b[i])return false;
    for(unsigned i=201;i<252;i++)if(b[i])return false;
    memset(m,0,sizeof(*m));m->epoch=u64(b+16);m->account=u32(b+24);m->points=u32(b+28);m->points_available=b[32];
    memcpy(m->username,b+40,64);memcpy(m->uid,b+104,32);memcpy(m->sha256,b+136,65);*revision=u64(b+8);
    return *revision&&*revision<=9007199254740991ULL&&pocket_profile_meta_valid(m);
}
static bool equal(const pocket_profile_meta_t *a,const pocket_profile_meta_t *b){return a->epoch==b->epoch&&a->account==b->account&&a->points==b->points&&a->points_available==b->points_available&&!strcmp(a->username,b->username)&&!strcmp(a->uid,b->uid)&&!strcmp(a->sha256,b->sha256);}
static bool digest(pocket_profile_t *s,int bank,const char *expected){uint8_t d[32];char h[65];if(!s->io.sha((uint32_t)bank*POCKET_PROFILE_BANK+256,8192,d,s->io.context))return false;for(unsigned i=0;i<32;i++){h[2*i]="0123456789abcdef"[d[i]>>4];h[2*i+1]="0123456789abcdef"[d[i]&15];}h[64]=0;return !strcmp(h,expected);}
static bool written(pocket_profile_t *s,uint32_t off,const void *bytes,size_t n){
    uint8_t check[512];if(n>sizeof(check)||!s->io.write(off,bytes,n,s->io.context)||!s->io.read(off,check,n,s->io.context)||memcmp(check,bytes,n)){s->uncertain=true;return false;}return true;
}
static bool recover_pending(pocket_profile_t *,int,const pocket_profile_meta_t *,uint64_t);
bool pocket_profile_open(pocket_profile_t *s,const pocket_profile_io_t *io){
    memset(s,0,sizeof(*s));s->bank=s->pending_bank=-1;if(!io)return true;
    if(!io->read||!io->write||!io->erase||!io->sha)return false;
    s->io=*io;s->enabled=true;
    pocket_profile_meta_t m[2];uint64_t rev[2]={0,0};bool valid[2]={false,false},headers[2]={false,false};
    for(int bank=0;bank<2;bank++){
        uint8_t b[256],f[64];uint32_t start=(uint32_t)bank*POCKET_PROFILE_BANK;
        if(!io->read(start,b,256,io->context)||!io->read(start+FOOTER,f,64,io->context)){s->uncertain=true;return false;}
        if(!parse_header(b,&m[bank],&rev[bank]))continue;
        headers[bank]=true;
        valid[bank]=!memcmp(f,"PPC1",4)&&u64(f+8)==rev[bank]&&u32(f+16)==u32(b+252)&&u32(f+60)==crc(f,60)&&digest(s,bank,m[bank].sha256);
    }
    if(valid[0]&&valid[1]&&rev[0]==rev[1]){s->uncertain=true;return false;}
    for(int bank=0;bank<2;bank++)if(valid[bank]&&(s->bank<0||rev[bank]>s->revision)){s->bank=bank;s->revision=rev[bank];s->current=m[bank];}
    int pending=s->bank==0?1:0;
    if(headers[pending]&&!valid[pending]&&rev[pending]==s->revision+1)(void)recover_pending(s,pending,&m[pending],rev[pending]);
    return !s->uncertain;
}
bool pocket_profile_owned(const pocket_profile_t *s,uint32_t account,uint64_t epoch){return s&&s->enabled&&s->bank>=0&&s->current.account==account&&s->current.epoch==epoch;}
static bool recover_pending(pocket_profile_t *s,int bank,const pocket_profile_meta_t *m,uint64_t revision){
    uint8_t b[256],mark[16],chunk[512];pocket_profile_meta_t stored;uint64_t r;
    uint32_t start=(uint32_t)bank*POCKET_PROFILE_BANK;
    if(!s->io.read(start,b,256,s->io.context)){s->uncertain=true;return false;}
    if(!parse_header(b,&stored,&r)||r!=revision||!equal(&stored,m))return false;
    unsigned next=0;
    for(unsigned i=0;i<16;i++){
        if(!s->io.read(start+MARKS+i*16,mark,16,s->io.context)){s->uncertain=true;return false;}
        bool empty=true;for(unsigned j=0;j<16;j++)if(mark[j]!=255)empty=false;
        if(empty)break;
        if(memcmp(mark,"PPCk",4)||u32(mark+4)!=i*512||u32(mark+12)!=crc(mark,12)||
           !s->io.read(start+256+i*512,chunk,512,s->io.context)||u32(mark+8)!=crc(chunk,512))return false;
        next+=512;
    }
    uint8_t footer[64],expected[64]={0};
    if(!s->io.read(start+FOOTER,footer,64,s->io.context)){s->uncertain=true;return false;}
    memcpy(expected,"PPC1",4);put64(expected+8,revision);put32(expected+16,u32(b+252));put32(expected+60,crc(expected,60));
    for(unsigned i=0;i<64;i++)if((footer[i]&expected[i])!=expected[i])return false;
    if(next==8192&&!digest(s,bank,m->sha256))return false;
    s->pending=*m;s->pending_revision=revision;s->pending_bank=bank;s->next=next;return true;
}
bool pocket_profile_begin(pocket_profile_t *s,const pocket_profile_meta_t *m){
    if(!s||!s->enabled||!pocket_profile_meta_valid(m))return false;
    /* A readback/write failure can hide a committed bank. Reopen before erasing
     * either bank so a lost ACK cannot erase the last durable version. */
    if(s->uncertain){pocket_profile_io_t io=s->io;if(!pocket_profile_open(s,&io))return false;}
    if(s->bank>=0&&equal(&s->current,m)){s->pending_bank=-1;s->next=0;return true;}
    if(s->revision>=9007199254740991ULL)return false;
    int bank=s->bank==0?1:0;uint64_t r=s->revision+1;
    if(recover_pending(s,bank,m,r))return true;
    if(s->uncertain)return false;
    uint32_t start=(uint32_t)bank*POCKET_PROFILE_BANK;uint8_t b[256];header(m,r,b);
    if(!s->io.erase(start,POCKET_PROFILE_BANK,s->io.context)){s->uncertain=true;return false;}
    if(!written(s,start,b,256))return false;
    s->pending=*m;s->pending_bank=bank;s->pending_revision=r;s->next=0;
    /* Same normalized image never crosses BLE again. Copy only verified old
     * pixels into the new metadata transaction, one bounded chunk at a time. */
    if(pocket_profile_owned(s,m->account,m->epoch)&&!strcmp(s->current.sha256,m->sha256)){
        uint8_t chunk[512];for(unsigned i=0;i<16;i++){
            if(!s->io.read((uint32_t)s->bank*POCKET_PROFILE_BANK+256+i*512,chunk,512,s->io.context)||!pocket_profile_chunk(s,m->sha256,i*512,chunk,512))return false;
        }
    }return true;
}
bool pocket_profile_chunk(pocket_profile_t *s,const char *sha,uint32_t off,const uint8_t *bytes,size_t length){
    if(!s||!s->enabled||s->uncertain||s->pending_bank<0||!sha||strcmp(sha,s->pending.sha256)||!bytes||length!=512||off%512||off>8192-512||off>s->next)return false;
    uint32_t start=(uint32_t)s->pending_bank*POCKET_PROFILE_BANK;
    if(off<s->next){uint8_t old[512];return s->io.read(start+256+off,old,512,s->io.context)&&!memcmp(old,bytes,512);}
    uint8_t mark[16]={0};memcpy(mark,"PPCk",4);put32(mark+4,off);put32(mark+8,crc(bytes,512));put32(mark+12,crc(mark,12));
    if(!written(s,start+256+off,bytes,512)||!written(s,start+MARKS+(off/512)*16,mark,16))return false;
    s->next+=512;return true;
}
bool pocket_profile_commit(pocket_profile_t *s,const char *sha){
    if(!s||!s->enabled||s->uncertain||!sha)return false;
    if(s->pending_bank<0)return s->bank>=0&&!strcmp(s->current.sha256,sha);
    if(s->next!=8192||strcmp(s->pending.sha256,sha)||!digest(s,s->pending_bank,sha))return false;
    uint8_t b[256],f[64]={0};uint32_t start=(uint32_t)s->pending_bank*POCKET_PROFILE_BANK;
    if(!s->io.read(start,b,256,s->io.context)){s->uncertain=true;return false;}
    pocket_profile_meta_t m;uint64_t r;if(!parse_header(b,&m,&r)||r!=s->pending_revision||!equal(&m,&s->pending))return false;
    memcpy(f,"PPC1",4);put64(f+8,r);put32(f+16,u32(b+252));put32(f+60,crc(f,60));
    if(!written(s,start+FOOTER,f,64))return false;
    s->bank=s->pending_bank;s->current=s->pending;s->revision=r;s->pending_bank=-1;s->next=0;return true;
}
bool pocket_profile_pixels(pocket_profile_t *s,uint8_t out[8192]){return s&&s->bank>=0&&out&&s->io.read((uint32_t)s->bank*POCKET_PROFILE_BANK+256,out,8192,s->io.context);}
