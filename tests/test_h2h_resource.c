#include "h2h_resource.h"
#include <assert.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char slots[2][H2H_RESOURCE_SLOT_BYTES], nvs[H2H_RESOURCE_STATE_BYTES], source[H2H_RESOURCE_BYTES], public_key[32];
static EVP_PKEY *signing_key;
static unsigned writes,fail_at,erases,fetches;
static bool fail_commit,authorized=true,corrupt_fetch;
static bool key(const char *id,unsigned char out[32],void *ctx){(void)ctx;if(strcmp(id,"fixture"))return false;memcpy(out,public_key,32);return true;}
static bool read_slot(int slot,size_t offset,void *out,size_t length,void *ctx){(void)ctx;assert(slot>=0&&slot<2&&offset+length<=sizeof(slots[0]));memcpy(out,slots[slot]+offset,length);return true;}
static bool write_slot(int slot,size_t offset,const void *bytes,size_t length,void *ctx){(void)ctx;writes++;if(fail_at&&writes==fail_at){memcpy(slots[slot]+offset,bytes,length/2);return false;}assert(slot>=0&&slot<2&&offset+length<=sizeof(slots[0]));memcpy(slots[slot]+offset,bytes,length);return true;}
static bool erase_slot(int slot,void *ctx){(void)ctx;erases++;memset(slots[slot],0xff,sizeof(slots[slot]));return true;}
static bool hash_slot(int slot,size_t offset,size_t length,unsigned char out[32],void *ctx){(void)ctx;return SHA256(slots[slot]+offset,length,out)!=NULL;}
static bool fetch(size_t offset,void *out,size_t length,void *ctx){(void)ctx;fetches++;assert(offset+length<=sizeof(source));memcpy(out,source+offset,length);if(corrupt_fetch&&offset==0)((unsigned char *)out)[100]^=1;return true;}
static bool alive(void *ctx){(void)ctx;return authorized;}
static bool commit(const void *bytes,size_t length,void *ctx){(void)ctx;if(fail_commit)return false;assert(length==sizeof(nvs));memcpy(nvs,bytes,length);return true;}
static h2h_resource_io_t io={.capacity={H2H_RESOURCE_SLOT_BYTES,H2H_RESOURCE_SLOT_BYTES},.key=key,.read=read_slot,.write=write_slot,.erase=erase_slot,.hash=hash_slot,.fetch=fetch,.alive=alive,.commit=commit};
static h2h_resource_manifest_t manifest(unsigned version){
 h2h_resource_manifest_t m={.kind=1,.version=version,.min_firmware=1,.max_firmware=1};strcpy(m.id,"h2h-ian-visual");strcpy(m.key_id,"fixture");SHA256(source,sizeof(source),m.sha256);
 char hex[65],message[512];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",m.sha256[i]);
 int n=snprintf(message,sizeof(message),"[\"pocket-visual-v2\",\"member_visual\",\"esp32c3-pocket-8m\",2,\"%s\",%u,1,1,%u,15360,\"%s\",\"fixture\"]",m.id,version,H2H_RESOURCE_BYTES,hex);
 EVP_MD_CTX *context=EVP_MD_CTX_new();size_t size=64;assert(EVP_DigestSignInit(context,NULL,NULL,NULL,signing_key)==1&&EVP_DigestSign(context,m.signature,&size,(const unsigned char *)message,(size_t)n)==1&&size==64);EVP_MD_CTX_free(context);return m;
}

