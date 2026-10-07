#include "h2h_setup.h"
#include <stdio.h>
#include <string.h>

void h2h_setup_gesture_init(h2h_setup_gesture_t *g, uint64_t now) {
    *g=(h2h_setup_gesture_t){.candidate=-2,.stable=-2,.changed_ms=now};
}
static void cancel(h2h_setup_gesture_t *g) {
    g->stage=0; g->pending=false; g->deadline_ms=0;
}
static bool gesture_step(h2h_setup_gesture_t *g,int key,uint64_t changed,uint64_t now) {
    if(key < -1 || key > 2) { cancel(g); g->armed=false; g->stable=g->candidate=-2; return false; }
    if(g->stage && now>g->deadline_ms) cancel(g);
    if(key!=g->candidate) { g->candidate=key; g->changed_ms=changed; }
    if(now-g->changed_ms<40) return false;
    if(!g->armed) {
        if(key==-1 && now-g->changed_ms>=100) { g->armed=true; g->stable=-1; }
        return false;
    }
    if(key==g->stable) {
        if(g->stage==5 && key==2 && now-g->pressed_ms>=3000) {
            g->stage=6; g->deadline_ms=now+3000;
        }
        return false;
    }
    int previous=g->stable; g->stable=key;
    if(key!=-1) {
        g->pressed_ms=g->changed_ms;
        if(previous!=-1) { cancel(g); return false; }
        if(g->stage==4 && key==2) { g->stage=5; g->deadline_ms=now+6000; }
        else if(g->stage==7 && key==2) { g->stage=8; g->deadline_ms=now+5000; }
        else if(g->stage>=4) cancel(g);
        return false;
    }
    if(previous<0) return false;
    const uint64_t held_ms=g->changed_ms-g->pressed_ms;
    /* A completed hold may be released before the app polls the threshold.
     * Use the debounced transition timestamps without counting release time. */
    if(g->stage==5 && previous==2 && held_ms>=3000) g->stage=6;
    if(g->stage==6) { g->stage=7; g->pending=true; g->deadline_ms=now+5000; return false; }
    if(g->stage==8 && previous==2 && held_ms<500) { cancel(g); return true; }
    if(g->stage>=4) { cancel(g); return false; }
    const int sequence[]={0,1,0,1};
    if(previous==sequence[g->stage] && held_ms<1000) {
        ++g->stage; g->deadline_ms=now+3000;
    } else cancel(g);
    return false;
}
bool h2h_setup_gesture_step(h2h_setup_gesture_t *g,int key,uint64_t now){
    return gesture_step(g,key,key!=g->candidate?now:g->changed_ms,now);
}
bool h2h_setup_gesture_sample(h2h_setup_gesture_t *g,int key,uint64_t changed,
                              uint64_t sampled,uint64_t sequence,uint64_t now){
    bool valid=key>=-1&&key<=2&&sequence>0&&changed<=sampled&&sampled<=now&&now-sampled<=100;
    if(g->sampled)valid=valid&&sampled>=g->sample_now_ms&&sequence>=g->sample_sequence&&
        sequence-g->sample_sequence<=1&&(sequence!=g->sample_sequence||key==g->sample_key);
    g->sampled=true;g->sample_sequence=sequence;g->sample_now_ms=sampled;g->sample_key=key;
    if(!valid){cancel(g);g->armed=false;g->stable=g->candidate=-2;return false;}
    return gesture_step(g,key,changed,now);
}
bool h2h_setup_url(char *out, size_t size, const char *id) {
    if(!out || !size) return false;
    out[0]=0;
    if(!id || strlen(id)!=32) return false;
    for(unsigned i=0;i<32;i++) if(!((id[i]>='0' && id[i]<='9') || (id[i]>='a' && id[i]<='f'))) return false;
    int length=snprintf(out,size,"https://ai-passport.randomdance.cn/pocket/setup?device_id=%s",id);
    if(length<0 || (size_t)length>=size) { out[0]=0; return false; }
    return true;
}
