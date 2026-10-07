#include "enrollment_core.h"
#include <string.h>
#include <limits.h>

void pe_zero(void *data,size_t n){volatile uint8_t *p=data;while(n--)*p++=0;}
bool pe_hex(const char *s,size_t n){if(!s||strlen(s)!=n)return false;for(size_t i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;return true;}
static bool refuse(pe_state_t *s,const char *e){s->phase=PE_REFUSED;s->error=e;return false;}
static bool live(pe_state_t *s,int64_t now){if(now<s->last_us)return refuse(s,"clock_fault");s->last_us=now;if(now>=s->expires_us)return refuse(s,"operation_expired");return true;}
bool pe_init(pe_state_t *s,const char *device,const char *mac,const char *image,const char *nonce,pe_store_t store){
    memset(s,0,sizeof(*s));s->store=store;s->last_key=-2;
    if(!pe_hex(device,32)||!pe_hex(mac,12)||!pe_hex(image,64)||!pe_hex(nonce,32)||!store.key_size||!store.insert)return refuse(s,"identity_invalid");
    strcpy(s->device,device);strcpy(s->mac,mac);strcpy(s->image,image);strcpy(s->nonce,nonce);
    int size=store.key_size(store.ctx);
    if(size!=-1)return refuse(s,size==-2?"storage_unavailable":"key_already_present");
    s->phase=PE_READY;return true;
}
bool pe_arm(pe_state_t *s,const char *device,const char *mac,const char *nonce,const char *operation,int64_t now){
    if(s->phase!=PE_READY)return false;
    if(!pe_hex(device,32)||!pe_hex(mac,12)||!pe_hex(nonce,32)||!pe_hex(operation,32)||strcmp(device,s->device)||strcmp(mac,s->mac)||strcmp(nonce,s->nonce))return refuse(s,"target_mismatch");
    if(now<0||now>INT64_MAX-60000000)return refuse(s,"clock_fault");
    if(s->store.key_size(s->store.ctx)!=-1)return refuse(s,"key_already_present");
    strcpy(s->operation,operation);s->phase=PE_ARMED;s->armed_us=s->last_us=now;s->expires_us=now+60000000;s->pressed_us=0;s->released=false;return true;
}
void pe_sample(pe_state_t *s,int key,int64_t sampled,int64_t changed,uint64_t sequence,int64_t now){
    if(s->phase!=PE_ARMED&&s->phase!=PE_CONFIRMED)return;
    if(!live(s,now))return;
    if(key<-1||key>2||sampled<0||changed<0||sampled>now||now-sampled>100000||changed>sampled||sampled<s->last_sampled_us||changed<s->last_changed_us||!sequence){refuse(s,"button_sample_invalid");return;}
    if(s->sequence&&(sequence<s->sequence||sequence-s->sequence>1||(sequence==s->sequence&&key!=s->last_key))){refuse(s,"button_transition_lost");return;}
    if(s->sequence&&((sequence==s->sequence&&changed!=s->last_changed_us)||(sequence>s->sequence&&(key==s->last_key||changed<=s->last_changed_us)))){refuse(s,"button_transition_invalid");return;}
    s->sequence=sequence;s->last_key=key;s->last_sampled_us=sampled;s->last_changed_us=changed;
    if(s->phase==PE_CONFIRMED)return;
    if(key==-1){
        if(s->pressed_us&&changed-s->pressed_us>=3000000){s->phase=PE_CONFIRMED;return;}
        s->released=true;s->pressed_us=0;return;
    }
    if(key!=2){s->released=false;s->pressed_us=0;return;}
    if(s->released&&changed>=s->armed_us&&!s->pressed_us)s->pressed_us=changed;
}
bool pe_install(pe_state_t *s,const char *operation,uint8_t key[32],int64_t now){
    bool ok=false;
    if(s->phase!=PE_CONFIRMED||!pe_hex(operation,32)||strcmp(operation,s->operation)||!live(s,now))goto done;
    if(s->store.key_size(s->store.ctx)!=-1){refuse(s,"key_already_present");goto done;}
    /* Any attempted set/commit failure is indeterminate. NVS may have already
     * persisted the blob. Never erase/retry/claim that nothing was written. */
    if(!s->store.insert(key,s->store.ctx)){s->phase=PE_UNCERTAIN;s->error="write_indeterminate";goto done;}
    s->phase=PE_WRITTEN;ok=true;
done:
    /* Caller keeps its new input key only until the receipt MAC is computed;
     * core never loads or returns an existing key. */
    return ok;
}
const char *pe_phase_name(pe_phase_t p){static const char *names[]={"ready","awaiting_button","confirmed","written","indeterminate","refused"};return p<=PE_REFUSED?names[p]:"refused";}
