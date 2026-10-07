#include "stayc_game_visual.h"
#include "stayc_game_decor.h"
#include "stayc_game_life.h"
#include "stayc_game_motion.h"
#include "stayc_game_reaction.h"
#include "stayc_visual.h"
#include <string.h>

#define INK 0x26466bu
#define SKIN 0xfacdb6u
#define WHITE 0xfffcf2u
#define LEMON 0xffea8au
#define HUG_PINK 0xf7afc6u

/* These small integer commands replace the old IAN body with one whole genuine
 * STAYC atlas cell. 1,280 bytes, bounded, shared by background and foreground.
 * Dense life/reaction coverage measures at most 52 source commands. */
typedef struct {
    int16_t type, x, y, a, b, p, q, scale;
    uint32_t color;
} command_t;
enum { COMMAND_LIMIT = 64 };
static command_t commands[COMMAND_LIMIT];
static unsigned command_count, first_sprite, last_skin;
static bool command_overflow, compact_scene, reaction_scene;
static unsigned active_member, active_outfit, active_action = 99;
static stayc_game_t scene_model;
static stayc_frame_t registered_frame;

static void capture(const stayc_game_life_draw_t *d, void *context) {
    (void)context;
    if (command_count == COMMAND_LIMIT) { command_overflow = true; return; }
    commands[command_count++] = (command_t){d->type,d->x,d->y,d->a,d->b,d->p,d->q,d->scale,d->color};
}
static void collect(const stayc_game_t *m, bool reaction) {
    command_count = 0; command_overflow = false; first_sprite = COMMAND_LIMIT;
    last_skin = COMMAND_LIMIT;
    if (reaction) stayc_game_reaction_scene(m,capture,NULL);
    else stayc_game_life_scene(m,capture,NULL);
    for (unsigned i=0;i<command_count;i++) {
        const command_t *d=&commands[i];
        if (d->type == STAYC_GAME_LIFE_SPRITE && first_sprite == COMMAND_LIMIT) first_sprite=i;
        if (d->type != STAYC_GAME_LIFE_SPRITE && d->color == SKIN) last_skin=i;
    }
}
static int round_q8(int value) {
    return value >= 0 ? (value+128)/256 : -((-value+127)/256);
}
static void origin(const stayc_game_t *m, bool reaction, stayc_frame_t *f) {
    f->x=56; f->y=reaction?77:m->life_kind==1?68:m->life_kind==0?72:133;
    f->scale_q8=reaction||m->life_kind<2?512:256;
    for (unsigned i=0;i<command_count;i++) {
        const command_t *d=&commands[i];
        if (d->type == STAYC_GAME_LIFE_SPRITE && d->a==23 && d->b==40 && d->p==20 && d->q==24) {
            f->x=d->x-d->a*d->scale; f->y=d->y-d->b*d->scale;
            f->scale_q8=d->scale*256; break;
        }
    }
}
static unsigned life_pose(const stayc_game_t *m) {
    unsigned t=m->life_started?m->life_ms:0;
    if (m->life_kind==0) {
        if (t>=10100) return 5;
        if (t>=8400) return 29;
        /* Original scene spoon tip establishes the bite branch. */
        bool bite=false;
        for(unsigned i=0;i<command_count;i++) {
            const command_t *d=&commands[i];
            if(d->type==STAYC_GAME_LIFE_LINE && d->color==INK && d->p==2 &&
               d->a-d->x==-10 && d->b-d->y==-5 && d->b<160) bite=true;
        }
        return bite?17:16;
    }
    if(m->life_kind==1) {
        if(!m->life_started||t<700||t>=10000) return 5;
        if(t>=6200&&t<8800) return m->life_choice?7:t>=6600?30:6;
        return 6;
    }
    if(m->life_kind==2) {
        if(t>=10800) return 31;
        if(!m->life_started||t<1500) return 8;
        unsigned cycle=(t-1500)%3000;
        return cycle<1000||cycle>=2050?8:cycle<1500?9:cycle<1700?10:11;
    }
    return t>=9700?24:t>=4600?19:18;
}
static unsigned reaction_pose(const stayc_game_t *m) {
    unsigned card=m->reaction_card,t=m->reaction_ms;
    unsigned duration=stayc_game_card_reaction_duration(card);
    if(card>=8||!m->reaction_active||t<88||t>=duration-174) return 0;
    switch(card) {
        case 0:return 1;
        case 1:return 20;
        case 2:return t<1600?21:22;
        case 3:return 23;
        case 4:return t<1000?15:4;
        case 5:return t<1300?24:25;
        case 6: {
            stayc_game_reaction_frame_t r=stayc_game_reaction_frame(m);
            return 56+r.dx+r.lean*2<56?26:27;
        }
        default:return 28;
    }
}
void stayc_game_visual_sample(const stayc_game_t *m,unsigned member,unsigned outfit,stayc_frame_t *f) {
    memset(f,0,sizeof(*f));
    if(!m||member>=STAYC_MEMBERS||outfit>=STAYC_OUTFITS)return;
    bool reaction=m->page==STAYC_GAME_CARD||m->page==STAYC_GAME_BADGE;
    collect(m,reaction);origin(m,reaction,f);
    f->pose=reaction?reaction_pose(m):life_pose(m);
    f->elapsed=reaction?m->reaction_ms:m->life_started?m->life_ms:0;
    f->duration=reaction?stayc_game_card_reaction_duration(m->reaction_card):stayc_game_life_duration(m->life_kind);
    if(f->elapsed>f->duration)f->elapsed=f->duration;
    f->done=f->elapsed>=f->duration;
}
unsigned stayc_game_visual_command_count(void) { return command_count; }

