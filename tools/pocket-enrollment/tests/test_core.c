#include "enrollment_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *D="112233445566778899aabbccddeeff00",*M="112233445566",*I="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",*N="ffeeddccbbaa99887766554433221100",*O="00112233445566778899aabbccddeeff";
typedef struct {int size,writes;bool failure,committed_on_failure;unsigned char key[32],progress[164];} storage;
static int size(void *c){return ((storage*)c)->size;}
static bool insert(const unsigned char k[32],void *c){storage *s=c;s->writes++;if(!s->failure||s->committed_on_failure){memcpy(s->key,k,32);s->size=32;}return !s->failure;}
static pe_state_t init(storage *m){pe_state_t s;pe_init(&s,D,M,I,N,(pe_store_t){size,insert,m});return s;}
static void confirm(pe_state_t *s){assert(pe_arm(s,D,M,N,O,1000));pe_sample(s,-1,2000,2000,1,2000);pe_sample(s,2,10000,10000,2,10000);pe_sample(s,2,3009000,10000,2,3009000);pe_sample(s,-1,3010000,3010000,3,3010000);assert(s->phase==PE_CONFIRMED);}
int main(void){int count=0;unsigned char key[32];for(int j=0;j<32;j++)key[j]=(unsigned char)j; /* public synthetic ONLY */
 {storage m={.size=-1};memset(m.progress,0x5a,164);pe_state_t s=init(&m);confirm(&s);assert(pe_install(&s,O,key,3020000));assert(m.writes==1&&m.size==32&&!memcmp(m.key,key,32));for(int j=0;j<164;j++)assert(m.progress[j]==0x5a);assert(!pe_install(&s,O,key,3030000)&&m.writes==1);count++;}
 {storage m={.size=32};pe_state_t s=init(&m);assert(s.phase==PE_REFUSED&&!pe_arm(&s,D,M,N,O,1000)&&m.writes==0);count++;}
 {storage m={.size=-2};pe_state_t s=init(&m);assert(s.phase==PE_REFUSED&&m.writes==0);count++;}
 {storage m={.size=-1};pe_state_t s=init(&m);assert(!pe_arm(&s,"00000000000000000000000000000000",M,N,O,1000)&&m.writes==0);count++;}
 {storage m={.size=-1};pe_state_t s=init(&m);assert(!pe_arm(&s,D,"000000000000",N,O,1000)&&m.writes==0);count++;}
 {storage m={.size=-1};pe_state_t s=init(&m);assert(!pe_arm(&s,D,M,"00000000000000000000000000000000",O,1000)&&m.writes==0);count++;}
 {storage m={.size=-1};pe_state_t s=init(&m);assert(pe_arm(&s,D,M,N,O,1000));assert(!pe_install(&s,O,key,2000)&&m.writes==0);count++;}
 {storage m={.size=-1};pe_state_t s=init(&m);assert(pe_arm(&s,D,M,N,O,1000));pe_sample(&s,2,2000,0,1,2000);pe_sample(&s,-1,4000000,4000000,2,4000000);assert(s.phase!=PE_CONFIRMED&&m.writes==0);count++;}
 {storage m={.size=-1};pe_state_t s=init(&m);assert(pe_arm(&s,D,M,N,O,1000));pe_sample(&s,-1,2000,2000,1,2000);pe_sample(&s,2,10000,10000,2,10000);pe_sample(&s,-1,3009000,3009000,3,3129000);assert(s.phase==PE_REFUSED&&m.writes==0);count++;}
 {storage m={.size=-1};pe_state_t s=init(&m);assert(pe_arm(&s,D,M,N,O,1000));pe_sample(&s,-1,2000,2000,1,2000);pe_sample(&s,2,10000,10000,4,10000);assert(s.phase==PE_REFUSED&&m.writes==0);count++;}
 {storage m={.size=-1};pe_state_t s=init(&m);confirm(&s);assert(!pe_install(&s,O,key,60001000)&&m.writes==0);count++;}
 {storage m={.size=-1};pe_state_t s=init(&m);confirm(&s);m.size=32;assert(!pe_install(&s,O,key,3020000)&&m.writes==0);count++;}
 for(int cut=0;cut<2;cut++){storage m={.size=-1,.failure=true,.committed_on_failure=cut};pe_state_t s=init(&m);confirm(&s);assert(!pe_install(&s,O,key,3020000)&&s.phase==PE_UNCERTAIN&&m.writes==1);s=init(&m);assert(s.phase==(cut?PE_REFUSED:PE_READY));count++;}
 {storage m={.size=-1};pe_state_t s=init(&m);confirm(&s);pe_sample(&s,-2,3020000,3020000,4,3020000);assert(s.phase==PE_REFUSED&&m.writes==0);count++;}
 pe_zero(key,32);for(int i=0;i<32;i++)assert(!key[i]);printf("PASS %d device-core synthetic boundary scenarios\n",count);return 0;
}
