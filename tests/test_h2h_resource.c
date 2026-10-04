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
 EVP_PKEY_free(signing_key);
 printf("Visual resource signature/budget/CAS/power-loss/reboot/rollback tests: PASS (%u write checkpoints, real tracked image bytes)\n",checkpoints);
}