static void rect(lv_layer_t *l,int x,int y,int w,int h,uint32_t color,int radius) {
    if(w<=0||h<=0)return;
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
    d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;d.radius=radius;
    lv_area_t area={x,y,x+w-1,y+h-1};lv_draw_rect(l,&d,&area);
}
static void line(lv_layer_t *l,int x,int y,int a,int b,uint32_t color,int width) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);
    d.p1=(lv_point_precise_t){x,y};d.p2=(lv_point_precise_t){a,b};
    d.color=lv_color_hex(color);d.width=width;d.opa=LV_OPA_COVER;lv_draw_line(l,&d);
}
static void draw(lv_layer_t *layer,const command_t *d,int dx,int dy) {
    if(d->type==STAYC_GAME_LIFE_RECT)rect(layer,d->x+dx,d->y+dy,d->a,d->b,d->color,d->p);
    else if(d->type==STAYC_GAME_LIFE_LINE)line(layer,d->x+dx,d->y+dy,d->a+dx,d->b+dy,d->color,d->p);
}
static void decor_emit(const stayc_game_life_draw_t *d,void *ctx) {
    command_t c={d->type,d->x,d->y,d->a,d->b,d->p,d->q,d->scale,d->color};
    if(compact_scene){c.x=39+(c.x-12)*3/4;c.y=75+(c.y-43)*3/4;c.a=(c.a*3+3)/4;c.b=(c.b*3+3)/4;c.p=c.p*3/4;}
    draw(ctx,&c,0,0);
}
static void room_box(lv_layer_t *l,const stayc_box_t *b) {
    int x=b->x,y=b->y,w=b->w,h=b->h,r=b->radius;
    if(compact_scene){x=39+(x-12)*3/4;y=75+(y-43)*3/4;w=(w*3+3)/4;h=(h*3+3)/4;r=r*3/4;}
    rect(l,x,y,w,h,b->color,r);
}
static void background_draw(lv_event_t *e) {
    lv_layer_t *l=lv_event_get_layer(e);
    if(scene_model.page!=STAYC_GAME_CARD){
        const stayc_room_t *room=&stayc_rooms[stayc_game_shop_room_preview(&scene_model)%3];
        for(unsigned i=0;i<room->count;i++)room_box(l,&room->boxes[i]);
        stayc_game_decor_scene(&scene_model,decor_emit,l);
    }
    if(active_action==5){rect(l,52,184,116,29,0xb5d6e3,0);rect(l,57,207,106,12,0x9cc0d0,0);}
    if(active_action==7){line(l,198,87,198,218,INK,2);rect(l,181,88,32,22,WHITE,0);rect(l,188,101,19,3,INK,0);line(l,190,112,205,112,0xd17d54,2);line(l,190,113,193,129,WHITE,1);line(l,205,113,202,129,WHITE,1);line(l,193,129,202,129,WHITE,1);}
    unsigned reaction_duration=stayc_game_card_reaction_duration(scene_model.reaction_card);
    bool show_reaction=reaction_scene&&scene_model.reaction_active&&scene_model.reaction_ms>=88&&scene_model.reaction_ms<reaction_duration-174;
    if(scene_model.page==STAYC_GAME_LIFE||show_reaction)
        for(unsigned i=0;i<command_count&&i<first_sprite;i++)draw(l,&commands[i],0,0);
}
/* Same floor registration and anchors as member-scenes.mjs, including life
 * sceneScale and supplemental registrationOffsetY. */
