#include "h2h_ui.h"
#include "h2h_motion.h"
#include "h2h_tracks.h"
#include "h2h_setup.h"
#include "h2h_header.h"
#include "stayc_visual.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(h2h_font_16);
extern const lv_image_dsc_t *const h2h_icons[4];
#define CREAM 0xFFFCF2
#define INK 0x26466B
#define SKY 0xDCEFFA
#define BLUE 0x80BBE6
#define PINK 0xF7CDD9
#define LEMON 0xFFEA8A
static const char *home_names[] = {"小房间","随身听","星星接接乐","收藏册","应援牌","设置"};
static const char *card_names[] = {"初见晴空","柠檬心事","海风来信","比心时刻","粉色舞台","夏日星光","一起跳舞","心动满格"};
static const char *modes[] = {"顺序循环","随机播放","单曲循环"};
static const char *outfits[] = {"初始造型","运动短裤套装"};
static const char *rooms[] = {"汽水小屋","薄荷天台","糖果舞台"};
static const stayc_control_t *visuals;
static lv_obj_t *props;
static lv_obj_t *screen, *title, *hint, *sprite, *detail, *progress, *progress_text;
static h2h_header_t header;
static lv_obj_t *rows[10], *row_text[10], *falling[6], *basket, *toast, *hearts[4], *gesture_hearts[3];
static int old_page = -1;
static const pocket_profile_view_t *profile_view;
static uint64_t profile_revision;
static bool profile_available,profile_enabled;
static lv_image_dsc_t profile_image;
void h2h_ui_profile(const pocket_profile_view_t *view){
    bool available=view&&view->available,enabled=view&&view->enabled;
    uint64_t revision=available?view->revision:0;
    if(profile_available!=available||profile_enabled!=enabled||profile_revision!=revision){lv_image_cache_drop(&profile_image);old_page=-1;}
    profile_view=view;profile_available=available;profile_enabled=enabled;profile_revision=revision;
}
static bool setup_diagnostics, setup_confirm_debug, clean_setup;
static int setup_ble_error;
static bool setup_local_waiting;
void h2h_ui_setup_state(bool diagnostics, bool confirm_debug, int ble_error, bool local_waiting) {
    if(setup_diagnostics!=diagnostics || setup_confirm_debug!=confirm_debug || setup_ble_error!=ble_error || setup_local_waiting!=local_waiting) old_page=-1;
    setup_diagnostics=diagnostics; setup_confirm_debug=confirm_debug; setup_ble_error=ble_error;
    setup_local_waiting=local_waiting;
}
void h2h_ui_reset(void) { old_page=-1;screen=NULL; }
static unsigned old_selection = 999, old_room = 999, old_cards = 999, old_track = 999;
static int sprite_x,sprite_y;
static const unsigned char *resource_bytes;
static uint32_t resource_generation;
static h2h_resource_labels_t resource_labels;
static lv_image_dsc_t resource_sprites[3][5], resource_icons[4];
void h2h_ui_resources(const h2h_resource_view_t *view) {
    const unsigned char *bytes=view?view->bytes:NULL;
    uint32_t generation=view?view->generation:0;
    if(resource_bytes==bytes && resource_generation==generation)return;
    // The caller holds LVGL and retains the previous mapping through the rebuild.
    lv_image_cache_drop(NULL);
    resource_bytes=bytes;resource_generation=generation;old_page=-1;
    if(!view)return;
    resource_labels=view->labels;
    for(unsigned outfit=0;outfit<3;outfit++)for(unsigned pose=0;pose<5;pose++)
        resource_sprites[outfit][pose]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565A8,.w=64,.h=80,.stride=128},
            .data_size=15360,.data=bytes+512+(outfit*5+pose)*15360};
    for(unsigned i=0;i<4;i++)resource_icons[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565A8,.w=16,.h=16,.stride=32},
        .data_size=768,.data=bytes+512+15*15360+i*768};
}
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t bg,int radius) {
    lv_obj_t *o=lv_obj_create(parent); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(bg),0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_radius(o,radius,0); lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *text(lv_obj_t *parent,int x,int y,int w,const char *value,uint32_t ink) {
    lv_obj_t *o=lv_label_create(parent); lv_obj_set_pos(o,x,y); lv_obj_set_width(o,w);
    lv_obj_set_style_text_font(o,&h2h_font_16,0); lv_obj_set_style_text_color(o,lv_color_hex(ink),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT); lv_label_set_text(o,value); return o;
}
static void center_text(lv_obj_t *o) { lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0); }
static void room_decor(unsigned room) {
    stayc_visual_room(screen,room,visuals->decor);
}
static void make_sprite(int x,int y,int scale) {
    (void)scale;
    sprite=stayc_visual_actor(screen);props=stayc_visual_props(screen);
    sprite_x=x-32;sprite_y=y-40;
}
static void make_row(int index,int y,const char *name,bool selected,bool locked) {
    rows[index]=box(screen,16,y,208,32,selected?LEMON:0xFFFFFF,10);
    row_text[index]=text(rows[index],10,6,190,name,locked?0x98A4AF:INK);
}
static void build(const h2h_model_t *m, const h2h_cloud_status_t *cloud, bool ble_connected, int passkey) {
    lv_obj_t *previous=lv_screen_active();
    screen=lv_obj_create(NULL); lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen,240,320); lv_obj_set_style_bg_color(screen,lv_color_hex(CREAM),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0); lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    memset(rows,0,sizeof(rows)); memset(row_text,0,sizeof(row_text)); memset(falling,0,sizeof(falling));
    sprite=props=detail=progress=progress_text=basket=NULL; memset(hearts,0,sizeof(hearts)); memset(gesture_hearts,0,sizeof(gesture_hearts));
    clean_setup=m->page==H2H_ACCOUNT && (passkey>=0 || setup_local_waiting || (!m->play_allowed && !setup_diagnostics));
    if(clean_setup) {
        title=hint=toast=NULL;
        if(passkey>=0) {
            char pin[32];snprintf(pin,sizeof(pin),"BLE PIN %06d",passkey);
            center_text(text(screen,20,79,200,"Pair this phone",INK));
            center_text(text(screen,20,115,200,pin,INK));
            center_text(text(screen,20,154,200,"Confirm on your phone",INK));
        } else if(setup_local_waiting) {
            center_text(text(screen,20,105,200,"Preparing local test",INK));
            center_text(text(screen,20,145,200,"Hold OK to cancel",INK));
        } else if(setup_confirm_debug) {
            center_text(text(screen,20,92,200,"Open diagnostics?",INK));
            center_text(text(screen,20,126,200,"OK: confirm / arrows: cancel",INK));
        } else if(cloud && cloud->state==H2H_CLOUD_CLAIMED) {
            center_text(text(screen,20,88,200,cloud->message,INK));
            center_text(text(screen,20,126,200,"OK: confirm account",INK));
        } else {
            char entry[128];
            const char *payload=cloud && cloud->pair_url[0]?cloud->pair_url:NULL;
            if(!payload && h2h_setup_url(entry,sizeof(entry),cloud?cloud->device_id:NULL)) payload=entry;
            if(payload) {
                lv_obj_t *qr=lv_qrcode_create(screen);
                lv_qrcode_set_size(qr,192); lv_qrcode_set_quiet_zone(qr,true);
                lv_qrcode_set_dark_color(qr,lv_color_hex(INK));
                lv_qrcode_set_light_color(qr,lv_color_hex(0xFFFFFF));
                lv_obj_set_pos(qr,24,24);
                if(lv_qrcode_update(qr,payload,strlen(payload))!=LV_RESULT_OK)
                    center_text(text(screen,20,110,200,"QR unavailable",INK));
            } else center_text(text(screen,20,110,200,"Setup unavailable",INK));
        }
        /* Approved future alias: deployment and the published guide destination
         * remain prerequisites for claiming the phone setup works. */
        lv_obj_t *guide=text(screen,10,251,220,"https://ai-passport.randomdance.cn/pocket/guide",INK);
        lv_label_set_long_mode(guide,LV_LABEL_LONG_WRAP); center_text(guide);
        lv_screen_load(screen); if(previous) lv_obj_delete(previous); return;
    }
    title=text(screen,24,12,112,"Pocket",INK);
    h2h_header_create(&header,screen);
    hint=text(screen,20,286,200,"↑ ↓ 选择   ● 打开",INK); center_text(hint);
    const char *name="Pocket";
    switch(m->page) {
    case H2H_HOME:
        room_decor(m->save.room); make_sprite(88,94,512);
        for(unsigned i=0;i<6;i++) box(screen,91+(int)i*10,231,5,5,i==m->selection?INK:BLUE,3);
        rows[0]=box(screen,16,244,208,34,LEMON,12);
        text(rows[0],8,7,16,"←",INK); text(rows[0],184,7,16,"→",INK);
        center_text(text(rows[0],28,7,152,home_names[m->selection],INK));
        break;
    case H2H_ROOM: {
        name=stayc_members[visuals->member];
        if(visuals->view==STAYC_MEMBER_MENU) {
            name="选择成员";
            for(unsigned i=0;i<6;i++)make_row((int)i,47+(int)i*37,stayc_members[i],i==visuals->selection,false);
            lv_label_set_text(hint,"● 选择   长按返回");break;
        }
        if(visuals->view==STAYC_ACTION_MENU) {
            name="互动与生活";unsigned start=visuals->selection/5*5;
            for(unsigned i=start;i<13&&i<start+5;i++)make_row((int)(i-start),62+(int)(i-start)*42,i==12?"房间装饰":stayc_action_label(i),i==visuals->selection,false);
            lv_label_set_text(hint,"● 开始   长按返回");break;
        }
        if(visuals->view==STAYC_DECOR_MENU) {
            name="房间装饰";room_decor(m->save.room);
            const char *slots[]={"灯光","抱枕","盆栽","摆件"};char label[80];
            for(unsigned i=0;i<4;i++){snprintf(label,sizeof(label),"%s %s",slots[i],stayc_decor_labels[i][(visuals->decor>>(i*2))&3u]);make_row((int)i,58+(int)i*49,label,i==visuals->selection,false);}
            lv_label_set_text(hint,"● 更改   长按返回");break;
        }
        room_decor(m->save.room);
        if(visuals->view==STAYC_ACTION_PLAYER) {
            if(visuals->action==5||visuals->action==9){box(screen,52,184,116,29,0xb5d6e3,3);box(screen,57,207,106,12,0x9cc0d0,3);}
            if(visuals->action==7){box(screen,198,87,2,131,INK,0);box(screen,181,88,32,22,CREAM,0);box(screen,190,112,15,3,0xd17d54,0);}
            if(visuals->action==11){box(screen,12,174,216,68,0x91c59b,0);box(screen,28,209,188,21,0xb5d99d,0);box(screen,207,167,2,55,INK,0);box(screen,209,168,15,11,PINK,0);}
            make_sprite(88,94,512);detail=text(screen,20,250,200,stayc_action_label(visuals->action),INK);center_text(detail);
            lv_label_set_text(hint,"● 暂停   长按返回");break;
        }
        make_sprite(88,94,512);
        const char *controls[]={"成员","互动","换装","房间"};
        for(int i=0;i<4;i++) {
            rows[i]=box(screen,16+i*54,246,46,34,m->selection==(unsigned)i?LEMON:SKY,10);
            center_text(text(rows[i],3,8,40,controls[i],INK));
        }
        if(m->selection>=2) { detail=text(screen,22,221,196,m->selection==2?outfits[m->save.outfit%2]:rooms[m->save.room],INK); center_text(detail); }
        lv_label_set_text(hint,"↑ ↓ 切换   ● 互动"); break;
    }
    case H2H_MUSIC: {
        name="随身听"; text(screen,22,52,196,h2h_tracks[m->track].title,INK);
        char content[48], mode[80], countdown[80];
        snprintf(content,sizeof(content),"播放内容  %s",h2h_chorus_mode(m)?"副歌":"全曲");
        snprintf(mode,sizeof(mode),"播放模式  %s",modes[m->save.mode]);
        snprintf(countdown,sizeof(countdown),"开跳倒计时  %s",m->save.countdown&1u?"开":"关");
        bool paired=m->song_count<m->track_count;
        const char *labels[]={"▶ 播放器","选择歌曲",paired?content:mode,paired?mode:countdown,countdown};
        for(int i=0;i<(paired?5:4);i++) make_row(i,(paired?81:112)+i*39,labels[i],m->selection==(unsigned)i,false);
        lv_label_set_text(hint,"● 选择   长按返回"); break;
    }
    case H2H_PLAYER:
        name="随身听"; box(screen,12,43,216,179,SKY,18); make_sprite(88,106,512);
        text(screen,26,49,188,h2h_tracks[m->track].title,INK);
        progress=lv_bar_create(screen); lv_obj_set_pos(progress,24,229); lv_obj_set_size(progress,192,6);
        lv_bar_set_range(progress,0,1000); lv_obj_set_style_bg_color(progress,lv_color_hex(BLUE),LV_PART_INDICATOR);
        progress_text=text(screen,24,241,192,"",INK); center_text(progress_text);
        detail=text(screen,20,263,200,"",INK); center_text(detail);
        lv_label_set_text(hint,"短按音量  三连切歌"); break;
    case H2H_TRACKS: {
        name=h2h_chorus_mode(m)?"选择副歌":"选择全曲";
        unsigned start=(m->selection/5)*5;
        for(unsigned i=start;i<m->song_count && i<start+5;i++) make_row((int)(i-start),62+(int)(i-start)*42,h2h_tracks[i].title,i==m->selection,false);
        lv_label_set_text(hint,"▶ 播放   长按返回"); break;
    }
    case H2H_GAME:
        name="星星接接乐"; box(screen,12,43,216,232,SKY,18);
        detail=text(screen,24,50,192,"",INK); center_text(detail);
        for(int i=0;i<5;i++) box(screen,27+i*40,79,2,165,0xC9E4F3,0);
        for(int i=0;i<6;i++) { falling[i]=text(screen,0,0,20,"●",LEMON); }
        basket=box(screen,0,251,32,14,PINK,5);
        lv_label_set_text(hint,"上键左 下键右 确定暂停"); break;
    case H2H_RESULT: {
        name="接住小幸运"; room_decor(m->save.room); make_sprite(88,100,512);
        char result[100]; snprintf(result,sizeof(result),"接住 %u 个  获得 %u 爱心",m->caught,m->game_reward);
        detail=text(screen,20,246,200,result,INK); center_text(detail);
        lv_label_set_text(hint,"● 再玩   长按返回"); break;
    }
    case H2H_COLLECTION: {
        name="收藏册";
        char subtitle[80]; snprintf(subtitle,sizeof(subtitle),"已收藏 %u/8  每5爱心解锁",h2h_card_count(m));
        detail=text(screen,20,45,200,subtitle,INK); center_text(detail);
        for(int i=0;i<8;i++) {
            bool unlocked=(m->save.cards&(1u<<i))!=0;
            rows[i]=box(screen,16+(i%2)*108,73+(i/2)*44,100,39,m->selection==(unsigned)i?LEMON:unlocked?SKY:0xEEEDE8,10);
            row_text[i]=text(rows[i],5,12,90,unlocked?card_names[i]:"等待心动",unlocked?INK:0x98A4AF); center_text(row_text[i]);
        }
        char outfit[48], room[48];
        snprintf(outfit,sizeof(outfit),"服装 %lu/2",(unsigned long)(m->save.outfit%2)+1);
        snprintf(room,sizeof(room),"背景 %lu/3",(unsigned long)m->save.room+1);
        for(int i=8;i<10;i++) { rows[i]=box(screen,16+(i-8)*108,253,100,27,m->selection==(unsigned)i?LEMON:SKY,8); center_text(text(rows[i],4,4,92,i==8?outfit:room,INK)); }
        lv_label_set_text(hint,"● 选择   长按返回"); break;
    }
    case H2H_CARD:
        name="心动收藏"; box(screen,18,44,204,226,m->selection%2?PINK:SKY,16);
        center_text(text(screen,29,55,182,card_names[m->selection],INK)); make_sprite(88,111,512);
        text(screen,44,239,152,stayc_members[visuals->member],INK);
        lv_label_set_text(hint,"♥ 设为应援牌"); break;
    case H2H_BADGE:
        name=stayc_members[visuals->member]; room_decor(m->save.room); make_sprite(88,113,512);
        center_text(text(screen,22,51,196,stayc_members[visuals->member],INK));
        detail=text(screen,20,251,200,m->save.badge_card<8?card_names[m->save.badge_card]:"Pocket",INK); center_text(detail);
        for(int i=0;i<4;i++) hearts[i]=text(screen,27+i*53,103+i*29,20,"♥",0xEAAFCA);
        lv_label_set_text(hint,"长按确定返回"); break;
    case H2H_SETTINGS: {
        name="设置";
        char volume[64], countdown[64], mode[80]; snprintf(volume,sizeof(volume),"音量  %lu%%",(unsigned long)m->save.volume);
        snprintf(countdown,sizeof(countdown),"倒计时  %s",m->save.countdown&1u?"开":"关");
        snprintf(mode,sizeof(mode),"模式  %s",modes[m->save.mode]);
        const char *labels[]={volume,countdown,mode,"用户信息"};
        for(int i=0;i<4;i++) make_row(i,58+i*48,labels[i],m->selection==(unsigned)i,false);
        lv_label_set_text(hint,"● 更改   长按返回"); break;
    }
    case H2H_ACCOUNT: {
        name="用户信息";
        char line[90];
        if(setup_ble_error) snprintf(line,sizeof(line),"BLE init error %d; OK retries",setup_ble_error);
        else if(cloud&&cloud->bound&&profile_available)snprintf(line,sizeof(line),"上次手机同步的资料");
        else snprintf(line,sizeof(line),"%s",cloud ? cloud->message : "Starting");
        center_text(text(screen,20,45,200,line,INK));
        if (cloud && cloud->pair_url[0]) {
            lv_obj_t *qr=lv_qrcode_create(screen);
            lv_qrcode_set_size(qr,144);
            lv_qrcode_set_dark_color(qr,lv_color_hex(INK));
            lv_qrcode_set_light_color(qr,lv_color_hex(0xFFFFFF));
            lv_obj_set_pos(qr,48,62);
            if (lv_qrcode_update(qr,cloud->pair_url,strlen(cloud->pair_url))!=LV_RESULT_OK)
                center_text(text(screen,20,188,200,"QR unavailable",INK));
        } else {
            box(screen,20,76,200,129,SKY,14);
            if(cloud&&cloud->bound&&profile_available&&profile_view){
                name="用户信息";
                memset(&profile_image,0,sizeof(profile_image));profile_image.header.magic=LV_IMAGE_HEADER_MAGIC;
                profile_image.header.cf=LV_COLOR_FORMAT_RGB565;profile_image.header.w=64;profile_image.header.h=64;profile_image.header.stride=128;
                profile_image.data_size=POCKET_PROFILE_BYTES;profile_image.data=profile_view->pixels;
                lv_obj_t *image=lv_image_create(screen);lv_obj_set_pos(image,29,80);lv_image_set_src(image,&profile_image);
                lv_obj_t *username=text(screen,102,87,108,profile_view->meta.username,INK);
                lv_obj_set_height(username,48);
                snprintf(line,sizeof(line),"UID %s",profile_view->meta.uid);center_text(text(screen,29,149,182,line,INK));
                if(profile_view->meta.points_available)snprintf(line,sizeof(line),"上次积分 %lu",(unsigned long)profile_view->meta.points);
                else snprintf(line,sizeof(line),"账户积分 暂不可用");
                center_text(text(screen,29,176,182,line,INK));
            }else center_text(text(screen,29,101,182,cloud&&cloud->bound?(profile_enabled?"等待手机同步资料":"资料存储待启用"):"CONNECT PHONE BY BLE",INK));
            if ((!cloud||!cloud->bound||!profile_available) && cloud && cloud->device_id[0]) {
                snprintf(line,sizeof(line),"RDP-%s",cloud->device_id+24);
                center_text(text(screen,29,137,182,line,INK));
            }
        }
        if (passkey>=0) {
            snprintf(line,sizeof(line),"BLE PIN %06d",passkey);
            center_text(text(screen,18,207,204,line,INK));
        } else if (ble_connected) center_text(text(screen,18,207,204,"BLE CONNECTED",INK));
        const char *actions[]={"Confirm","Sync","Forget","Unbind","Status","Local"};
        for(int i=0;i<(setup_diagnostics?6:5);i++) {
            rows[i]=box(screen,9+(i%3)*75,225+(i/3)*30,71,28,m->selection==(unsigned)i?LEMON:SKY,8);
            center_text(text(rows[i],2,7,67,actions[i],INK));
        }
        lv_label_set_text(hint,m->selection==4?"Auto sync; conflict protected":m->activation_required && !m->play_allowed?"Activation required":"Hold OK to return");
        break;
    }
    }
    if(sprite && m->page!=H2H_CARD) for(int i=0;i<3;i++) gesture_hearts[i]=text(screen,(i==0?40:i==1?188:174),100+i*18,20,"♥",0xEAAFCA);
    lv_label_set_text(title,name);
    toast=text(screen,20,270,200,"",INK); center_text(toast);
    lv_obj_set_style_bg_color(toast,lv_color_hex(LEMON),0); lv_obj_set_style_bg_opa(toast,LV_OPA_COVER,0);
    lv_obj_add_flag(toast,LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(screen); if(previous) lv_obj_delete(previous);
}
void h2h_ui_render(const h2h_model_t *m,unsigned position,bool playing,int battery,bool storage_ok,bool input_ok,unsigned ticks,
                   const h2h_cloud_status_t *cloud,bool ble_connected,int passkey,const stayc_control_t *control) {
    visuals=control;
    static unsigned old_view=999,old_visual_selection=999,old_member=999,old_decor=999;
    static unsigned last_volume=999,last_mode=999,last_countdown=999,last_outfit_value=999;
    static h2h_cloud_status_t last_cloud;
    static bool last_ble_connected;
    static int last_passkey=-2;
    bool rebuild=!screen || lv_screen_active()!=screen || (int)m->page!=old_page || m->selection!=old_selection || m->save.room!=old_room || m->save.cards!=old_cards || m->track!=old_track;
    if(old_view!=visuals->view||old_visual_selection!=visuals->selection||old_member!=visuals->member||old_decor!=visuals->decor)rebuild=true;
    if ((m->page==H2H_SETTINGS || m->page==H2H_MUSIC) && (last_volume!=m->save.volume || last_mode!=m->save.mode || last_countdown!=m->save.countdown)) rebuild=true;
    if((m->page==H2H_ROOM || m->page==H2H_COLLECTION) && last_outfit_value!=m->save.outfit) rebuild=true;
    if(m->page==H2H_ACCOUNT && (memcmp(&last_cloud,cloud,sizeof(last_cloud)) ||
        last_ble_connected!=ble_connected || last_passkey!=passkey)) rebuild=true;
    if(rebuild) build(m,cloud,ble_connected,passkey);
    old_view=visuals->view;old_visual_selection=visuals->selection;old_member=visuals->member;old_decor=visuals->decor;
    if(cloud) last_cloud=*cloud;
    last_ble_connected=ble_connected; last_passkey=passkey;
    old_page=m->page; old_selection=m->selection; old_room=m->save.room; old_cards=m->save.cards; old_track=m->track;
    last_volume=m->save.volume; last_mode=m->save.mode; last_countdown=m->save.countdown; last_outfit_value=m->save.outfit;
    char line[100];
    if(clean_setup) return;
    h2h_header_update(&header,title,m->save.hearts,battery,false,ble_connected);
    if(sprite) {
        h2h_motion_t motion=h2h_motion(m,playing);
        stayc_frame_t frame={.pose=motion.pose,.x=sprite_x+motion.dx,.y=sprite_y+motion.dy,.scale_q8=512};unsigned action=99;
        if(m->page==H2H_CARD)frame.pose=stayc_reaction_pose(m->selection,m->animation_ms);
        if(m->page==H2H_BADGE&&m->save.badge_card<8&&!playing)frame.pose=stayc_reaction_pose(m->save.badge_card,m->animation_ms);
        if(visuals->view==STAYC_ACTION_PLAYER){action=visuals->action;stayc_sample(visuals->member,m->save.outfit%2,action,visuals->elapsed,&frame);
            lv_label_set_text(hint,frame.done?"● 重播   长按返回":visuals->paused?"● 继续   长按返回":"● 暂停   长按返回");}
        if(!stayc_visual_update(sprite,props,visuals->member,m->save.outfit%2,action,&frame))lv_label_set_text(hint,"角色资源校验失败");
        for(int i=0;i<3;i++) if(gesture_hearts[i]) {
            if(motion.hearts) { lv_obj_remove_flag(gesture_hearts[i],LV_OBJ_FLAG_HIDDEN); lv_obj_set_y(gesture_hearts[i],112+i*14-(int)((m->animation_ms/60+(unsigned)i*7)%24)); }
            else lv_obj_add_flag(gesture_hearts[i],LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(m->page==H2H_PLAYER) {
        unsigned total=h2h_tracks[m->track].duration_ms;
        lv_bar_set_value(progress,total?(int)((uint64_t)position*1000/total):0,LV_ANIM_OFF);
        snprintf(line,sizeof(line),"%u:%02u / %u:%02u",position/60000,(position/1000)%60,total/60000,(total/1000)%60); lv_label_set_text(progress_text,line);
        if(m->countdown_ms) snprintf(line,sizeof(line),"准备开跳  %lu",(unsigned long)((m->countdown_ms+999)/1000));
        else if(m->audio_error) snprintf(line,sizeof(line),"播放失败  确定重试");
        else snprintf(line,sizeof(line),"%s  音量%lu%%",playing?"正在播放":"已暂停",(unsigned long)m->save.volume);
        lv_label_set_text(detail,line);
    }
    if(m->page==H2H_GAME) {
        snprintf(line,sizeof(line),"%s %lus   接住 %u",m->game_paused?"暂停":"剩余",(unsigned long)((m->game_ms+999)/1000),m->caught); lv_label_set_text(detail,line);
        lv_obj_set_x(basket,22+(int)m->basket*40);
        for(int i=0;i<6;i++) {
            if(m->drops[i].active) { lv_obj_remove_flag(falling[i],LV_OBJ_FLAG_HIDDEN); lv_obj_set_pos(falling[i],29+m->drops[i].lane*40,79+m->drops[i].y); lv_obj_set_style_text_color(falling[i],lv_color_hex(m->drops[i].heart?0xEAAFCA:0xEAB94A),0); lv_label_set_text(falling[i],m->drops[i].heart?"♥":"●"); }
            else lv_obj_add_flag(falling[i],LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(m->page==H2H_BADGE) for(int i=0;i<4;i++) lv_obj_set_y(hearts[i],95+(int)((ticks/110+(unsigned)i*39)%125));
    if(!input_ok || !storage_ok || (m->pose_ms && m->pose==H2H_CELEBRATE && m->last_unlock<8)) {
        const char *message=!input_ok?"按键不可用，请重启":!storage_ok?"存档失败，稍后重试":"解锁了一张新收藏卡";
        lv_label_set_text(toast,message); lv_obj_remove_flag(toast,LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(toast,LV_OBJ_FLAG_HIDDEN);
}