static unsigned char resume_bytes[160];static bool resume_present,resume_fail;
static bool digest_bytes(const void *bytes,size_t n,unsigned char out[32],void *ctx){(void)ctx;return SHA256(bytes,n,out)!=NULL;}
static bool erase_range_test(int slot,size_t offset,size_t n,void *ctx){(void)ctx;assert(offset%4096==0&&n%4096==0&&offset+n<=H2H_RESOURCE_SLOT_BYTES);erases++;memset(slots[slot]+offset,0xff,n);return true;}
static int resume_read(void *out,size_t n,void *ctx){(void)ctx;assert(n==160);if(!resume_present)return 0;memcpy(out,resume_bytes,n);return 1;}
static bool resume_write(const void *bytes,size_t n,void *ctx){(void)ctx;assert(n==160);if(resume_fail)return false;memcpy(resume_bytes,bytes,n);resume_present=true;return true;}
static bool append(h2h_resource_t *r,const h2h_resource_io_t *ops,size_t n){unsigned char sha[32];SHA256(source+r->cursor,n,sha);return h2h_resource_transfer_chunk(r,r->staged_epoch,r->staged_revision,r->staged.sha256,r->cursor,source+r->cursor,n,sha,ops);}
static void resume_tests(h2h_resource_manifest_t current,h2h_resource_manifest_t next){
 h2h_resource_t r;h2h_resource_init(&r);authorized=true;fail_at=0;resume_present=false;resume_fail=false;
 h2h_resource_io_t ops=io;ops.digest=digest_bytes;ops.erase_range=erase_range_test;ops.read_resume=resume_read;ops.commit_resume=resume_write;
 assert(h2h_resource_stage(&r,&current,12,1,&io)&&h2h_resource_publish(&r,12,&io));
 unsigned char active_sha[32];SHA256(slots[0],sizeof(slots[0]),active_sha);
 assert(h2h_resource_transfer_begin(&r,&next,12,2,false,&ops)&&r.cursor==0&&r.resume.slot==1);
 for(unsigned i=0;i<6;i++)assert(append(&r,&ops,1024));
 assert(r.cursor==6144&&r.resume.validated_offset==4096);
 h2h_resource_t reboot=r;reboot.transferring=false;
 assert(h2h_resource_transfer_begin(&reboot,&next,12,2,false,&ops)&&reboot.cursor==4096);
 unsigned erase_before=erases;assert(append(&reboot,&ops,1024)&&erases==erase_before+1); /* incomplete sector is re-erased */
 unsigned before=writes;unsigned char bad[32]={0};
 assert(!h2h_resource_transfer_chunk(&reboot,13,2,next.sha256,reboot.cursor,source+reboot.cursor,1024,bad,&ops)&&writes==before);
 assert(!h2h_resource_transfer_chunk(&reboot,12,2,next.sha256,reboot.cursor,source+reboot.cursor,1024,bad,&ops)&&writes==before);
 slots[1][H2H_RESOURCE_PAYLOAD_OFFSET+100]^=1;reboot.transferring=false;
 assert(!h2h_resource_transfer_begin(&reboot,&next,12,2,false,&ops));
 assert(h2h_resource_transfer_begin(&reboot,&next,12,2,true,&ops)&&reboot.cursor==0);
 /* Torn write and failed checkpoint commit preserve the active bank. */
 for(unsigned cut=1;cut<=4;cut++){
  assert(h2h_resource_transfer_begin(&reboot,&next,12,2,true,&ops));writes=0;fail_at=cut;
  for(unsigned i=0;i<4;i++)if(!append(&reboot,&ops,1024))break;
  assert(!reboot.transferring);fail_at=0;
  assert(h2h_resource_transfer_begin(&reboot,&next,12,2,false,&ops)&&reboot.cursor==0);
  unsigned char sha[32];SHA256(slots[0],sizeof(slots[0]),sha);assert(!memcmp(sha,active_sha,32));
 }
 assert(h2h_resource_transfer_begin(&reboot,&next,12,2,true,&ops));
 for(unsigned i=0;i<3;i++){assert(append(&reboot,&ops,1024));}
 resume_fail=true;assert(!append(&reboot,&ops,1024));resume_fail=false;
 assert(h2h_resource_transfer_begin(&reboot,&next,12,2,false,&ops)&&reboot.cursor==0);
 /* The same immutable transfer resumes independently of which channel fetched it. */
 while(reboot.cursor<H2H_RESOURCE_BYTES){size_t n=H2H_RESOURCE_BYTES-reboot.cursor;if(n>1024)n=1024;assert(append(&reboot,&ops,n));}
 assert(reboot.resume.validated_offset==H2H_RESOURCE_BYTES);assert(h2h_resource_transfer_finish(&reboot,&ops));
 assert(h2h_resource_publish(&reboot,12,&ops)&&reboot.record.active==1&&reboot.record.previous==0);
 slots[1][H2H_RESOURCE_PAYLOAD_OFFSET+700]^=1;assert(h2h_resource_rollback(&reboot,12,&ops)&&reboot.record.active==0);
 assert(!h2h_resource_transfer_begin(&reboot,&current,12,3,true,&ops)); /* watermark survives rollback */
 authorized=false;assert(!h2h_resource_transfer_begin(&reboot,&next,12,3,true,&ops));authorized=true;
 puts("Resource resume: sector-aligned durable checkpoints, actual-prefix rehash, explicit restart, bounded SHA chunks, torn writes/checkpoints, channel-neutral transfer and rollback watermark: PASS");
}

