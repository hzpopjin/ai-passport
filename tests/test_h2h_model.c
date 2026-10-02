#include "h2h_model.h"
#include "h2h_motion.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void click(h2h_model_t *m) { h2h_input(m,H2H_OK,H2H_PRESS); h2h_input(m,H2H_OK,H2H_CLICK); }
int main(void) {
    h2h_model_t m; h2h_init(&m,1,1,7);
    assert(m.page==H2H_HOME && m.save.volume==35);
    m.page=H2H_SETTINGS; assert(h2h_menu_count(&m)==4);
    m.selection=3; click(&m); assert(m.page==H2H_ACCOUNT && h2h_menu_count(&m)==3);
    h2h_input(&m,H2H_OK,H2H_PRESS); h2h_input(&m,H2H_OK,H2H_LONG);
    assert(m.page==H2H_SETTINGS);
    m.page=H2H_MUSIC;assert(h2h_menu_count(&m)==4);m.selection=2;click(&m);assert(m.save.mode==1);
    m.save.mode=0;m.page=H2H_HOME;m.selection=0;
    h2h_input(&m,H2H_UP,H2H_PRESS); assert(m.selection==5);
    h2h_input(&m,H2H_UP,H2H_CLICK); assert(m.selection==5);
    h2h_input(&m,H2H_DOWN,H2H_PRESS); click(&m); assert(m.page==H2H_ROOM);
    h2h_input(&m,H2H_OK,H2H_PRESS); h2h_input(&m,H2H_OK,H2H_LONG);
    h2h_input(&m,H2H_OK,H2H_CLICK); assert(m.page==H2H_HOME);
    m.save.countdown=1; h2h_start_track(&m,0); assert(!m.want_play && m.countdown_ms==5000);
    h2h_tick(&m,4999); assert(!m.want_play); h2h_tick(&m,1); assert(m.want_play);
    m.page=H2H_PLAYER; click(&m); assert(!m.want_play);
    h2h_tick(&m,240000); assert(m.save.hearts==0);
    h2h_listened(&m,119999); assert(m.save.hearts==0);
    h2h_listened(&m,1); assert(m.save.hearts==1 && !m.save.listen_ms);
    h2h_listened(&m,39*120000); assert(m.save.hearts==40 && h2h_card_count(&m)==8);
    assert(h2h_outfit_count(&m)==3 && h2h_room_count(&m)==3);
    m.page=H2H_COLLECTION; m.selection=8; click(&m); assert(m.save.outfit==1);
    m.selection=9; click(&m); assert(m.save.room==1);
    assert(h2h_menu_count(&m)==10);
    h2h_save_t save; h2h_save_snapshot(&m,&save);
    h2h_model_t restored; h2h_init(&restored,1,1,4); assert(h2h_load(&restored,&save,sizeof(save)));
    assert(restored.save.hearts==40); save.hearts++; assert(!h2h_load(&restored,&save,sizeof(save)));
    assert(!h2h_load(&restored,&save,sizeof(save)-1));
    for(unsigned i=0;i<3;i++) { m.save.mode=i; h2h_audio_end(&m,m.generation,false); assert(m.track==0); }
    m.track_count=m.song_count=3; m.save.mode=1;
    for(unsigned i=0;i<100;i++) { unsigned old=m.track; h2h_audio_end(&m,m.generation,false); assert(m.track!=old && m.track<3); }
    unsigned generation=m.generation; h2h_audio_end(&m,generation-1,true); assert(!m.audio_error);
    h2h_audio_end(&m,generation,true); assert(m.audio_error && !m.want_play);
    m.page=H2H_PLAYER; click(&m); assert(!m.audio_error && m.generation==generation+1);
    h2h_input(&m,H2H_OK,H2H_LONG); assert(m.countdown_ms==0 && !m.want_play);
    m.page=H2H_HOME; m.selection=2; click(&m); assert(m.page==H2H_GAME);
    click(&m); h2h_tick(&m,10000); assert(m.game_ms==45000);
    click(&m); m.caught=100; h2h_tick(&m,45000); assert(m.page==H2H_RESULT && m.game_reward==5);
    unsigned hearts=m.save.hearts; h2h_tick(&m,45000); assert(m.save.hearts==hearts);
    click(&m); m.caught=10; h2h_input(&m,H2H_OK,H2H_LONG); assert(m.save.hearts==hearts);
    h2h_init(&m,1,1,3); m.page=H2H_PLAYER;m.save.countdown=0;h2h_start_track(&m,0);
    m.duration_ms=163000;m.position_ms=15000;
    unsigned volume=m.save.volume, hearts_before=m.save.hearts;
    h2h_input(&m,H2H_UP,H2H_PRESS);assert(m.save.volume==volume);
    h2h_input(&m,H2H_UP,H2H_LONG);h2h_input(&m,H2H_UP,H2H_CLICK);
    assert(m.start_ms==25000 && m.position_ms==25000 && m.save.volume==volume && m.want_play);
    h2h_input(&m,H2H_DOWN,H2H_PRESS);h2h_input(&m,H2H_DOWN,H2H_CLICK);
    assert(m.save.volume==volume && m.tap_count==1);
    h2h_tick(&m,799);assert(m.save.volume==volume);
    h2h_tick(&m,1);assert(m.save.volume==volume-5);
    m.want_play=false;
    h2h_input(&m,H2H_DOWN,H2H_PRESS);h2h_input(&m,H2H_DOWN,H2H_LONG);h2h_input(&m,H2H_DOWN,H2H_CLICK);
    assert(m.position_ms==15000 && !m.want_play && m.save.volume==volume-5);
    h2h_seek_relative(&m,-100000);assert(!m.position_ms);
    h2h_seek_relative(&m,1000000);assert(m.position_ms==162980 && m.start_ms==162980);
    generation=m.generation;h2h_audio_end(&m,generation-1,false);assert(m.generation==generation);
    assert(m.save.hearts==hearts_before && !m.save.listen_ms);
    m.save.countdown=1;h2h_start_track(&m,0);h2h_seek_relative(&m,10000);assert(!m.countdown_ms && !m.want_play);
    m.pose_ms=2000;m.pose=H2H_HEART;
    for(unsigned i=0;i<100;i++) { h2h_tick(&m,10);h2h_motion_t a=h2h_motion(&m,false);assert(a.pose==H2H_HEART && a.hearts && a.dy>=-1); }
    m.pose_ms=0;unsigned poses=0;
    for(unsigned i=0;i<12;i++) { m.animation_ms=i*240;h2h_motion_t a=h2h_motion(&m,true);poses|=1u<<a.pose;assert(a.dx>=-3 && a.dx<=3 && a.dy>=-4 && a.dy<=0); }
    assert((poses&(1u<<H2H_DANCE)) && (poses&(1u<<H2H_HEART)) && (poses&(1u<<H2H_CELEBRATE)));
    h2h_init(&m,14,7,9);
    m.page=H2H_MUSIC; assert(h2h_menu_count(&m)==5);
    m.selection=2; click(&m); assert(h2h_chorus_mode(&m) && m.track==7 && !m.want_play);
    m.selection=1; click(&m); assert(m.page==H2H_TRACKS && h2h_menu_count(&m)==7);
    m.selection=2; click(&m); assert(m.page==H2H_PLAYER && m.track==9 && m.want_play);
    m.save.mode=0; h2h_audio_end(&m,m.generation,false); assert(m.track==10);
    m.save.mode=2; h2h_audio_end(&m,m.generation,false); assert(m.track==10);
    m.save.mode=1; for(unsigned i=0;i<50;i++) { h2h_audio_end(&m,m.generation,false); assert(m.track>=7 && m.track<14); }
    m.save.countdown=2; m.save.mode=2; h2h_start_track(&m,0); m.duration_ms=30000;
    for(unsigned i=0;i<3;i++) { h2h_input(&m,H2H_DOWN,H2H_PRESS);h2h_input(&m,H2H_DOWN,H2H_CLICK);if(i<2) h2h_tick(&m,300); }
    assert(m.track==8 && m.position_ms==0 && m.want_play && !m.tap_count && m.save.volume==35);
    m.want_play=false;
    for(unsigned i=0;i<3;i++) { h2h_input(&m,H2H_UP,H2H_PRESS);h2h_input(&m,H2H_UP,H2H_CLICK); }
    assert(m.track==7 && !m.want_play);
    for(unsigned i=0;i<3;i++) { h2h_input(&m,H2H_UP,H2H_PRESS);h2h_input(&m,H2H_UP,H2H_CLICK); }
    assert(m.track==13 && !m.want_play);
    m.position_ms=20000; m.start_ms=0;
    h2h_input(&m,H2H_UP,H2H_PRESS);h2h_input(&m,H2H_UP,H2H_CLICK);
    h2h_input(&m,H2H_DOWN,H2H_PRESS);h2h_input(&m,H2H_DOWN,H2H_CLICK);
    assert(m.save.volume==40 && m.tap_count==1); /* Opposite direction completes the earlier volume change. */
    h2h_tick(&m,800);assert(m.save.volume==35 && !m.tap_count);
    h2h_input(&m,H2H_UP,H2H_PRESS);h2h_input(&m,H2H_UP,H2H_CLICK);
    h2h_input(&m,H2H_DOWN,H2H_PRESS);h2h_input(&m,H2H_DOWN,H2H_LONG);
    assert(m.save.volume==40 && m.position_ms==10000 && !m.tap_count);
    h2h_input(&m,H2H_OK,H2H_PRESS);
    h2h_input(&m,H2H_OK,H2H_LONG);assert(m.page==H2H_MUSIC);
    h2h_save_snapshot(&m,&save);h2h_init(&restored,14,7,4);
    assert(h2h_load(&restored,&save,sizeof(save)) && h2h_chorus_mode(&restored) && restored.track==7);
    m.page=H2H_MUSIC;m.selection=2;m.want_play=false;click(&m);
    assert(!h2h_chorus_mode(&m) && m.track<7 && !m.want_play);
    h2h_model_t taps; h2h_init(&taps,3,3,1); taps.page=H2H_PLAYER; h2h_start_track(&taps,0);
    for(unsigned i=0;i<2;i++) { h2h_input(&taps,H2H_UP,H2H_PRESS); h2h_input(&taps,H2H_UP,H2H_CLICK); }
    assert(taps.save.volume==35 && taps.track==0 && taps.tap_count==2);
    h2h_tick(&taps,799); assert(taps.save.volume==35 && taps.tap_count==2);
    h2h_tick(&taps,1); assert(taps.save.volume==45 && taps.track==0 && !taps.tap_count);
    h2h_init(&m,0,0,3); h2h_start_track(&m,0); assert(!m.want_play);
    puts("H2H model: navigation, long press, countdown, rewards, shuffle, game, save validation PASS");
}
