#include "h2h_resource.h"
#include "monocypher-ed25519.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define MAX_EPOCH UINT64_C(9007199254740991)
static void put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void put64(unsigned char *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t get32(const unsigned char *p){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)p[i]<<(8*i);return v;}
static uint64_t get64(const unsigned char *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static uint32_t check(const unsigned char *p,size_t n){uint32_t v=2166136261u;for(size_t i=0;i<n;i++)v=(v^p[i])*16777619u;return v;}
static bool id_valid(const char id[33]){
 size_t n=0;for(;n<33&&id[n];n++)if(!((id[n]>='a'&&id[n]<='z')||(id[n]>='0'&&id[n]<='9')||id[n]=='_'||id[n]=='-'))return false;
 if(!n||n>32)return false;
 for(size_t i=n;i<33;i++)if(id[i])return false;
 return true;
}
void h2h_resource_init(h2h_resource_t *r){memset(r,0,sizeof(*r));r->record.active=r->record.previous=r->staged_slot=-1;}
void h2h_resource_encode(const h2h_resource_record_t *r,unsigned char out[H2H_RESOURCE_STATE_BYTES]){
 memset(out,0,H2H_RESOURCE_STATE_BYTES);memcpy(out,"PKSTATE2",8);put32(out+8,(uint32_t)(r->active+1));put32(out+12,(uint32_t)(r->previous+1));
 put64(out+16,r->epoch);put32(out+24,r->generation);put32(out+28,r->revision);put32(out+32,r->highwater_count);
 for(unsigned i=0;i<r->highwater_count;i++){unsigned char *p=out+64+i*72;memcpy(p,r->highwaters[i].id,33);put32(p+36,r->highwaters[i].version);memcpy(p+40,r->highwaters[i].sha256,32);}
 put32(out+1532,check(out,1532));
}
bool h2h_resource_decode(h2h_resource_t *r,const void *bytes,size_t length){
 if(!bytes||length!=H2H_RESOURCE_STATE_BYTES)return false;
 const unsigned char *p=bytes;if(memcmp(p,"PKSTATE2",8)||get32(p+1532)!=check(p,1532)||get32(p+8)>2||get32(p+12)>2)return false;
 h2h_resource_record_t next={0};next.active=(int)get32(p+8)-1;next.previous=(int)get32(p+12)-1;next.epoch=get64(p+16);
 next.generation=get32(p+24);next.revision=get32(p+28);next.highwater_count=get32(p+32);
 if(next.epoch>MAX_EPOCH||next.highwater_count>H2H_RESOURCE_HIGHWATERS||(next.active>=0&&!next.epoch)||(next.active>=0&&next.active==next.previous))return false;
 for(unsigned i=0;i<next.highwater_count;i++){const unsigned char *entry=p+64+i*72;memcpy(next.highwaters[i].id,entry,33);next.highwaters[i].version=get32(entry+36);memcpy(next.highwaters[i].sha256,entry+40,32);
  if(!id_valid(next.highwaters[i].id)||!next.highwaters[i].version)return false;
  for(unsigned j=0;j<i;j++)if(!strcmp(next.highwaters[i].id,next.highwaters[j].id))return false;
 }
 r->record=next;r->pending=false;return true;
}
static void manifest_encode(const h2h_resource_manifest_t *m,unsigned char out[512]){
 memset(out,0,512);memcpy(out,"PKSIG02\0",8);put32(out+8,m->kind);put32(out+12,m->version);put32(out+16,m->min_firmware);put32(out+20,m->max_firmware);
 put32(out+24,H2H_RESOURCE_BYTES);put32(out+28,15360);memcpy(out+32,m->id,33);memcpy(out+68,m->key_id,33);memcpy(out+104,m->sha256,32);memcpy(out+136,m->signature,64);
}
static bool manifest_decode(const unsigned char in[512],h2h_resource_manifest_t *m){
 if(memcmp(in,"PKSIG02\0",8)||get32(in+24)!=H2H_RESOURCE_BYTES||get32(in+28)!=15360)return false;
 memset(m,0,sizeof(*m));m->kind=get32(in+8);m->version=get32(in+12);m->min_firmware=get32(in+16);m->max_firmware=get32(in+20);
 memcpy(m->id,in+32,33);memcpy(m->key_id,in+68,33);memcpy(m->sha256,in+104,32);memcpy(m->signature,in+136,64);
 unsigned char canonical[512];manifest_encode(m,canonical);return !memcmp(in,canonical,512);
}
bool h2h_resource_manifest_valid(const h2h_resource_manifest_t *m,const h2h_resource_io_t *io){
 if(!m||!io||!id_valid(m->id)||!id_valid(m->key_id)||m->kind<1||m->kind>2||!m->version||!m->min_firmware||
    m->min_firmware>m->max_firmware||H2H_RESOURCE_FIRMWARE<m->min_firmware||H2H_RESOURCE_FIRMWARE>m->max_firmware||
    io->capacity[0]<H2H_RESOURCE_PAYLOAD_OFFSET+H2H_RESOURCE_BYTES||io->capacity[1]<H2H_RESOURCE_PAYLOAD_OFFSET+H2H_RESOURCE_BYTES)return false;
 unsigned char key[32];if(!io->key||!io->key(m->key_id,key,io->ctx))return false;
 char message[512];size_t n=h2h_resource_manifest_message(m,message);
 return n&&crypto_ed25519_check(m->signature,key,(const unsigned char *)message,n)==0;
}
size_t h2h_resource_manifest_message(const h2h_resource_manifest_t *m,char out[512]){
 if(!m||!id_valid(m->id)||!id_valid(m->key_id))return 0;
 char sha[65];for(unsigned i=0;i<32;i++)snprintf(sha+2*i,3,"%02x",m->sha256[i]);
 int n=snprintf(out,512,"[\"pocket-visual-v2\",\"%s\",\"esp32c3-pocket-8m\",2,\"%s\",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%u,15360,\"%s\",\"%s\"]",
  m->kind==1?"member_visual":"group_visual",m->id,m->version,m->min_firmware,m->max_firmware,H2H_RESOURCE_BYTES,sha,m->key_id);
 return n>0&&n<512?(size_t)n:0;
}

static bool label(const unsigned char *header,unsigned offset,unsigned size,char *out,bool id){
 unsigned n=0;while(n<size&&header[offset+n]){if(header[offset+n]<32||header[offset+n]>126)return false;n++;}
 if(!n||n==size)return false;
 for(unsigned i=n;i<size;i++)if(header[offset+i])return false;
 memcpy(out,header+offset,n);out[n]=0;return !id||id_valid(out);
}
bool h2h_resource_labels(const void *bytes,size_t length,const h2h_resource_manifest_t *m,h2h_resource_labels_t *out){
 if(!bytes||length<512||!m||!out)return false;
 const unsigned char *p=bytes;const uint32_t fields[]={2,H2H_RESOURCE_BYTES,15,64,80,15360,4,768,15360};
 if(memcmp(p,"PKVIS02\0",8)||p[48]!=m->kind)return false;
 for(unsigned i=0;i<9;i++)if(get32(p+8+4*i)!=fields[i])return false;
 h2h_resource_labels_t next={0};
 if(!label(p,64,33,next.id,true)||strcmp(next.id,m->id)||!label(p,100,33,next.group,false)||!label(p,136,33,next.member,false)||!label(p,172,65,next.title,false))return false;
 for(unsigned i=0;i<3;i++){next.room_colors[i]=get32(p+240+i*4);if(next.room_colors[i]>0xffffff)return false;}
 for(unsigned i=44;i<512;i++)if(!(i==48||(i>=64&&i<97)||(i>=100&&i<133)||(i>=136&&i<169)||(i>=172&&i<237)||(i>=240&&i<252))&&p[i])return false;
 *out=next;return true;
}
bool h2h_resource_slot_valid(int slot,const h2h_resource_io_t *io,h2h_resource_manifest_t *out){
 if(slot<0||slot>1||!io||!io->read||!io->hash)return false;
 unsigned char header[512],sha[32];h2h_resource_manifest_t m;h2h_resource_labels_t labels;
 if(!io->read(slot,0,header,512,io->ctx)||!manifest_decode(header,&m)||!h2h_resource_manifest_valid(&m,io)||
    !io->hash(slot,H2H_RESOURCE_PAYLOAD_OFFSET,H2H_RESOURCE_BYTES,sha,io->ctx)||memcmp(sha,m.sha256,32)||
    !io->read(slot,H2H_RESOURCE_PAYLOAD_OFFSET,header,512,io->ctx)||!h2h_resource_labels(header,512,&m,&labels))return false;
 if(out)*out=m;
 return true;
}
static int highwater(const h2h_resource_record_t *r,const char *id){for(unsigned i=0;i<r->highwater_count;i++)if(!strcmp(r->highwaters[i].id,id))return (int)i;return -1;}
static bool allowed(const h2h_resource_record_t *r,const h2h_resource_manifest_t *m){int i=highwater(r,m->id);return i<0?r->highwater_count<H2H_RESOURCE_HIGHWATERS:
 m->version>r->highwaters[i].version||(m->version==r->highwaters[i].version&&!memcmp(m->sha256,r->highwaters[i].sha256,32));}
void h2h_resource_resume_encode(const h2h_resource_resume_t *r,unsigned char out[160]){
 memset(out,0,160);memcpy(out,"PKRESUM2",8);put64(out+8,r->epoch);put32(out+16,r->revision);put32(out+20,(uint32_t)r->slot);
 put32(out+24,r->validated_offset);put32(out+28,r->generation);memcpy(out+32,r->sha256,32);memcpy(out+64,r->manifest_sha256,32);memcpy(out+96,r->prefix_sha256,32);put32(out+156,check(out,156));
}
bool h2h_resource_resume_decode(h2h_resource_resume_t *r,const void *bytes,size_t length){
 if(!bytes||length!=160)return false;
 const unsigned char *p=bytes;if(memcmp(p,"PKRESUM2",8)||get32(p+156)!=check(p,156))return false;
 for(unsigned i=128;i<156;i++)if(p[i])return false;
 h2h_resource_resume_t next={.epoch=get64(p+8),.revision=get32(p+16),.slot=(int)get32(p+20),.validated_offset=get32(p+24),.generation=get32(p+28)};
 if(!next.epoch||next.epoch>MAX_EPOCH||!next.revision||next.slot<0||next.slot>1||!next.generation||next.validated_offset>H2H_RESOURCE_BYTES||
    (next.validated_offset%4096&&next.validated_offset!=H2H_RESOURCE_BYTES))return false;
 memcpy(next.sha256,p+32,32);memcpy(next.manifest_sha256,p+64,32);memcpy(next.prefix_sha256,p+96,32);*r=next;return true;
}
static bool resume_commit(h2h_resource_t *r,const h2h_resource_resume_t *next,const h2h_resource_io_t *io){
 unsigned char bytes[160],readback[160];h2h_resource_resume_encode(next,bytes);
 if(!io->alive(io->ctx)||!io->commit_resume(bytes,160,io->ctx)||io->read_resume(readback,160,io->ctx)!=1||memcmp(bytes,readback,160))return false;
 r->resume=*next;return true;
}
bool h2h_resource_transfer_begin(h2h_resource_t *r,const h2h_resource_manifest_t *m,uint64_t epoch,uint32_t revision,bool restart,const h2h_resource_io_t *io){
 r->pending=r->transferring=r->restart_required=false;
 if(!epoch||epoch>MAX_EPOCH||!revision||epoch<r->record.epoch||(epoch==r->record.epoch&&revision<r->record.revision)||!io||!io->alive||!io->alive(io->ctx)||!io->digest||!io->hash||!io->read||!io->write||
    !io->erase_range||!io->read_resume||!io->commit_resume||!h2h_resource_manifest_valid(m,io)||!allowed(&r->record,m))return false;
 char message[512];size_t length=h2h_resource_manifest_message(m,message);unsigned char manifest_hash[32],prefix[32],bytes[160];
 if(!length||!io->digest(message,length,manifest_hash,io->ctx))return false;
 int slot=r->record.active==0?1:0;h2h_resource_resume_t resume={0};int stored=io->read_resume(bytes,160,io->ctx);
 bool valid=stored==1&&h2h_resource_resume_decode(&resume,bytes,160);
 bool matches=valid&&resume.slot==slot&&resume.epoch==epoch&&resume.revision==revision&&!memcmp(resume.sha256,m->sha256,32)&&!memcmp(resume.manifest_sha256,manifest_hash,32)&&
    io->hash(slot,H2H_RESOURCE_PAYLOAD_OFFSET,resume.validated_offset,prefix,io->ctx)&&!memcmp(prefix,resume.prefix_sha256,32);
 /* A completed journal whose exact artifact is already durably active is
  * consumed. A fresh signed later selection starts at zero in the other bank;
  * an unfinished/malformed/mismatched prefix still requires explicit restart. */
 bool consumed=false;
 if(valid&&resume.validated_offset==H2H_RESOURCE_BYTES&&resume.slot==r->record.active&&
    resume.epoch==r->record.epoch&&resume.revision==r->record.revision){
  h2h_resource_manifest_t active;
  if(h2h_resource_slot_valid(resume.slot,io,&active)&&!memcmp(active.sha256,resume.sha256,32)){
   char active_message[512];unsigned char active_hash[32];size_t active_length=h2h_resource_manifest_message(&active,active_message);
   consumed=active_length&&io->digest(active_message,active_length,active_hash,io->ctx)&&!memcmp(active_hash,resume.manifest_sha256,32);
  }
 }
 if(stored<0)return false;
 if(stored!=0&&!matches&&!restart&&!consumed){r->restart_required=true;return false;}
 if(!matches||restart){
  if(valid&&resume.generation==UINT32_MAX)return false;
  h2h_resource_resume_t next={.epoch=epoch,.revision=revision,.slot=slot,.generation=valid?resume.generation+1:1};
  memcpy(next.sha256,m->sha256,32);memcpy(next.manifest_sha256,manifest_hash,32);
  if(!io->digest("",0,next.prefix_sha256,io->ctx)||!resume_commit(r,&next,io))return false;
 }else r->resume=resume;
 r->staged=*m;r->staged_slot=slot;r->staged_epoch=epoch;r->staged_revision=revision;r->cursor=r->resume.validated_offset;r->transferring=true;return true;
}
bool h2h_resource_transfer_chunk(h2h_resource_t *r,uint64_t epoch,uint32_t revision,const unsigned char sha[32],uint32_t offset,
                                  const void *bytes,size_t length,const unsigned char chunk_sha[32],const h2h_resource_io_t *io){
 if(!r->transferring||!bytes||!sha||!chunk_sha||epoch!=r->resume.epoch||revision!=r->resume.revision||memcmp(sha,r->resume.sha256,32)||
    offset!=r->cursor||!length||length>1024||offset>H2H_RESOURCE_BYTES||length>H2H_RESOURCE_BYTES-offset||
    length>4096-offset%4096||r->staged_slot==r->record.active||!io->alive(io->ctx))return false;
 unsigned char digest[32],readback[1024];
 if(!io->digest(bytes,length,digest,io->ctx)||memcmp(digest,chunk_sha,32))return false;
 if(offset%4096==0&&!io->erase_range(r->staged_slot,H2H_RESOURCE_PAYLOAD_OFFSET+offset,4096,io->ctx)){r->transferring=false;return false;}
 if(!io->write(r->staged_slot,H2H_RESOURCE_PAYLOAD_OFFSET+offset,bytes,length,io->ctx)||
    !io->read(r->staged_slot,H2H_RESOURCE_PAYLOAD_OFFSET+offset,readback,length,io->ctx)||memcmp(bytes,readback,length)){r->transferring=false;return false;}
 r->cursor+=(uint32_t)length;
 if(r->cursor%4096==0||r->cursor==H2H_RESOURCE_BYTES){
  h2h_resource_resume_t next=r->resume;next.validated_offset=r->cursor;
  if(next.generation==UINT32_MAX){r->transferring=false;return false;}next.generation++;
  if(!io->hash(r->staged_slot,H2H_RESOURCE_PAYLOAD_OFFSET,r->cursor,next.prefix_sha256,io->ctx)||!resume_commit(r,&next,io)){r->transferring=false;return false;}
 }
 return true;
}
bool h2h_resource_transfer_finish(h2h_resource_t *r,const h2h_resource_io_t *io){
 unsigned char header[512];h2h_resource_labels_t labels;
 if(!r->transferring||r->staged_slot==r->record.active||r->cursor!=H2H_RESOURCE_BYTES||r->resume.validated_offset!=H2H_RESOURCE_BYTES||
    memcmp(r->resume.prefix_sha256,r->staged.sha256,32)||!io->alive(io->ctx)||!allowed(&r->record,&r->staged)||
    !io->read(r->staged_slot,H2H_RESOURCE_PAYLOAD_OFFSET,header,512,io->ctx)||!h2h_resource_labels(header,512,&r->staged,&labels))return false;
 if(!io->erase_range(r->staged_slot,0,4096,io->ctx))return false;
 manifest_encode(&r->staged,header);
 if(!io->write(r->staged_slot,0,header,512,io->ctx)||!h2h_resource_slot_valid(r->staged_slot,io,NULL))return false;
 r->transferring=false;r->pending=true;return true;
}
bool h2h_resource_stage(h2h_resource_t *r,const h2h_resource_manifest_t *m,uint64_t epoch,uint32_t revision,const h2h_resource_io_t *io){
 if(io&&io->read_resume&&io->commit_resume){
  if(!h2h_resource_transfer_begin(r,m,epoch,revision,false,io))return false;
  unsigned char buffer[1024],sha[32];
  while(r->cursor<H2H_RESOURCE_BYTES){size_t n=H2H_RESOURCE_BYTES-r->cursor;if(n>1024)n=1024;
   if(!io->fetch||!io->fetch(r->cursor,buffer,n,io->ctx)||!io->digest(buffer,n,sha,io->ctx)||
      !h2h_resource_transfer_chunk(r,epoch,revision,m->sha256,r->cursor,buffer,n,sha,io))return false;
  }return h2h_resource_transfer_finish(r,io);
 }
 r->pending=false;
 if(!epoch||epoch>MAX_EPOCH||!revision||epoch<r->record.epoch||(epoch==r->record.epoch&&revision<r->record.revision)||!h2h_resource_manifest_valid(m,io)||!allowed(&r->record,m)||!io->alive||!io->alive(io->ctx)||!io->fetch||!io->erase||!io->write)return false;
 int slot=r->record.active==0?1:0;unsigned char buffer[1024];
 if(!io->erase(slot,io->ctx))return false;
 for(size_t offset=0;offset<H2H_RESOURCE_BYTES;){size_t n=H2H_RESOURCE_BYTES-offset;if(n>sizeof(buffer))n=sizeof(buffer);
  if(!io->alive(io->ctx)||!io->fetch(offset,buffer,n,io->ctx)||!io->write(slot,H2H_RESOURCE_PAYLOAD_OFFSET+offset,buffer,n,io->ctx))return false;
  offset+=n;
 }
 unsigned char sha[32];h2h_resource_labels_t labels;
 if(!io->alive(io->ctx)||!io->hash||!io->hash(slot,H2H_RESOURCE_PAYLOAD_OFFSET,H2H_RESOURCE_BYTES,sha,io->ctx)||memcmp(sha,m->sha256,32)||
    !io->read(slot,H2H_RESOURCE_PAYLOAD_OFFSET,buffer,512,io->ctx)||!h2h_resource_labels(buffer,512,m,&labels))return false;
 manifest_encode(m,buffer);if(!io->write(slot,0,buffer,512,io->ctx)||!h2h_resource_slot_valid(slot,io,NULL))return false;
 r->staged=*m;r->staged_slot=slot;r->staged_epoch=epoch;r->staged_revision=revision;r->pending=true;return true;
}
static bool commit(h2h_resource_t *r,const h2h_resource_record_t *next,const h2h_resource_io_t *io){
 unsigned char bytes[H2H_RESOURCE_STATE_BYTES];h2h_resource_encode(next,bytes);
 if(!io->alive||!io->alive(io->ctx)||!io->commit||!io->commit(bytes,sizeof(bytes),io->ctx))return false;
 r->record=*next;return true;
}
bool h2h_resource_publish(h2h_resource_t *r,uint64_t epoch,const h2h_resource_io_t *io){
 if(!r->pending||r->record.generation==UINT32_MAX||epoch<r->record.epoch||epoch!=r->staged_epoch||!allowed(&r->record,&r->staged)||!h2h_resource_slot_valid(r->staged_slot,io,NULL))return false;
 h2h_resource_record_t next=r->record;next.previous=next.epoch==epoch?next.active:-1;next.active=r->staged_slot;next.epoch=epoch;next.revision=r->staged_revision;++next.generation;
 int i=highwater(&next,r->staged.id);if(i<0)i=(int)next.highwater_count++;
 strcpy(next.highwaters[i].id,r->staged.id);next.highwaters[i].version=r->staged.version;memcpy(next.highwaters[i].sha256,r->staged.sha256,32);
 if(!commit(r,&next,io))return false;
 r->pending=false;return true;
}
bool h2h_resource_builtin(h2h_resource_t *r,uint64_t epoch,uint32_t revision,const h2h_resource_io_t *io){
 if(!epoch||epoch>MAX_EPOCH||r->record.generation==UINT32_MAX||epoch<r->record.epoch||(epoch==r->record.epoch&&revision<r->record.revision))return false;
 h2h_resource_record_t next=r->record;next.previous=next.epoch==epoch?next.active:-1;next.active=-1;next.epoch=epoch;next.revision=revision;++next.generation;
 if(!commit(r,&next,io))return false;
 r->pending=false;return true;
}
bool h2h_resource_recover(h2h_resource_t *r,uint64_t epoch,const h2h_resource_io_t *io){
 r->pending=false;if(r->record.epoch!=epoch)return false;
 if(r->record.active<0)return true;
 if(h2h_resource_slot_valid(r->record.active,io,NULL))return true;
 return h2h_resource_rollback(r,epoch,io);
}
bool h2h_resource_rollback(h2h_resource_t *r,uint64_t epoch,const h2h_resource_io_t *io){
 if(r->record.epoch!=epoch||r->record.generation==UINT32_MAX)return false;
 h2h_resource_record_t next=r->record;next.active=h2h_resource_slot_valid(next.previous,io,NULL)?next.previous:-1;next.previous=-1;++next.generation;
 return commit(r,&next,io);
}
int h2h_resource_active(const h2h_resource_t *r,uint64_t epoch){return r->record.epoch==epoch?r->record.active:-1;}