static void hand(unsigned which,int *x,int *y) {
    const stayc_sprite_t *p=&stayc_sprites[active_member][active_outfit][registered_frame.pose];
    int ax,ay;
    if(which==4){ax=(p->anchors[0][0]+p->anchors[1][0])*128;ay=(p->anchors[0][1]+p->anchors[1][1])*128;}
    else {ax=p->anchors[which][0]*256;ay=p->anchors[which][1]*256;}
    *x=registered_frame.x+round_q8(ax*registered_frame.scale_q8/256);
    *y=registered_frame.y+round_q8((ay+p->offset_y*256)*registered_frame.scale_q8/256);
}
static unsigned holding_hand(void) { return stayc_sprites[active_member][active_outfit][registered_frame.pose].hand; }
static int find_rect(unsigned start,uint32_t color,int w,int h) {
    for(unsigned i=start;i<command_count;i++)if(commands[i].type==STAYC_GAME_LIFE_RECT&&commands[i].color==color&&commands[i].a==w&&commands[i].b==h)return (int)i;
    return -1;
}
static void sparks(lv_layer_t *l) {
    for(unsigned i=0;i<command_count;i++)if(commands[i].type==STAYC_GAME_LIFE_LINE&&commands[i].color==LEMON)draw(l,&commands[i],0,0);
}
static void reaction_foreground(lv_layer_t *l) {
    unsigned card=scene_model.reaction_card,t=scene_model.reaction_ms,duration=stayc_game_card_reaction_duration(card);
    if(card>=8||!scene_model.reaction_active||t<88||t>=duration-174)return;
    unsigned start=last_skin==COMMAND_LIMIT?command_count:last_skin+1,prop_count=0;
    int old_x=0,old_y=0,target_x=0,target_y=0,prop_start=(int)start;
    if(card==2&&command_count-start>=5){prop_count=3;old_x=commands[start].x+14;old_y=commands[start].y+10;hand(holding_hand(),&target_x,&target_y);}
    if(card==3&&command_count-start>=10){prop_count=5;old_x=(commands[start].x+commands[start+1].x+commands[start+1].a)/2;old_y=commands[start].y+commands[start].b/2;hand(4,&target_x,&target_y);}
    if(card==5&&command_count-start>=9){prop_count=3;old_x=(commands[start].x+commands[start].a)/2;old_y=commands[start].y+9;hand(holding_hand(),&target_x,&target_y);}
    if(card==7){prop_start=find_rect(0,HUG_PINK,16,16);if(prop_start>=0){prop_count=5;old_x=commands[prop_start].x+16;old_y=commands[prop_start].y+20;hand(4,&target_x,&target_y);}}
    if(prop_count)for(unsigned n=0;n<prop_count&&(unsigned)prop_start+n<command_count;n++)draw(l,&commands[prop_start+n],target_x-old_x,target_y-old_y);
    if(card!=7)start+=prop_count;
    for(unsigned i=start;i<command_count;i++)draw(l,&commands[i],0,0);
}
static void meal_foreground(lv_layer_t *l) {
    int table=-1;
    for(unsigned i=0;i<command_count;i++)if(commands[i].type==STAYC_GAME_LIFE_RECT&&commands[i].x==52&&commands[i].y==191&&commands[i].a==136&&commands[i].b==10&&commands[i].color==0xd9a981)table=(int)i;
    if(table>=0){for(unsigned i=(unsigned)table;i<command_count&&i<(unsigned)table+5;i++)draw(l,&commands[i],0,0);unsigned food=(unsigned)table+5;if(food<command_count&&commands[food].type==STAYC_GAME_LIFE_RECT&&commands[food].color==0xf4db94)draw(l,&commands[food],0,0);}
    int cup=find_rect(0,WHITE,17,20);if(cup>=0){draw(l,&commands[cup],0,0);if((unsigned)cup+1<command_count)draw(l,&commands[cup+1],0,0);}
    int hx,hy;hand(holding_hand(),&hx,&hy);
    unsigned t=scene_model.life_started?scene_model.life_ms:0;
    if(t<8400){int tx=127,ty=179;if(registered_frame.pose==17)hand(2,&tx,&ty);line(l,hx,hy,tx,ty,INK,2);for(unsigned i=0;i<command_count;i++)if(commands[i].type==STAYC_GAME_LIFE_RECT&&commands[i].color==0xf2bb68)rect(l,tx-3,ty-5,commands[i].a,commands[i].b,commands[i].color,commands[i].p);}
    if(t>=8400&&t<10100){int mx,my;hand(2,&mx,&my);int tissue=find_rect(0,WHITE,12,8);if(tissue>=0)rect(l,mx-6,my-4,12,8,WHITE,1);}
    sparks(l);
}
static void phone_foreground(lv_layer_t *l) {
    int phone=find_rect(0,INK,25,36);
    if(phone>=0){int hx,hy;hand(holding_hand(),&hx,&hy);int dx=hx-commands[phone].x-13,dy=hy-commands[phone].y-23;for(unsigned i=(unsigned)phone;i<command_count;i++){if(commands[i].color==SKIN||commands[i].type!=STAYC_GAME_LIFE_RECT)break;draw(l,&commands[i],dx,dy);}}
    sparks(l);
}
static void golf_foreground(lv_layer_t *l) {
    for(unsigned i=1;i<command_count;i++)if(commands[i].type==STAYC_GAME_LIFE_LINE&&commands[i].color==0x7d8fa1&&commands[i].p==4){command_t shaft=commands[i-1];int x,y;const stayc_sprite_t *p=&stayc_sprites[active_member][active_outfit][registered_frame.pose];hand(p->anchors[3][0]>=0?3:scene_model.life_ms>=9700?1u-holding_hand():4,&x,&y);shaft.x=x;shaft.y=y;draw(l,&shaft,0,0);draw(l,&commands[i],0,0);}
    for(unsigned i=0;i<command_count;i++){const command_t *d=&commands[i];if(d->type==STAYC_GAME_LIFE_RECT&&d->color==WHITE&&d->x>=120&&d->y>=210&&d->a==6&&d->b<=6)draw(l,d,0,0);}
    sparks(l);
}
static void badminton_foreground(lv_layer_t *l) {
    int oval=find_rect(0,INK,20,25);if(oval<1)return;
    unsigned start=(unsigned)oval-1,end=(unsigned)oval+8;
    if(end>command_count)return;
    command_t handle=commands[start];int hx,hy;hand(holding_hand(),&hx,&hy);
    int sign=holding_hand()?1:-1,head_x=hx+(handle.a-handle.x)*sign;
    int center=registered_frame.x+32*registered_frame.scale_q8/256;
    int clearance=22*registered_frame.scale_q8/256;
    int safe_x=sign<0?(head_x<center-clearance?head_x:center-clearance):(head_x>center+clearance?head_x:center+clearance);
    for(unsigned i=start;i<end;i++){
        command_t d=commands[i];d.x=hx+(d.x-handle.x)*sign;d.y=hy+d.y-handle.y;
        if(d.type==STAYC_GAME_LIFE_LINE){d.a=hx+(d.a-handle.x)*sign;d.b=hy+d.b-handle.y;}
        else if(sign<0)d.x-=d.a;
        if(i==start)d.a=safe_x;else{d.x+=safe_x-head_x;if(d.type==STAYC_GAME_LIFE_LINE)d.a+=safe_x-head_x;}
        draw(l,&d,0,0);
    }
    int shuttle=-1;for(unsigned i=end;i<command_count;i++)if(commands[i].type==STAYC_GAME_LIFE_LINE&&commands[i].color==WHITE&&commands[i].p==3){shuttle=(int)i;break;}
    if(shuttle>=0){const stayc_sprite_t *p=&stayc_sprites[active_member][active_outfit][10];int cs=p->hand?1:-1,cx=65+p->anchors[p->hand][0]+14*cs;cx=cs<0?(cx<75?cx:75):(cx>119?cx:119);int cy=126+p->anchors[p->hand][1]+p->offset_y-11;unsigned t=scene_model.life_ms,cycle=t>=1500?(t-1500)%3000:0;unsigned weight=scene_model.life_started&&t>=1500?(cycle<=1500?cycle:3000-cycle):0;for(unsigned i=(unsigned)shuttle;i<command_count&&i<(unsigned)shuttle+4;i++)draw(l,&commands[i],(cx-129)*(int)weight/1500,(cy-162)*(int)weight/1500);}
    for(unsigned i=end;i<command_count;i++)if(commands[i].type==STAYC_GAME_LIFE_LINE&&commands[i].color==LEMON){command_t d=commands[i];if(registered_frame.pose==10){d.x=hx+(d.x-handle.x)*sign+safe_x-head_x;d.y=hy+d.y-handle.y;d.a=hx+(d.a-handle.x)*sign+safe_x-head_x;d.b=hy+d.b-handle.y;}draw(l,&d,0,0);}
}
static void foreground_draw(lv_event_t *e) {
    if(command_overflow)return;
    lv_layer_t *l=lv_event_get_layer(e);
    if(reaction_scene)reaction_foreground(l);
    else if(scene_model.page==STAYC_GAME_LIFE){switch(scene_model.life_kind){case 0:meal_foreground(l);break;case 1:phone_foreground(l);break;case 2:badminton_foreground(l);break;case 3:golf_foreground(l);break;default:break;}}
}
lv_obj_t *stayc_game_visual_background(lv_obj_t *parent) {
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,12,43);lv_obj_set_size(o,216,199);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_event_cb(o,background_draw,LV_EVENT_DRAW_MAIN,NULL);return o;
}
lv_obj_t *stayc_game_visual_actor(lv_obj_t *parent) { return stayc_visual_actor(parent); }
lv_obj_t *stayc_game_visual_foreground(lv_obj_t *parent) {
    lv_obj_t *o=stayc_visual_props(parent);lv_obj_add_event_cb(o,foreground_draw,LV_EVENT_DRAW_MAIN,NULL);return o;
}
bool stayc_game_visual_update(lv_obj_t *background,lv_obj_t *actor,lv_obj_t *foreground,const stayc_game_t *m,const stayc_control_t *controls,bool playing,int x,int y,unsigned scale,bool compact) {
    if(!m||!actor)return false;
    scene_model=*m;active_member=controls?controls->member%STAYC_MEMBERS:0;
    unsigned candidate=stayc_game_shop_outfit_preview(m);
    active_outfit=candidate<STAYC_OUTFITS?candidate:0;compact_scene=compact;
    reaction_scene=stayc_game_reaction_visible(m);
    active_action=99;command_count=0;command_overflow=false;
    stayc_frame_t frame={.x=x,.y=y,.scale_q8=(int)scale};
    if(m->page==STAYC_GAME_LIFE||reaction_scene)stayc_game_visual_sample(m,active_member,active_outfit,&frame);
    else if(m->page==STAYC_GAME_ROOM&&controls&&controls->view==STAYC_ACTION_PLAYER){active_action=controls->action;stayc_sample(active_member,active_outfit,active_action,controls->elapsed,&frame);}
    else{stayc_game_motion_t motion=stayc_game_motion(m,playing);frame.pose=motion.pose;frame.x+=motion.dx;frame.y+=motion.dy;}
    if(compact){frame.x=39+(frame.x-12)*3/4;frame.y=75+(frame.y-43)*3/4;frame.scale_q8=frame.scale_q8*3/4;}
    registered_frame=frame;
    const stayc_sprite_t *p=&stayc_sprites[active_member][active_outfit][frame.pose];
    if(p->scale_q8!=256){registered_frame.x+=32*frame.scale_q8*(256-(int)p->scale_q8)/65536;registered_frame.y+=78*frame.scale_q8*(256-(int)p->scale_q8)/65536;registered_frame.scale_q8=frame.scale_q8*p->scale_q8/256;}
    bool ok=!command_overflow&&stayc_visual_update(actor,foreground,active_member,active_outfit,active_action,&frame);
    if(background)lv_obj_invalidate(background);
    if(foreground)lv_obj_invalidate(foreground);
    return ok;
}
