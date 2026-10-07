#include "stayc_game.h"
#include "bsp_button.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 assert((int)BSP_BTN_UP==(int)STAYC_GAME_UP&&(int)BSP_BTN_DOWN==(int)STAYC_GAME_DOWN&&(int)BSP_BTN_OK==(int)STAYC_GAME_OK);
 stayc_game_t m;stayc_game_init(&m,12,6,1);
 /* The discoverable account shortcut is a native UI extension, not a save
  * migration or an ownership bypass. Both origins return to their own focus. */
 stayc_game_save_t navigation_before,navigation_after;
 stayc_game_save_snapshot(&m,&navigation_before);
 assert(m.page==STAYC_GAME_HOME&&stayc_game_menu_count(&m)==6);
 stayc_game_account_open(&m,true);
 assert(m.page==STAYC_GAME_ACCOUNT&&m.account_from_home);
 stayc_game_account_back(&m);assert(m.page==STAYC_GAME_HOME&&m.selection==6);
 m.selection=0;
 m.selection=5;stayc_game_input(&m,STAYC_GAME_OK,STAYC_GAME_CLICK);
 assert(m.page==STAYC_GAME_SETTINGS);m.selection=3;
 stayc_game_input(&m,STAYC_GAME_OK,STAYC_GAME_CLICK);
 assert(m.page==STAYC_GAME_ACCOUNT&&!m.account_from_home);
 stayc_game_account_back(&m);
 assert(m.page==STAYC_GAME_SETTINGS&&m.selection==3);
 stayc_game_save_snapshot(&m,&navigation_after);
 assert(!memcmp(&navigation_before,&navigation_after,sizeof(navigation_before))&&!m.dirty);
 stayc_game_init(&m,12,6,1);
 m.page=STAYC_GAME_ARCADE;m.selection=1;
 stayc_game_input(&m,STAYC_GAME_OK,STAYC_GAME_CLICK);
 assert(m.page==STAYC_GAME_ECHO&&m.echo_show&&m.echo_length==2);
 stayc_game_save_t before,after;stayc_game_save_snapshot(&m,&before);
 stayc_game_input(&m,STAYC_GAME_DOWN,STAYC_GAME_PRESS);
 assert(m.echo_feedback_ms==350&&m.echo_feedback_direction==1&&m.echo_feedback==STAYC_GAME_FEEDBACK_WAIT&&!m.echo_input&&!m.echo_mistakes);
 stayc_game_tick(&m,1800);assert(!m.echo_show&&!m.echo_feedback_ms);
 unsigned expected=m.echo_sequence[0];
 stayc_game_input(&m,(stayc_game_key_t)expected,STAYC_GAME_PRESS);
 assert(m.echo_input==1&&m.echo_feedback_direction==expected&&m.echo_feedback==STAYC_GAME_FEEDBACK_CORRECT);
 stayc_game_input(&m,(stayc_game_key_t)expected,STAYC_GAME_CLICK);assert(m.echo_input==1);
 stayc_game_tick(&m,349);assert(m.echo_feedback_ms==1);
 stayc_game_tick(&m,1);assert(m.echo_feedback==STAYC_GAME_FEEDBACK_NONE);
 unsigned wrong=1u-m.echo_sequence[1];
 stayc_game_input(&m,(stayc_game_key_t)wrong,STAYC_GAME_PRESS);
 assert(m.echo_mistakes==1&&m.echo_show&&m.echo_feedback_direction==wrong&&m.echo_feedback==STAYC_GAME_FEEDBACK_WRONG);
 stayc_game_tick(&m,350);assert(!m.echo_feedback_ms&&m.echo_feedback==STAYC_GAME_FEEDBACK_NONE);
 stayc_game_save_snapshot(&m,&after);assert(!memcmp(&before,&after,sizeof(before)));
 m.game_paused=true;unsigned mistakes=m.echo_mistakes;
 stayc_game_input(&m,STAYC_GAME_UP,STAYC_GAME_PRESS);
 assert(m.echo_feedback_direction==0&&m.echo_feedback==STAYC_GAME_FEEDBACK_WAIT&&m.echo_mistakes==mistakes);
 stayc_game_tick(&m,1000);assert(!m.echo_feedback_ms);
 m.page=STAYC_GAME_GAME;m.game_paused=false;m.basket=2;
 stayc_game_input(&m,(stayc_game_key_t)BSP_BTN_UP,STAYC_GAME_PRESS);assert(m.basket==1);
 stayc_game_input(&m,(stayc_game_key_t)BSP_BTN_DOWN,STAYC_GAME_PRESS);assert(m.basket==2);
 /* Actual main frame tick separates success from the next demonstration. */
 stayc_game_init(&m,12,6,1);m.page=STAYC_GAME_ARCADE;m.selection=1;
 stayc_game_input(&m,STAYC_GAME_OK,STAYC_GAME_CLICK);stayc_game_tick_frame(&m,1800);
 m.echo_sequence[0]=0;m.echo_sequence[1]=1;m.echo_sequence[2]=0;
 stayc_game_save_snapshot(&m,&before);
 stayc_game_input(&m,STAYC_GAME_UP,STAYC_GAME_PRESS);
 stayc_game_input(&m,STAYC_GAME_DOWN,STAYC_GAME_PRESS);
 assert(m.echo_round==1&&m.echo_round_wait_ms==600&&m.echo_show&&!m.echo_show_ms&&stayc_game_echo_display_cue(&m)==2);
 stayc_game_tick_frame(&m,349);assert(m.echo_feedback_ms==1&&m.echo_round_wait_ms==251&&!m.echo_show_ms&&stayc_game_echo_display_cue(&m)==2);
 stayc_game_tick_frame(&m,1);assert(!m.echo_feedback_ms&&m.echo_round_wait_ms==250&&!m.echo_show_ms&&stayc_game_echo_display_cue(&m)==2);
 m.game_paused=true;stayc_game_tick_frame(&m,2000);assert(m.echo_round_wait_ms==250&&!m.echo_show_ms);m.game_paused=false;
 stayc_game_tick_frame(&m,249);assert(m.echo_round_wait_ms==1&&stayc_game_echo_display_cue(&m)==2);
 stayc_game_tick_frame(&m,1);assert(!m.echo_round_wait_ms&&!m.echo_show_ms&&stayc_game_echo_display_cue(&m)==0);
 stayc_game_tick_frame(&m,2700);assert(!m.echo_show);
 stayc_game_save_snapshot(&m,&after);assert(!memcmp(&before,&after,sizeof(before)));
 for(unsigned i=0;i<3;i++)stayc_game_input(&m,(stayc_game_key_t)m.echo_sequence[i],STAYC_GAME_PRESS);
 assert(m.echo_round_wait_ms==600&&m.echo_length==4);
 stayc_game_tick_frame(&m,1000);assert(!m.echo_round_wait_ms&&m.echo_show_ms==400&&!m.echo_feedback_ms);
 /* Five complete rounds still settle exactly once with the same save/reward. */
 for(unsigned length=4;length<=6;length++){
  stayc_game_tick_frame(&m,length*900);assert(!m.echo_show);
  for(unsigned i=0;i<length;i++)stayc_game_input(&m,(stayc_game_key_t)m.echo_sequence[i],STAYC_GAME_PRESS);
  if(length<6)stayc_game_tick_frame(&m,600);
 }
 assert(m.page==STAYC_GAME_RESULT&&m.game_settled&&m.echo_round==5&&m.score==20&&m.game_reward==5&&m.save.games_played==1);
 stayc_game_save_snapshot(&m,&before);stayc_game_tick_frame(&m,10000);stayc_game_save_snapshot(&m,&after);assert(!memcmp(&before,&after,sizeof(before)));
 puts("STAYC account shortcut/origin/back/save invariants and echo feedback/timing/exactly-once rewards: PASS");
}
