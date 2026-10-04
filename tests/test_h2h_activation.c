#include "h2h_model.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    h2h_model_t m;
    for(unsigned page=H2H_HOME;page<=H2H_ACCOUNT;page++) for(unsigned key=H2H_UP;key<=H2H_OK;key++) for(unsigned event=H2H_PRESS;event<=H2H_LONG;event++) {
        h2h_init(&m,2,1,1); h2h_activation(&m,false);
        m.page=(h2h_page_t)page; m.want_play=true; m.countdown_ms=1; m.game_ms=1; m.game_paused=false; m.game_settled=false;
        h2h_input(&m,(h2h_key_t)key,(h2h_event_t)event); h2h_tick(&m,120000);
        h2h_start_track(&m,0); h2h_audio_end(&m,m.generation,false); h2h_listened(&m,120000);
        assert(m.page==H2H_ACCOUNT && !m.want_play && !m.countdown_ms && m.save.hearts==0 && m.save.listen_ms==0);
    }
    h2h_init(&m,2,1,1); h2h_activation(&m,false);
    assert(h2h_menu_count(&m)==5);
    h2h_input(&m,H2H_DOWN,H2H_PRESS); assert(m.selection==1);
    h2h_activation(&m,true); assert(m.page==H2H_HOME);
    h2h_start_track(&m,0); assert(m.want_play); h2h_listened(&m,120000); assert(m.save.hearts==1);
    unsigned generation=m.generation;
    h2h_activation(&m,false); assert(m.generation!=generation && !m.want_play);
    h2h_audio_end(&m,generation,false); assert(!m.want_play && m.save.hearts==1);
    puts("Activation gate: all pages/buttons/timers/rewards/audio callbacks PASS");
}
