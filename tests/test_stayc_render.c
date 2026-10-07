/* Optional desktop LVGL 9.5 render test. Never links main or device services. */
#include "h2h_ui.h"
#include "h2h_tracks.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t framebuffer[240*320];
static uint16_t draw_buffer[240*40];
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels) {
    unsigned width=(unsigned)(area->x2-area->x1+1);
    for(int y=area->y1;y<=area->y2;y++)memcpy(framebuffer+y*240+area->x1,pixels+(y-area->y1)*width*2,width*2);
    lv_display_flush_ready(display);
}
static void snapshot(const char *folder,const char *name) {
    char path[512];snprintf(path,sizeof(path),"%s/%s.rgb565",folder,name);
    FILE *file=fopen(path,"wb");assert(file);assert(fwrite(framebuffer,sizeof(framebuffer),1,file)==1);fclose(file);
}
static void render(h2h_model_t *model,stayc_control_t *controls) {
    const h2h_cloud_status_t cloud={.bound=model->play_allowed};
    h2h_ui_render(model,10000,false,87,true,true,10000,&cloud,false,-1,controls);
    lv_tick_inc(25);lv_timer_handler();lv_refr_now(NULL);
}
int main(int argc,char **argv) {
    assert(argc==2);lv_init();lv_display_t *display=lv_display_create(240,320);assert(display);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    h2h_model_t m;h2h_init(&m,h2h_track_count,h2h_song_count,1);h2h_activation(&m,true);
    m.save.hearts=40;m.save.cards=255;
    stayc_control_t controls={0};char name[100];
    for(unsigned member=0;member<6;member++)for(unsigned outfit=0;outfit<2;outfit++) {
        controls.member=member;m.save.outfit=outfit;m.page=H2H_HOME;
        render(&m,&controls);snprintf(name,sizeof(name),"member_%u_outfit_%u",member,outfit);snapshot(argv[1],name);
    }
    controls.member=0;m.page=H2H_ROOM;controls.view=STAYC_MEMBER_MENU;
    render(&m,&controls);snapshot(argv[1],"member_menu");
    controls.view=STAYC_ACTION_MENU;controls.selection=8;
    render(&m,&controls);snapshot(argv[1],"action_menu");
    controls.view=STAYC_DECOR_MENU;controls.selection=2;controls.decor=170;
    render(&m,&controls);snapshot(argv[1],"decor_menu");
    controls.view=STAYC_ACTION_PLAYER;
    for(unsigned action=0;action<12;action++) {
        controls.action=action;controls.selection=action;controls.elapsed=action==10?3100:action==11?4600:action==8?2550:1000;
        render(&m,&controls);snprintf(name,sizeof(name),"action_%u",action);snapshot(argv[1],name);
    }
    controls.view=STAYC_NORMAL;
    for(unsigned room=0;room<3;room++){m.page=H2H_HOME;m.save.room=room;render(&m,&controls);snprintf(name,sizeof(name),"room_%u",room);snapshot(argv[1],name);}
    for(unsigned n=0;n<120;n++){m.page=n%2?H2H_COLLECTION:H2H_ROOM;m.selection=n%2?n%10:n%4;render(&m,&controls);}
    h2h_activation(&m,false);render(&m,&controls);snapshot(argv[1],"activation_required");
    lv_mem_monitor_t memory;lv_mem_monitor(&memory);
    printf("Desktop LVGL rendering: PASS; 48 KiB pool free=%lu largest=%lu; does not measure ESP heap\n",(unsigned long)memory.free_size,(unsigned long)memory.free_biggest_size);
    lv_deinit();
}
