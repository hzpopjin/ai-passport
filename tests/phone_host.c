#define _POSIX_C_SOURCE 200809L
#include "h2h_phone.h"
#include "cJSON.h"
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t profile_flash[32768];static pocket_profile_t profile;static bool profile_enabled;
static bool profile_read(uint32_t off,void *b,size_t n,void *ctx){(void)ctx;if(off>32768||n>32768-off)return false;memcpy(b,profile_flash+off,n);return true;}
static bool profile_write(uint32_t off,const void *b,size_t n,void *ctx){(void)ctx;if(off>32768||n>32768-off)return false;for(size_t i=0;i<n;i++){if((profile_flash[off+i]&((const uint8_t *)b)[i])!=((const uint8_t *)b)[i])return false;profile_flash[off+i]&=((const uint8_t *)b)[i];}return true;}
static bool profile_erase(uint32_t off,size_t n,void *ctx){(void)ctx;if(off%16384||n!=16384||off>16384)return false;memset(profile_flash+off,255,n);return true;}
static bool profile_sha(uint32_t off,size_t n,uint8_t b[32],void *ctx){(void)ctx;return off<=32768&&n<=32768-off&&SHA256(profile_flash+off,n,b)!=NULL;}
static const pocket_profile_io_t profile_io={profile_read,profile_write,profile_erase,profile_sha,NULL};
static uint8_t banks[2][1024],ptr[32];static bool exists[3];static int fail;
static int read_bank(int bank,void *bytes,size_t length,void *ctx){(void)ctx;if(!exists[bank])return 0;memcpy(bytes,bank==2?ptr:banks[bank],length);return 1;}
static bool write_bank(int bank,const void *bytes,size_t length,void *ctx){(void)ctx;if(fail>0&&--fail==0)return false;memcpy(bank==2?ptr:banks[bank],bytes,length);exists[bank]=true;return true;}
static bool sha(const void *b,size_t n,uint8_t out[32]){return SHA256(b,n,out)!=NULL;}
int main(void){
 h2h_pocket_t p;h2h_fullsave_t f;h2h_pocket_init(&p,12,8);h2h_fullsave_init(&f);
 profile_enabled=getenv("POCKET_PROFILE_TEST")!=NULL;memset(profile_flash,255,sizeof(profile_flash));(void)pocket_profile_open(&profile,profile_enabled?&profile_io:NULL);
 const h2h_fullsave_io_t io={read_bank,write_bank,NULL};h2h_phone_t phone={.pocket=&p,.full=&f,.io=&io,.device="11111111111111111111111111111111",.sha=sha,.profile=&profile};
 char *line=NULL;size_t capacity=0;ssize_t length;
 while((length=getline(&line,&capacity,stdin))>0){while(length>0&&(line[length-1]=='\n'||line[length-1]=='\r'))line[--length]=0;
  cJSON *request=cJSON_Parse(line),*control=cJSON_GetObjectItemCaseSensitive(request,"test_control");
  if(cJSON_IsString(control)){
   bool restored=false;
   if(!strcmp(control->valuestring,"restore"))restored=h2h_fullsave_restore(&f,&p,&io);
   if(!strcmp(control->valuestring,"reboot")){h2h_pocket_init(&p,12,8);(void)h2h_fullsave_load(&f,&p,&io);(void)pocket_profile_open(&profile,profile_enabled?&profile_io:NULL);}
   if(!strcmp(control->valuestring,"unbind")){uint8_t bytes[384];h2h_pocket_record_t owner=p.record;(void)h2h_ownership_unbind(&owner.ownership,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");h2h_pocket_encode(&owner,bytes);(void)h2h_fullsave_pocket_write(&f,&p,bytes,384,&io);}
   if(!strcmp(control->valuestring,"fail_bank"))fail=1;
   if(!strcmp(control->valuestring,"fail_pointer"))fail=2;
   if(!strcmp(control->valuestring,"advance")){stayc_game_record_t record;if(stayc_game_record_decode(&record,f.local,164)){
       record.gameplay.star_balance++;record.gameplay.checksum=0;stayc_game_t game;stayc_game_init(&game,12,8,1);
       /* Snapshot regenerates checksum after changing a valid runtime model. */
       stayc_game_record_decode(&record,f.local,164);if(stayc_game_load(&game,&record.gameplay,sizeof(record.gameplay))){game.save.star_balance++;stayc_game_save_t save;stayc_game_save_snapshot(&game,&save);(void)h2h_fullsave_local(&f,&p,&save,record.settled_claims,&io);}
   }}
   printf("{\"test_control\":true,\"restored\":%s}\n",restored?"true":"false");
  }else{char out[4097];size_t n=h2h_phone_rpc(&phone,line,(size_t)length,out);if(n)puts(out);else puts("{\"test_error\":true}");}
  cJSON_Delete(request);fflush(stdout);
 }
 free(line);return 0;
}
