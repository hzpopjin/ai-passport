#include "h2h_motion.h"
h2h_motion_t h2h_motion(const h2h_model_t *m,bool playing) {
    unsigned t=m->animation_ms;
    h2h_motion_t a={H2H_IDLE,0,0,false};
    if(m->pose_ms) {
        a.pose=m->pose;
        unsigned step=t/180;
        if(a.pose==H2H_WAVE) { a.pose=step%3?H2H_WAVE:H2H_IDLE; a.dx=step%2?1:-1; }
        if(m->pose==H2H_HEART) { a.hearts=true; a.dy=(t/300)%2?-1:0; }
        if(m->pose==H2H_CELEBRATE) { a.pose=step%4==3?H2H_WAVE:H2H_CELEBRATE; a.dy=step%3==1?-4:0; }
    } else if(playing) {
        static const unsigned poses[]={H2H_DANCE,H2H_DANCE,H2H_IDLE,H2H_DANCE,H2H_CELEBRATE,H2H_CELEBRATE,H2H_DANCE,H2H_IDLE,H2H_DANCE,H2H_HEART,H2H_HEART,H2H_DANCE};
        static const int x[]={-3,-2,0,2,3,2,0,-2,-3,-2,0,2};
        static const int y[]={0,-2,0,-2,-4,-2,0,-2,0,-1,-1,-2};
        unsigned frame=(t/240)%12;a.pose=poses[frame];a.dx=x[frame];a.dy=y[frame];a.hearts=a.pose==H2H_HEART;
    } else {
        unsigned phase=t%12000;
        a.dy=(t/700)%2?-1:0;
        if(phase>=9200 && phase<10800) { a.pose=(phase/240)%2?H2H_WAVE:H2H_IDLE; a.dx=1; }
    }
    return a;
}
