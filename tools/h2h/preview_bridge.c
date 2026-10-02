#include "h2h_model.h"
#include "h2h_motion.h"
#include <stdio.h>
#include <string.h>
static h2h_model_t model;
void preview_init(unsigned tracks,unsigned songs) { h2h_init(&model,tracks,songs,20260929); }
void preview_input(int key,int event) { if(key>=0 && key<=2 && event>=0 && event<=2) h2h_input(&model,key,event); }
void preview_tick(unsigned ms) { h2h_tick(&model,ms); }
void preview_audio(unsigned ms,unsigned generation,int ended,int failed) {
    if(generation!=model.generation) return;
    if(ms && model.want_play) h2h_listened(&model,ms);
    if(ended || failed) h2h_audio_end(&model,generation,failed!=0);
}
void preview_position(unsigned position,unsigned duration,unsigned generation) { if(generation==model.generation) { model.position_ms=position;model.duration_ms=duration; } }
int preview_load(const void *bytes,unsigned size) { return h2h_load(&model,bytes,size); }
unsigned preview_save(void *bytes) { h2h_save_t save; h2h_save_snapshot(&model,&save); memcpy(bytes,&save,sizeof(save)); return sizeof(save); }
const char *preview_state(void) {
    static char buffer[4096];
    h2h_motion_t motion=h2h_motion(&model,model.want_play);
    int prefix=snprintf(buffer,sizeof(buffer),"{\"start_ms\":%u,\"position_ms\":%u,\"anim_pose\":%u,\"anim_dx\":%d,\"anim_dy\":%d,\"anim_hearts\":%s,",model.start_ms,model.position_ms,motion.pose,motion.dx,motion.dy,motion.hearts?"true":"false");
    int n=prefix+snprintf(buffer+prefix,sizeof(buffer)-(size_t)prefix,"\"page\":%u,\"selection\":%u,\"track\":%u,\"song_count\":%u,\"chorus\":%s,\"generation\":%u,\"play\":%s,\"error\":%s,\"countdown\":%u,\"pose\":%u,\"pose_ms\":%u,\"hearts\":%u,\"cards\":%u,\"outfit\":%u,\"room\":%u,\"volume\":%u,\"mode\":%u,\"countdown_on\":%u,\"badge_card\":%u,\"game_ms\":%u,\"paused\":%s,\"caught\":%u,\"reward\":%u,\"basket\":%u,\"unlocked\":%u,\"outfits\":%u,\"rooms\":%u,\"drops\":[",model.page,model.selection,model.track,model.song_count,h2h_chorus_mode(&model)?"true":"false",model.generation,model.want_play?"true":"false",model.audio_error?"true":"false",model.countdown_ms,model.pose,model.pose_ms,model.save.hearts,model.save.cards,model.save.outfit,model.save.room,model.save.volume,model.save.mode,model.save.countdown&1u,model.save.badge_card,model.game_ms,model.game_paused?"true":"false",model.caught,model.game_reward,model.basket,model.last_unlock,h2h_outfit_count(&model),h2h_room_count(&model));
    for(unsigned i=0;i<6;i++) n+=snprintf(buffer+n,sizeof(buffer)-(size_t)n,"%s{\"lane\":%d,\"y\":%d,\"active\":%s,\"heart\":%s}",i?",":"",model.drops[i].lane,model.drops[i].y,model.drops[i].active?"true":"false",model.drops[i].heart?"true":"false");
    snprintf(buffer+n,sizeof(buffer)-(size_t)n,"]}"); return buffer;
}