int main(int argc,char **argv){
 assert(argc==2);FILE *input=fopen(argv[1],"rb");assert(input&&fread(source,1,sizeof(source),input)==sizeof(source)&&fgetc(input)==EOF);fclose(input);
 EVP_PKEY_CTX *keygen=EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519,NULL);assert(keygen&&EVP_PKEY_keygen_init(keygen)==1&&EVP_PKEY_keygen(keygen,&signing_key)==1);EVP_PKEY_CTX_free(keygen);size_t public_size=32;assert(EVP_PKEY_get_raw_public_key(signing_key,public_key,&public_size)==1);
 h2h_resource_t r;h2h_resource_init(&r);h2h_resource_manifest_t first=manifest(1),second=manifest(2);
 assert(h2h_resource_manifest_valid(&first,&io));
 for(unsigned mutation=0;mutation<7;mutation++){h2h_resource_manifest_t bad=first;
  if(mutation==0)bad.signature[0]^=1;
  if(mutation==1)bad.sha256[0]^=1;
  if(mutation==2)bad.version++;
  if(mutation==3)bad.kind=3;
  if(mutation==4)strcpy(bad.key_id,"unknown");
  if(mutation==5)bad.min_firmware=2;
  if(mutation==6)bad.id[32]='x';
  assert(!h2h_resource_stage(&r,&bad,1,1,&io));assert(erases==0);
 }
 h2h_resource_io_t small=io;small.capacity[1]=H2H_RESOURCE_BYTES;assert(!h2h_resource_stage(&r,&first,1,1,&small)&&!erases);
 assert(h2h_resource_stage(&r,&first,1,1,&io));assert(r.pending&&r.record.active==-1);
 assert(h2h_resource_publish(&r,1,&io));assert(r.record.active==0&&h2h_resource_active(&r,2)==-1);
 const h2h_resource_record_t baseline=r.record;unsigned char durable[sizeof(nvs)];memcpy(durable,nvs,sizeof(nvs));
 // Fail every payload/meta write in the inactive slot; reboot always uses old active.
 const unsigned checkpoints=(H2H_RESOURCE_BYTES+1023)/1024+1;
 for(unsigned cut=1;cut<=checkpoints;cut++){
  r.record=baseline;writes=0;fail_at=cut;assert(!h2h_resource_stage(&r,&second,1,2,&io));
  assert(!memcmp(nvs,durable,sizeof(nvs))&&h2h_resource_slot_valid(0,&io,NULL));
  h2h_resource_t reboot;h2h_resource_init(&reboot);assert(h2h_resource_decode(&reboot,nvs,sizeof(nvs))&&h2h_resource_recover(&reboot,1,&io)&&reboot.record.active==0);
 }
 fail_at=0;corrupt_fetch=true;assert(!h2h_resource_stage(&r,&second,1,2,&io));corrupt_fetch=false;
 assert(h2h_resource_stage(&r,&second,1,2,&io));fail_commit=true;assert(!h2h_resource_publish(&r,1,&io)&&r.record.active==0&&!memcmp(nvs,durable,sizeof(nvs)));fail_commit=false;
 authorized=false;assert(!h2h_resource_publish(&r,1,&io)&&r.record.active==0);authorized=true;
 assert(!h2h_resource_publish(&r,3,&io));assert(h2h_resource_publish(&r,1,&io)&&r.record.active==1);
 unsigned before=erases;assert(!h2h_resource_stage(&r,&first,1,3,&io)&&erases==before);
 slots[1][H2H_RESOURCE_PAYLOAD_OFFSET+700]^=1;assert(h2h_resource_recover(&r,1,&io)&&r.record.active==0);
 assert(!h2h_resource_stage(&r,&first,1,3,&io)); // Fallback never lowers the watermark.
 assert(h2h_resource_builtin(&r,1,4,&io)&&r.record.active==-1);
 assert(h2h_resource_decode(&r,nvs,sizeof(nvs)));unsigned char bad[sizeof(nvs)];memcpy(bad,nvs,sizeof(bad));bad[20]^=1;assert(!h2h_resource_decode(&r,bad,sizeof(bad))&&!h2h_resource_decode(&r,nvs,sizeof(nvs)-1));
 resume_tests(first,second);
 EVP_PKEY_free(signing_key);
 printf("Visual resource signature/budget/CAS/power-loss/reboot/rollback tests: PASS (%u write checkpoints, real tracked image bytes)\n",checkpoints);
}
