#include "pocket_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
static uint8_t flash[32768],old_flash[32768],old_pixels[8192],new_pixels[8192];
static unsigned writes,erases,calls,fail_call,cut,lengths[40];
static bool read_flash(uint32_t off,void *out,size_t n,void *ctx){(void)ctx;assert(off<=32768&&n<=32768-off);memcpy(out,flash+off,n);return true;}
static bool write_flash(uint32_t off,const void *bytes,size_t n,void *ctx){
    (void)ctx;assert(n<=512&&off<=32768&&n<=32768-off);writes++;calls++;if(calls<40)lengths[calls]=(unsigned)n;
    size_t actual=calls==fail_call?(cut<n?cut:n):n;
    for(size_t i=0;i<actual;i++){assert((flash[off+i]&((const uint8_t *)bytes)[i])==((const uint8_t *)bytes)[i]);flash[off+i]&=((const uint8_t *)bytes)[i];}
    return calls!=fail_call;
}
static bool erase_flash(uint32_t off,size_t n,void *ctx){(void)ctx;assert(off%16384==0&&n==16384);erases++;calls++;if(calls<40)lengths[calls]=(unsigned)n;memset(flash+off,255,calls==fail_call?(cut<n?cut:n):n);return calls!=fail_call;}
static bool sha_flash(uint32_t off,size_t n,uint8_t out[32],void *ctx){(void)ctx;assert(off<=32768&&n<=32768-off);return SHA256(flash+off,n,out)!=NULL;}
static const pocket_profile_io_t io={read_flash,write_flash,erase_flash,sha_flash,NULL};
static pocket_profile_meta_t metadata(const char *name,const uint8_t *pixels,uint32_t points){
    pocket_profile_meta_t m={.epoch=1,.account=7,.points=points,.points_available=true};strcpy(m.username,name);strcpy(m.uid,"7");uint8_t sha[32];SHA256(pixels,8192,sha);
    for(unsigned i=0;i<32;i++)snprintf(m.sha256+2*i,3,"%02x",sha[i]);return m;
}
static bool transfer(pocket_profile_t *s,const pocket_profile_meta_t *m,const uint8_t *pixels){
    if(!pocket_profile_begin(s,m))return false;
    while(s->pending_bank>=0&&s->next<8192){unsigned off=s->next;if(!pocket_profile_chunk(s,m->sha256,off,pixels+off,512))return false;}
    return pocket_profile_commit(s,m->sha256);
}
int main(void){
    memset(flash,255,sizeof(flash));for(unsigned i=0;i<8192;i++){old_pixels[i]=(uint8_t)(i*3);new_pixels[i]=(uint8_t)(i*7+1);}
    pocket_profile_t s;assert(pocket_profile_open(&s,NULL)&&!s.enabled);assert(!pocket_profile_begin(&s,NULL));
    assert(pocket_profile_open(&s,&io));pocket_profile_meta_t old=metadata("old-public-user",old_pixels,11),next=metadata("new-public-user",new_pixels,13);
    assert(transfer(&s,&old,old_pixels));assert(s.revision==1&&s.bank==0);memcpy(old_flash,flash,32768);
    unsigned count=writes+erases;assert(pocket_profile_begin(&s,&old));assert(pocket_profile_commit(&s,old.sha256));assert(writes+erases==count);
    assert(pocket_profile_begin(&s,&next));for(unsigned i=0;i<4;i++)assert(pocket_profile_chunk(&s,next.sha256,i*512,new_pixels+i*512,512));
    assert(pocket_profile_chunk(&s,next.sha256,0,new_pixels,512));assert(!pocket_profile_chunk(&s,next.sha256,0,old_pixels,512));
    assert(!pocket_profile_chunk(&s,next.sha256,4096,new_pixels,512));assert(!pocket_profile_chunk(&s,next.sha256,2048,new_pixels,513));
    assert(pocket_profile_owned(&s,7,1)&&!pocket_profile_owned(&s,8,1)&&!pocket_profile_owned(&s,7,2));
    assert(pocket_profile_open(&s,&io));assert(s.revision==1);assert(pocket_profile_begin(&s,&next)&&s.next==2048);assert(transfer(&s,&next,new_pixels));assert(s.revision==2);
    uint8_t pixels[8192];assert(pocket_profile_pixels(&s,pixels)&&!memcmp(pixels,new_pixels,8192));
    next.points=99;assert(pocket_profile_begin(&s,&next)&&s.next==8192);assert(pocket_profile_commit(&s,next.sha256));assert(s.revision==3&&s.current.points==99);
    next.points_available=false;next.points=0;assert(transfer(&s,&next,new_pixels));assert(!s.current.points_available);
    next.uid[0]='8';assert(!pocket_profile_begin(&s,&next));next.uid[0]='7';strcpy(next.username,"\xc0\xaf");assert(!pocket_profile_meta_valid(&next));
    strcpy(next.username,"\xed\xa0\x80");assert(!pocket_profile_meta_valid(&next));strcpy(next.username,"ok\n");assert(!pocket_profile_meta_valid(&next));
    memset(next.username,'a',63);next.username[63]=0;assert(pocket_profile_meta_valid(&next));
    strcpy(next.username,"组合👨‍👩‍👧‍👦");assert(pocket_profile_meta_valid(&next));
    next=metadata("new-public-user",new_pixels,13);
    memcpy(flash,old_flash,32768);calls=0;assert(pocket_profile_open(&s,&io)&&transfer(&s,&next,new_pixels));unsigned total_calls=calls;assert(total_calls==35);
    unsigned sizes[40];memcpy(sizes,lengths,sizeof(sizes));unsigned cases=0;
    for(unsigned which=1;which<=total_calls;which++)for(unsigned prefix=0;prefix<=sizes[which];prefix++){
        memcpy(flash,old_flash,32768);calls=0;fail_call=which;cut=prefix;assert(pocket_profile_open(&s,&io));assert(!transfer(&s,&next,new_pixels));
        fail_call=0;assert(pocket_profile_open(&s,&io));assert(s.revision==1||s.revision==2);
        assert(!memcmp(flash,old_flash,16384));assert(pocket_profile_pixels(&s,pixels));assert(!memcmp(pixels,s.revision==1?old_pixels:new_pixels,8192));
        /* A lost ACK or a torn pending chunk repairs through identical begin. */
        assert(transfer(&s,&next,new_pixels));assert(s.revision==2);assert(pocket_profile_pixels(&s,pixels)&&!memcmp(pixels,new_pixels,8192));cases++;
    }
    memcpy(flash,old_flash,32768);assert(pocket_profile_open(&s,&io));assert(pocket_profile_begin(&s,&next));
    for(unsigned i=0;i<16;i++)assert(pocket_profile_chunk(&s,next.sha256,i*512,old_pixels+i*512,512));assert(!pocket_profile_commit(&s,next.sha256));assert(s.revision==1);
    assert(pocket_profile_open(&s,&io)&&s.revision==1);
    printf("PASS %u exhaustive byte-prefix erase/header/chunk/marker/commit faults; resume, duplicate chunks, hash equality, owner fence, UTF8, unavailable points, atomic old/new visibility\n",cases);
}
