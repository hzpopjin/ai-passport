#include "stayc_visual.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <string.h>

extern const uint8_t _binary_stayc_visuals_bin_start[], _binary_stayc_visuals_bin_end[];
static uint8_t pixels[STAYC_FRAME_BYTES];
static lv_image_dsc_t image = {.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565A8,.w=64,.h=80,.stride=128},.data_size=STAYC_FRAME_BYTES,.data=pixels};
static unsigned old_index=999, active_member, active_outfit, active_action;
static stayc_frame_t active_frame;
static unsigned active_room, active_decor;
static void box(lv_layer_t *layer,int x,int y,int w,int h,uint32_t color,int radius) {
    if(w<=0 || h<=0)return;
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;d.radius=radius;
    lv_area_t a={x,y,x+w-1,y+h-1};lv_draw_rect(layer,&d,&a);
}
static void line(lv_layer_t *layer,int x,int y,int a,int b,uint32_t color,int width) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.p1=(lv_point_precise_t){x,y};d.p2=(lv_point_precise_t){a,b};
    d.color=lv_color_hex(color);d.width=width;d.opa=LV_OPA_COVER;lv_draw_line(layer,&d);
}
static void ball(lv_layer_t *l,int x,int y) {
    box(l,x-6,y-8,12,16,0xda9656,0);box(l,x-8,y-6,16,12,0xda9656,0);
    line(l,x-7,y,x+7,y,0x26466b,1);line(l,x,y-7,x,y+7,0x26466b,1);
}
static void anchor(const stayc_sprite_t *p,const stayc_frame_t *f,unsigned which,int *x,int *y) {
    if(which==4) {
        *x=f->x+((p->anchors[0][0]+p->anchors[1][0]+1)/2)*f->scale_q8/256;
        *y=f->y+((p->anchors[0][1]+p->anchors[1][1]+1)/2+p->offset_y)*f->scale_q8/256;
    } else {
        *x=f->x+p->anchors[which][0]*f->scale_q8/256;
        *y=f->y+(p->anchors[which][1]+p->offset_y)*f->scale_q8/256;
    }
}
static void props_draw(lv_event_t *event) {
    lv_layer_t *l=lv_event_get_layer(event);const stayc_frame_t *f=&active_frame;
    const stayc_sprite_t *p=&stayc_sprites[active_member][active_outfit][f->pose];
    int x,y;anchor(p,f,f->attachment==2?4:p->hand,&x,&y);
    if(f->attachment==3)y+=8*f->scale_q8/256;
    if(f->prop==1) {box(l,x-10,y-8,10,12,0xf5a9ca,5);box(l,x,y-8,10,12,0xf5a9ca,5);box(l,x-6,y,12,9,0xf5a9ca,4);}
    if(f->prop==2) {
        /* renderer.mjs draws 11x17 before the close scene's 2x transform. */
        int scale=f->scale_q8;
        box(l,x-5*scale/256,y-12*scale/256,11*scale/256,17*scale/256,0x26466b,0);
        box(l,x-3*scale/256,y-10*scale/256,7*scale/256,12*scale/256,f->flash?0xfffcf2:0xeaf7f6,0);
        if(!f->flash){
            box(l,x-2*scale/256,y-8*scale/256,5*scale/256,2*scale/256,0x80bbe6,0);
            int scroll=f->scroll?(int)(f->progress_q8/86):0;
            box(l,x-2*scale/256,y-(4+scroll)*scale/256,4*scale/256,scale/256,0xf7cdd9,0);
        }
    }
    if(f->prop==3) {
        const int offsets[][2]={{7,-24},{-9,-25},{12,-18},{-16,-12}};
        unsigned n=f->pose>=8&&f->pose<=11?f->pose-8:0;int sign=p->hand?1:-1;
        int hx=x+(offsets[n][0]<0?-offsets[n][0]:offsets[n][0])*sign,hy=y+offsets[n][1];
        int center=f->x+32*f->scale_q8/256;
        if(sign<0&&hx>center-22)hx=center-22;
        if(sign>0&&hx<center+22)hx=center+22;
        line(l,x,y,hx,hy,0x26466b,3);
        box(l,hx-6,hy-10,12,20,0x26466b,0);box(l,hx-8,hy-8,16,16,0x26466b,0);
        box(l,hx-4,hy-8,8,16,0xfffcf2,0);box(l,hx-6,hy-6,12,12,0xfffcf2,0);
        for(int i=-3;i<=3;i+=3)line(l,hx+i,hy-7,hx+i,hy+7,0x80bbe6,1);
        for(int i=-4;i<=4;i+=4)line(l,hx-5,hy+i,hx+5,hy+i,0x80bbe6,1);
    }
    if(f->prop==4)ball(l,x,y);
    if(active_action==8) {
        int bx=p->hand?163:77;
        box(l,43,199,157,10,0xd4b68e,0);box(l,51,209,7,28,0xb99a74,0);box(l,185,209,7,28,0xb99a74,0);
        box(l,bx-16,190,34,8,0xffffff,3);box(l,bx-12,197,26,3,0xc6d8df,0);
        if(f->elapsed<8400)box(l,bx-12,187,26,4,0xffea8a,0);
        if(f->prop==5){int tx=bx,ty=193;if(f->pose==17)anchor(p,f,2,&tx,&ty);line(l,x,y,tx,ty,0x608491,2);box(l,tx-2,ty-1,5,3,0xffea8a,1);}
        if(f->prop==6){int mx,my;anchor(p,f,2,&mx,&my);if(p->anchors[2][0]>=0)box(l,mx-6,my-4,12,8,0xfffcf2,1);}
    }
    if(f->prop==7) {
        int hx=150,hy=221,t=(int)f->elapsed;
        if(t>=1800&&t<3700){hx=150-18*(t-1800)/1900;hy=221-5*(t-1800)/1900;}
        else if(t>=3700&&t<4600){hx=132+18*(t-3700)/900;hy=216+5*(t-3700)/900;}
        else if(t>=4600){int dt=t-4600;if(dt>1100)dt=1100;hx=150+28*dt/1100;hy=221-40*dt/1100;}
        anchor(p,f,3,&x,&y);line(l,x,y,hx,hy,0x6b8296,2);box(l,hx-6,hy-1,7,3,0x26466b,0);
    }
    if(f->effect==1)ball(l,f->effect_x,f->effect_y);
    if(f->effect==2){x=f->effect_x;y=f->effect_y;line(l,x,y,x-5,y-7,0xfffcf2,2);line(l,x,y,x,y-8,0xfffcf2,2);line(l,x,y,x+5,y-7,0xfffcf2,2);box(l,x-2,y-1,4,4,0xffea8a,0);}
    if(f->effect==3)box(l,f->effect_x-2,f->effect_y-2,4,4,0xfffcf2,2);
}
static void room_draw(lv_event_t *event) {
    lv_layer_t *l=lv_event_get_layer(event);
    for(unsigned layer=0;layer<5;layer++){
        const stayc_room_t *r=layer?&stayc_decor[layer-1][(active_decor>>(2*(layer-1)))&3u]:&stayc_rooms[active_room];
        for(unsigned i=0;i<r->count;i++){const stayc_box_t *b=&r->boxes[i];box(l,b->x,b->y,b->w,b->h,b->color,b->radius);}
    }
    /* Rich-action fixtures precede the actor, as in drawPreview(). */
    if(active_action==5){box(l,52,184,116,29,0xb5d6e3,0);box(l,57,207,106,12,0x9cc0d0,0);}
    if(active_action==7){
        line(l,198,87,198,218,0x26466b,2);box(l,181,88,32,22,0xfffcf2,0);box(l,188,101,19,3,0x26466b,0);
        line(l,190,112,205,112,0xd17d54,2);line(l,190,113,193,129,0xfffcf2,1);
        line(l,205,113,202,129,0xfffcf2,1);line(l,193,129,202,129,0xfffcf2,1);
    }
}
void stayc_visual_room(lv_obj_t *screen,unsigned room,unsigned decor) {
    active_room=room%3;active_decor=decor;
    for(unsigned i=0;i<4;i++)if(((decor>>(2*i))&3u)>2)active_decor&=~(3u<<(2*i));
    lv_obj_t *o=lv_obj_create(screen);lv_obj_remove_style_all(o);lv_obj_set_pos(o,12,43);lv_obj_set_size(o,216,199);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_event_cb(o,room_draw,LV_EVENT_DRAW_MAIN,NULL);
}
lv_obj_t *stayc_visual_actor(lv_obj_t *screen) {
    lv_obj_t *o=lv_image_create(screen);lv_image_set_pivot(o,0,0);lv_image_set_antialias(o,false);return o;
}
lv_obj_t *stayc_visual_props(lv_obj_t *screen) {
    lv_obj_t *o=lv_obj_create(screen);lv_obj_remove_style_all(o);lv_obj_set_pos(o,12,43);lv_obj_set_size(o,216,199);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_event_cb(o,props_draw,LV_EVENT_DRAW_MAIN,NULL);return o;
}
bool stayc_visual_update(lv_obj_t *actor,lv_obj_t *props,unsigned member,unsigned outfit,unsigned action,const stayc_frame_t *frame) {
    if(member>=6||outfit>=2||frame->pose>=32)return false;
    const stayc_sprite_t *p=&stayc_sprites[member][outfit][frame->pose];unsigned index=(member*2+outfit)*32+frame->pose;
    size_t length=(size_t)(_binary_stayc_visuals_bin_end-_binary_stayc_visuals_bin_start);
    if(index!=old_index){
        if(p->offset>length||p->length>length-p->offset||!stayc_unpack(_binary_stayc_visuals_bin_start+p->offset,p->length,pixels,sizeof(pixels)))return false;
        lv_image_cache_drop(&image);old_index=index;
    }
    active_member=member;active_outfit=outfit;active_action=action;active_frame=*frame;
    if(p->scale_q8!=256){
        active_frame.x+=32*frame->scale_q8*(256-(int)p->scale_q8)/65536;
        active_frame.y+=78*frame->scale_q8*(256-(int)p->scale_q8)/65536;
        active_frame.scale_q8=frame->scale_q8*p->scale_q8/256;
    }
    lv_image_set_src(actor,&image);lv_image_set_scale(actor,active_frame.scale_q8);
    lv_obj_set_pos(actor,active_frame.x,active_frame.y+p->offset_y*active_frame.scale_q8/256);lv_obj_invalidate(actor);
    if(props)lv_obj_invalidate(props);
    return true;
}
