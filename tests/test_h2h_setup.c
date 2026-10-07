#include "h2h_setup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static h2h_setup_gesture_t gesture;
static uint64_t now;
static unsigned entered;
static void level(int key,unsigned duration) {
    for(unsigned i=0;i<duration;i+=20) { now+=20; if(h2h_setup_gesture_step(&gesture,key,now)) ++entered; }
}
static void reset(void) { now=0; entered=0; h2h_setup_gesture_init(&gesture,now); level(-1,140); }
static void click(int key) { level(key,140); level(-1,180); }
static void sequence(void) { click(0);click(1);click(0);click(1); }
static void release_after_poll_gap(unsigned held_ms) {
    reset();sequence();
    now+=20;h2h_setup_gesture_step(&gesture,2,now);
    const uint64_t press=now;
    now+=40;h2h_setup_gesture_step(&gesture,2,now);
    now=press+1000;h2h_setup_gesture_step(&gesture,2,now);
    /* No app-loop poll at the threshold, e.g. radio/UI work overlaps release. */
    now=press+held_ms;h2h_setup_gesture_step(&gesture,-1,now);
    now+=40;h2h_setup_gesture_step(&gesture,-1,now);
    assert(gesture.pending==(held_ms>=3000));
    click(2);assert(entered==(held_ms>=3000));
}
static void sampled_release(unsigned held_ms){
    reset();sequence();now+=20;uint64_t press=now;
    assert(!h2h_setup_gesture_sample(&gesture,2,press,press,100,press));
    now=press+40;assert(!h2h_setup_gesture_sample(&gesture,2,press,now,100,now));
    now=press+1000;assert(!h2h_setup_gesture_sample(&gesture,2,press,now,100,now));
    /* The timer sees the actual release; the app sees it120ms later. */
    now=press+held_ms+120;
    assert(!h2h_setup_gesture_sample(&gesture,-1,press+held_ms,now,101,now));
    assert(gesture.pending==(held_ms>=3000));
    now+=200;uint64_t confirm=now;
    assert(!h2h_setup_gesture_sample(&gesture,2,confirm,now,102,now));
    now+=60;assert(!h2h_setup_gesture_sample(&gesture,2,confirm,now,102,now));
    now=confirm+140;assert(!h2h_setup_gesture_sample(&gesture,-1,now,now,103,now));
    now+=60;assert(h2h_setup_gesture_sample(&gesture,-1,confirm+140,now,103,now)==(held_ms>=3000));
}
static void sampled_missed_events(void){
    reset();sequence();now+=20;uint64_t press=now;
    h2h_setup_gesture_sample(&gesture,2,press,now,100,now);now+=60;
    h2h_setup_gesture_sample(&gesture,2,press,now,100,now);assert(gesture.stage==5);
    now+=3100;
    /* Release/repress completed between app polls: raw key is still OK,
     * sequence reveals missing events and prevents a guessed long hold. */
    assert(!h2h_setup_gesture_sample(&gesture,2,now-100,now,102,now));
    assert(!gesture.stage&&!gesture.pending&&!gesture.armed);
    now+=20;assert(!h2h_setup_gesture_sample(&gesture,-1,now,now,103,now));
    now+=120;assert(!h2h_setup_gesture_sample(&gesture,-1,now-120,now,103,now)&&gesture.armed);
    click(2);assert(!entered);
    reset();sequence();assert(!h2h_setup_gesture_sample(&gesture,2,now-200,now-101,200,now));assert(!gesture.stage&&!gesture.pending);
}
int main(void) {
    char url[128];
    assert(h2h_setup_url(url,sizeof(url),"0123456789abcdef0123456789abcdef"));
    assert(!strcmp(url,"https://ai-passport.randomdance.cn/pocket/setup?device_id=0123456789abcdef0123456789abcdef"));
    assert(!h2h_setup_url(url,12,"0123456789abcdef0123456789abcdef") && !url[0]);
    assert(!h2h_setup_url(url,sizeof(url),"device-secret") && !url[0]);
    reset();sequence();level(2,3200);assert(!gesture.pending && !entered);
    level(-1,180);assert(gesture.pending && !entered);click(2);assert(entered==1 && !gesture.pending);
    reset();sequence();level(2,2800);level(-1,180);click(2);assert(!entered && !gesture.pending);
    reset();sequence();level(2,3200);level(-1,180);click(1);click(2);assert(!entered);
    reset();sequence();level(2,3200);level(-1,5400);click(2);assert(!entered);
    reset();click(0);click(0);click(1);click(0);click(1);level(2,3200);level(-1,180);click(2);assert(!entered);
    reset();click(0);level(-1,3400);click(1);click(0);click(1);level(2,3200);level(-1,180);click(2);assert(!entered);
    reset();for(unsigned i=0;i<30;i++){level(0,20);level(-1,20);}assert(!gesture.stage && !entered);
    reset();sequence();level(-2,100);level(2,3400);level(-1,180);click(2);assert(!entered);
    now=0;entered=0;h2h_setup_gesture_init(&gesture,0);level(0,4000);level(-1,140);
    click(1);click(0);click(1);level(2,3200);level(-1,180);click(2);assert(!entered);
    reset();sequence();level(2,3200);level(-1,180);level(2,700);level(-1,180);assert(!entered);
    release_after_poll_gap(2999);
    release_after_poll_gap(3000);
    release_after_poll_gap(3100);
    sampled_release(2999);
    sampled_release(3000);
    sampled_release(3100);
    sampled_missed_events();
    puts("Setup gesture: release/confirm, short/held/boot keys, timeout, cancellation, bounce, ADC faults and public URL PASS");
}
