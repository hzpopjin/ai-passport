/* Host-only public state bridge for the explicit V4 port. */
#include "stayc_game.h"
#include "stayc_game_motion.h"
#include "stayc_game_life.h"
#include "stayc_game_reaction.h"
#include "stayc_game_decor.h"
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(stayc_game_save_t)<=128, "Preview durable save buffer too small");
_Static_assert(sizeof(stayc_game_t)<=2048, "Web model snapshot buffer too small");
static stayc_game_t model;
unsigned preview_shop_prepare(void *bytes){return (unsigned)stayc_game_shop_prepare(&model,bytes);}
int preview_shop_validate(const void *bytes,unsigned size,unsigned generation,unsigned epoch){return stayc_game_shop_validate(&model,bytes,size,generation,epoch);}
int preview_shop_finish(const void *bytes,unsigned size,unsigned result,unsigned generation,unsigned epoch){return stayc_game_shop_finish(&model,bytes,size,result,generation,epoch);}
void preview_init(unsigned tracks,unsigned songs) { stayc_game_init(&model,tracks,songs,20260929); }
void preview_input(int key,int event) { if(key>=0 && key<=2 && event>=0 && event<=2) stayc_game_input(&model,key,event); }
void preview_tick(unsigned ms) { stayc_game_tick(&model,ms); }
void preview_audio(unsigned ms,unsigned generation,int ended,int failed) {
    if(generation!=model.generation) return;
    if(ms && model.want_play) stayc_game_listened(&model,ms);
    if(ended || failed) stayc_game_audio_end(&model,generation,failed!=0);
}
void preview_position(unsigned position,unsigned duration,unsigned generation) { if(generation==model.generation) { model.position_ms=position;model.duration_ms=duration; } }
int preview_load(const void *bytes,unsigned size) { return stayc_game_load(&model,bytes,size); }
unsigned preview_save(void *bytes) { stayc_game_save_t save; stayc_game_save_snapshot(&model,&save); memcpy(bytes,&save,sizeof(save)); return sizeof(save); }
typedef struct { char *buffer;size_t size;int used,count; } scene_json_t;
static void scene_json(const stayc_game_life_draw_t *d,void *context) {
    scene_json_t *j=context;
    if(j->used<0 || (size_t)j->used>=j->size)return;
    j->used+=snprintf(j->buffer+j->used,j->size-(size_t)j->used,"%s[%d,%d,%d,%d,%d,%u,%d,%d,%d]",j->count++?",":"",d->type,d->x,d->y,d->a,d->b,d->color,d->p,d->q,d->scale);
}
const char *preview_state(void) {
    static char buffer[16384];
    stayc_game_shop_kind_t shop_kind=stayc_game_shop_current_kind(&model);
    unsigned shop_item=model.page==STAYC_GAME_REDEEM?model.shop_item:shop_kind==STAYC_GAME_SHOP_DECOR?model.decor_slot*4+model.selection:model.selection;
    stayc_game_motion_t motion=stayc_game_motion(&model,model.want_play);
    int prefix=snprintf(buffer,sizeof(buffer),"{\"home_label\":\"%s\",\"footer_hint\":\"%s\",\"debug_mode\":%s,\"debug_epoch\":%u,\"start_ms\":%u,\"position_ms\":%u,\"anim_pose\":%u,\"anim_dx\":%d,\"anim_dy\":%d,\"anim_hearts\":%s,",stayc_game_home_label(),stayc_game_footer_hint(&model),model.debug_mode?"true":"false",model.debug_epoch,model.start_ms,model.position_ms,motion.pose,motion.dx,motion.dy,motion.hearts?"true":"false");
    int n=prefix+snprintf(buffer+prefix,sizeof(buffer)-(size_t)prefix,"\"page\":%u,\"selection\":%u,\"track\":%u,\"song_count\":%u,\"chorus\":%s,\"generation\":%u,\"play\":%s,\"error\":%s,\"countdown\":%u,\"pose\":%u,\"pose_ms\":%u,\"hearts\":%u,\"cards\":%u,\"outfit\":%u,\"room\":%u,\"volume\":%u,\"volume_edit\":%s,\"mode\":%u,\"countdown_on\":%u,\"badge_card\":%u,\"game_ms\":%u,\"paused\":%s,\"caught\":%u,\"reward\":%u,\"basket\":%u,\"unlocked\":%u,\"outfits\":%u,\"rooms\":%u,\"drops\":[",model.page,model.selection,model.track,model.song_count,stayc_game_chorus_mode(&model)?"true":"false",model.generation,model.want_play?"true":"false",model.audio_error?"true":"false",model.countdown_ms,model.pose,model.pose_ms,model.save.hearts,model.save.cards,model.save.outfit,model.save.room,model.save.volume,model.volume_edit?"true":"false",model.save.mode,model.save.countdown&1u,model.save.badge_card,model.game_ms,model.game_paused?"true":"false",model.caught,model.game_reward,model.basket,model.last_unlock,stayc_game_outfit_count(&model),stayc_game_room_count(&model));
    for(unsigned i=0;i<6;i++) n+=snprintf(buffer+n,sizeof(buffer)-(size_t)n,"%s{\"lane\":%d,\"y\":%d,\"active\":%s,\"heart\":%s}",i?",":"",model.drops[i].lane,model.drops[i].y,model.drops[i].active?"true":"false",model.drops[i].heart?"true":"false");
    n+=snprintf(buffer+n,sizeof(buffer)-(size_t)n,
        "],\"score\":%u,\"combo\":%u,\"best_combo\":%u,\"phase\":%u,\"best_score\":%u,\"new_best\":%s,\"game_kind\":%u,\"echo_length\":%u,\"echo_step\":%u,\"echo_input\":%u,\"echo_round\":%u,\"echo_mistakes\":%u,\"echo_show\":%s,\"echo_cue\":%u,\"echo_best\":%u,\"echo_record\":%u,\"journey_claims\":%u,\"journey_ready\":%u,\"journey_progress\":[",
        model.score,model.combo,model.best_combo,stayc_game_game_phase(&model),model.save.best_score,model.new_best?"true":"false",model.game_kind,
        model.echo_length,model.echo_show_ms/900,model.echo_input,model.echo_round,model.echo_mistakes,model.echo_show?"true":"false",stayc_game_echo_cue(&model),model.echo_best,model.save.echo_best,model.save.journey_claims,stayc_game_journey_ready(&model));
    for(unsigned i=0;i<6;i++) n+=snprintf(buffer+n,sizeof(buffer)-(size_t)n,"%s%u",i?",":"",stayc_game_journey_progress(&model,i));
    n+=snprintf(buffer+n,sizeof(buffer)-(size_t)n,"],\"journey_targets\":[");
    for(unsigned i=0;i<6;i++) n+=snprintf(buffer+n,sizeof(buffer)-(size_t)n,"%s%u",i?",":"",stayc_game_journey_target(i));
    n+=snprintf(buffer+n,sizeof(buffer)-(size_t)n,"],\"star_balance\":%u,\"owned_outfits\":%u,\"owned_rooms\":%u,\"owned_decor\":%u,\"shop_kind\":%u,\"shop_item\":%u,\"shop_status\":%u,\"shop_generation\":%u,\"shop_name\":\"%s\",\"shop_price\":%u,\"shop_owned\":%s,\"shop_status_text\":\"%s\",\"outfit_preview\":%u,\"room_preview\":%u,\"decor_choices\":%u",model.save.star_balance,model.save.owned_outfits,model.save.owned_rooms,model.save.owned_decor,shop_kind,shop_item,model.shop_status,model.shop_generation,stayc_game_shop_name(shop_kind,shop_item),stayc_game_shop_price(shop_kind,shop_item),stayc_game_shop_owned(&model,shop_kind,shop_item)?"true":"false",stayc_game_shop_status_text(&model),stayc_game_shop_outfit_preview(&model),stayc_game_shop_room_preview(&model),STAYC_GAME_DECOR_CHOICES);
    n+=snprintf(buffer+n,sizeof(buffer)-(size_t)n,",\"life_kind\":%u,\"life_choice\":%u,\"life_ms\":%u,\"life_started\":%s,\"life_paused\":%s,\"life_finished\":%s,\"life_done\":%u,\"life_duration\":%u,\"life_caption\":\"%s\",\"life_scene\":[",model.life_kind,model.life_choice,model.life_ms,model.life_started?"true":"false",model.life_paused?"true":"false",model.life_finished?"true":"false",model.save.activity_flags>>2,stayc_game_life_duration(model.life_kind),stayc_game_life_caption(&model));
    scene_json_t json={buffer,sizeof(buffer),n,0};
    if(model.page==STAYC_GAME_LIFE)stayc_game_life_scene(&model,scene_json,&json);
    if(json.used<0 || (size_t)json.used>=sizeof(buffer)-3)return "{}";
    json.used+=snprintf(buffer+json.used,sizeof(buffer)-(size_t)json.used,"],\"decor\":%u,\"decor_preview\":%u,\"decor_slot\":%u,\"decor_slot_name\":\"%s\",\"decor_choice_name\":\"%s\",\"decor_scene\":[",model.save.decor,stayc_game_decor_preview(&model),model.decor_slot,stayc_game_decor_slot_name(model.page==STAYC_GAME_DECOR_PICKER?model.decor_slot:model.selection),stayc_game_decor_choice_name(model.page==STAYC_GAME_DECOR_PICKER?model.decor_slot:model.selection,model.page==STAYC_GAME_DECOR_PICKER?model.selection:stayc_game_decor_value(model.save.decor,model.selection)));
    json.count=0;
    if(model.page==STAYC_GAME_HOME || model.page==STAYC_GAME_ROOM || model.page==STAYC_GAME_BADGE || model.page==STAYC_GAME_LIFE || model.page==STAYC_GAME_DECOR || model.page==STAYC_GAME_DECOR_PICKER || model.page==STAYC_GAME_WARDROBE || model.page==STAYC_GAME_ROOM_PICKER || model.page==STAYC_GAME_REDEEM)stayc_game_decor_scene(&model,scene_json,&json);
    if(json.used<0 || (size_t)json.used>=sizeof(buffer)-3)return "{}";
    json.used+=snprintf(buffer+json.used,sizeof(buffer)-(size_t)json.used,"],\"reaction_card\":%u,\"reaction_ms\":%u,\"reaction_active\":%s,\"reaction_duration\":%u,\"reaction_scene\":[",model.reaction_card,model.reaction_ms,model.reaction_active?"true":"false",stayc_game_card_reaction_duration(model.reaction_card));
    json.count=0;
    stayc_game_reaction_scene(&model,scene_json,&json);
    if(json.used<0 || (size_t)json.used>=sizeof(buffer)-3)return "{}";
    snprintf(buffer+json.used,sizeof(buffer)-(size_t)json.used,"]}"); return buffer;
}
