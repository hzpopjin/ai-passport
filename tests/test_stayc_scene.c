#include "stayc_scene.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t out[5];
    const uint8_t literal[]={3,1,2,3,4},run[]={132,5},mixed[]={1,3,4,130,7};
    assert(stayc_unpack(literal,sizeof(literal),out,4)&&!memcmp(out,"\1\2\3\4",4));
    assert(stayc_unpack(run,sizeof(run),out,5)&&out[4]==5);
    assert(stayc_unpack(mixed,sizeof(mixed),out,5)&&out[0]==3&&out[4]==7);
    assert(!stayc_unpack(literal,sizeof(literal)-1,out,4));
    assert(!stayc_unpack(run,sizeof(run),out,4));
    assert(!stayc_unpack(run,1,out,5));
    assert(!stayc_unpack(literal,sizeof(literal),out,5));
    for(unsigned member=0;member<6;member++)for(unsigned outfit=0;outfit<2;outfit++)for(unsigned action=0;action<12;action++) {
        unsigned duration=stayc_action_duration(action);assert(duration>0&&duration<=30000);
        for(unsigned time=0;time<=duration+100;time+=37){stayc_frame_t frame;stayc_sample(member,outfit,action,time,&frame);
            assert(frame.pose<32&&frame.elapsed<=duration&&frame.done==(time>=duration));
            assert(frame.scale_q8==256||frame.scale_q8==384||frame.scale_q8==512);
        }
    }
    stayc_frame_t phone;
    unsigned start=0;
    for(unsigned i=0;i<stayc_actions[0][0][5].count;i++){
        const stayc_phase_t *phase=&stayc_actions[0][0][5].phases[i];
        if(phase->scroll){
            stayc_sample(0,0,5,start+phase->ms/2,&phone);
            assert(phone.scroll&&!phone.flash&&phone.progress_q8==128&&phone.scale_q8==512);
        }
        start+=phase->ms;
    }
    h2h_model_t m;h2h_init(&m,12,6,1);h2h_activation(&m,false);
    stayc_control_t state={0};m.page=H2H_ROOM;m.selection=0;
    assert(!stayc_control_input(&state,&m,H2H_OK,H2H_CLICK)&&state.view==STAYC_NORMAL);
    h2h_activation(&m,true);m.page=H2H_ROOM;
    assert(stayc_control_input(&state,&m,H2H_OK,H2H_CLICK));assert(state.view==STAYC_MEMBER_MENU);
    for(unsigned i=0;i<5;i++)stayc_control_input(&state,&m,H2H_DOWN,H2H_PRESS);
    stayc_control_input(&state,&m,H2H_OK,H2H_CLICK);assert(state.member==5&&state.preferences_dirty);
    m.selection=1;stayc_control_input(&state,&m,H2H_OK,H2H_CLICK);assert(state.view==STAYC_ACTION_MENU);
    stayc_control_input(&state,&m,H2H_OK,H2H_CLICK);assert(state.view==STAYC_ACTION_PLAYER);
    stayc_control_tick(&state,&m,500);assert(state.elapsed==500);
    stayc_control_input(&state,&m,H2H_OK,H2H_CLICK);stayc_control_tick(&state,&m,1000);assert(state.elapsed==500&&state.paused);
    stayc_control_input(&state,&m,H2H_OK,H2H_CLICK);stayc_control_tick(&state,&m,UINT32_MAX);assert(state.elapsed==stayc_action_duration(0));
    stayc_control_input(&state,&m,H2H_OK,H2H_CLICK);assert(state.elapsed==0);
    stayc_control_input(&state,&m,H2H_OK,H2H_LONG);assert(state.view==STAYC_ACTION_MENU);
    stayc_control_input(&state,&m,H2H_OK,H2H_CLICK);assert(state.view==STAYC_ACTION_MENU);
    stayc_control_input(&state,&m,H2H_OK,H2H_PRESS);state.selection=12;
    stayc_control_input(&state,&m,H2H_OK,H2H_CLICK);assert(state.view==STAYC_DECOR_MENU);
    for(unsigned slot=0;slot<4;slot++){
        state.selection=slot;
        for(unsigned value=1;value<=3;value++){stayc_control_input(&state,&m,H2H_OK,H2H_CLICK);assert(((state.decor>>(2*slot))&3u)==value%3);}
    }
    assert(state.preferences_dirty);
    /* Reading old outfit 2 never clamps or rewrites its cloud save. Only an
       explicit STAYC wardrobe input selects one of this group's two outfits. */
    m.page=H2H_COLLECTION;m.selection=8;m.save.outfit=2;m.suppress_ok=false;
    stayc_control_tick(&state,&m,0);assert(m.save.outfit==2);
    stayc_control_input(&state,&m,H2H_OK,H2H_CLICK);assert(m.save.outfit==1);
    stayc_control_input(&state,&m,H2H_OK,H2H_CLICK);assert(m.save.outfit==0);
    h2h_activation(&m,false);stayc_control_tick(&state,&m,100);assert(state.view==STAYC_NORMAL);
    puts("STAYC decoder, all member/action boundaries, pause/cancel and activation gate: PASS");
}
