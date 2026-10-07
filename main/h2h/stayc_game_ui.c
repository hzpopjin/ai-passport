/* New STAYC UI using genuine member art and the supplied V4 screen/scene
 * contracts. This is a native port, not recovered full original source. */
#include "stayc_game_ui.h"
#include "stayc_game_visual.h"
#include "stayc_game_decor.h"
#include "stayc_game_life.h"
#include "stayc_game_reaction.h"
#include "h2h_tracks.h"
#include "h2h_header.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(h2h_font_16);
#define CREAM 0xfffcf2u
#define INK 0x26466bu
#define SKY 0xdceffau
#define BLUE 0x80bbe6u
#define PINK 0xf7cdd9u
#define LEMON 0xffea8au
#define MUTED 0x6b8296u

static const char *const home_names[]={"小房间","随身听","口袋游乐场","收藏册","应援牌","设置","用户信息"};
static const char *const room_actions[]={"打个招呼","比个心","逛逛衣橱","挑选背景","一起生活","布置房间","选择成员","丰富互动"};
static const char *const journey_names[]={"向成员打招呼","接接乐达到80分","记住4步舞蹈","完成3场游戏","听满两分钟","收藏4张卡片"};
static const char *const card_names[]={"初见晴空","柠檬心事","海风来信","比心时刻","粉色舞台","夏日星光","一起跳舞","心动满格"};
static const char *const modes[]={"顺序循环","随机播放","单曲循环"};
static const char *const outfit_names[]={"初始造型","运动短裤套装"};
static const char *const room_names[]={"汽水小屋","薄荷天台","糖果舞台"};
static lv_obj_t *screen,*title,*hint,*detail,*progress,*progress_text,*toast;
static h2h_header_t header;
static bool phone_connected;
static lv_obj_t *rows[10],*row_text[10],*falling[6],*basket;
static lv_obj_t *background,*actor,*foreground;
static int actor_x,actor_y;
static unsigned actor_scale;
static bool compact_actor,force_rebuild=true;
static struct {
    int page;
    unsigned selection,track,member,view,visual_selection,action,life_kind,decor_slot,settled_claims;
    stayc_game_save_t save;
    stayc_game_shop_status_t shop_status;
    unsigned shop_item;
    bool volume_edit,debug_mode;
} previous={.page=-1};

bool stayc_game_ui_input(stayc_game_t *m,stayc_control_t *v,h2h_key_t key,h2h_event_t event) {
    if(!m||!v||(unsigned)key>H2H_OK||(unsigned)event>H2H_LONG)return false;
    if(m->shop_status==STAYC_GAME_SHOP_PENDING)return false;
    /* Product-only navigation surrounds the unchanged v15 gameplay engine. */
    if(m->page==STAYC_GAME_HOME){
        if(key!=H2H_OK&&event==H2H_PRESS){m->selection=(m->selection+(key==H2H_UP?6:1))%7;return true;}
        if(key==H2H_OK&&event==H2H_CLICK&&!m->suppress_ok&&m->selection==6){stayc_game_account_open(m,true);return true;}
    }
    if(m->page==STAYC_GAME_ACCOUNT&&key==H2H_OK&&event==H2H_LONG){
        m->suppress_ok=true;stayc_game_account_back(m);return true;
    }
    if(m->page!=STAYC_GAME_ROOM){v->view=STAYC_NORMAL;v->paused=false;v->elapsed=0;}
    if(m->page==STAYC_GAME_WARDROBE) {
        if(key!=H2H_OK&&event==H2H_PRESS){
            unsigned current=m->selection<2?m->selection:(key==H2H_UP?0:1);
            m->selection=(current+1)%2;m->shop_item=m->selection;m->shop_status=STAYC_GAME_SHOP_IDLE;
            return true;
        }
        /* A retained legacy third item is never sold as fictional STAYC art. */
        if(m->selection>=2&&key==H2H_OK&&event==H2H_CLICK&&!m->suppress_ok)return true;
        return false;
    }
    if(m->page!=STAYC_GAME_ROOM)return false;
    if(v->view==STAYC_NORMAL) {
        if(key!=H2H_OK&&event==H2H_PRESS){m->selection=(m->selection+(key==H2H_UP?7:1))%8;return true;}
        if(key==H2H_OK&&event==H2H_CLICK&&!m->suppress_ok&&m->selection>=6){
            v->view=m->selection==6?STAYC_MEMBER_MENU:STAYC_ACTION_MENU;
            v->selection=m->selection==6?v->member%STAYC_MEMBERS:0;v->elapsed=0;v->paused=false;v->suppress_ok=false;return true;
        }
        return false;
    }
    if(key==H2H_OK) {
        if(event==H2H_PRESS){v->suppress_ok=false;m->suppress_ok=false;return true;}
        if(event==H2H_LONG){
            v->suppress_ok=m->suppress_ok=true;
            if(v->view==STAYC_ACTION_PLAYER){v->view=STAYC_ACTION_MENU;v->selection=v->action;}
            else v->view=STAYC_NORMAL;
            v->elapsed=0;v->paused=false;return true;
        }
        if(event!=H2H_CLICK||v->suppress_ok||m->suppress_ok)return true;
        if(v->view==STAYC_MEMBER_MENU){v->preferences_dirty|=v->member!=v->selection;v->member=v->selection%STAYC_MEMBERS;v->view=STAYC_NORMAL;}
        else if(v->view==STAYC_ACTION_MENU){v->action=v->selection%8;v->elapsed=0;v->paused=false;v->view=STAYC_ACTION_PLAYER;}
        else if(v->view==STAYC_ACTION_PLAYER){if(v->elapsed>=stayc_action_duration(v->action)){v->elapsed=0;v->paused=false;}else v->paused=!v->paused;}
        return true;
    }
    if(event==H2H_PRESS&&v->view!=STAYC_ACTION_PLAYER){unsigned n=v->view==STAYC_MEMBER_MENU?STAYC_MEMBERS:8;v->selection=(v->selection+(key==H2H_UP?n-1:1))%n;}
    return true;
}
void stayc_game_ui_tick(stayc_game_t *m,stayc_control_t *v,uint32_t ms) {
    if(!m||!v)return;
    if(m->page!=STAYC_GAME_ROOM){v->view=STAYC_NORMAL;v->elapsed=0;v->paused=false;return;}
    if(v->view==STAYC_ACTION_PLAYER&&!v->paused){unsigned duration=stayc_action_duration(v->action);if(v->elapsed>=duration||ms>=duration-v->elapsed)v->elapsed=duration;else v->elapsed+=ms;}
}
void stayc_game_ui_reset(void) { screen=NULL;force_rebuild=true; }
void stayc_game_ui_phone_connection(bool connected) { phone_connected=connected; }

static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius) {
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,radius,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
static lv_obj_t *text(lv_obj_t *parent,int x,int y,int width,const char *value,uint32_t color) {
    lv_obj_t *o=lv_label_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,width);
    lv_obj_set_style_text_font(o,&h2h_font_16,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);lv_label_set_text(o,value);return o;
}
static void centered(lv_obj_t *o) { lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0); }
static void row(unsigned index,int y,int height,const char *label,bool selected,bool locked) {
    rows[index]=box(screen,16,y,208,height,selected?LEMON:locked?0xeeede8u:0xffffffu,9);
    row_text[index]=text(rows[index],8,(height-18)/2,192,label,locked?MUTED:INK);
}
static void scene(bool room,int x,int y,unsigned scale,bool compact) {
    if(room)background=stayc_game_visual_background(screen);
    actor=stayc_game_visual_actor(screen);foreground=stayc_game_visual_foreground(screen);
    actor_x=x;actor_y=y;actor_scale=scale;compact_actor=compact;
}
static const char *item_name(stayc_game_shop_kind_t kind,unsigned item) {
    if(kind==STAYC_GAME_SHOP_OUTFIT)return item<2?outfit_names[item]:"旧版造型记录";
    if(kind==STAYC_GAME_SHOP_ROOM)return item<3?room_names[item]:"";
    const char *name=stayc_game_shop_name(kind,item);
    return !strcmp(name,"心心抱枕")?"爱心抱枕":name;
}
static void wallet(const stayc_game_t *m) {
    char value[64];snprintf(value,sizeof(value),"本地星星  %lu",(unsigned long)m->save.star_balance);
    lv_obj_t *o=box(screen,16,43,208,27,LEMON,9);centered(text(o,4,3,200,value,INK));
}
static void wardrobe_stage(void) {
    box(screen,12,75,216,136,0xf6e7ea,16);
    box(screen,23,82,20,114,0xeac6d2,5);box(screen,197,82,20,114,0xeac6d2,5);
    box(screen,54,198,132,6,0xd8b4c3,3);box(screen,66,191,108,7,0xffffff,3);
    box(screen,164,94,21,75,0xddbca1,9);box(screen,168,98,13,65,SKY,5);
    box(screen,171,102,3,44,0xf8fcff,0);box(screen,173,169,4,13,0xddbca1,0);
    box(screen,167,182,16,3,0xddbca1,0);
}
static void picker(const stayc_game_t *m,stayc_game_shop_kind_t kind,unsigned item,unsigned count) {
    lv_obj_t *o=box(screen,16,217,208,30,0xffffff,9);
    text(o,8,5,16,"←",INK);text(o,184,5,16,"→",INK);centered(text(o,26,5,156,item_name(kind,item),INK));
    char line[112];bool owned=stayc_game_shop_owned(m,kind,item);
    bool equipped=kind==STAYC_GAME_SHOP_OUTFIT?m->save.outfit==item:kind==STAYC_GAME_SHOP_ROOM?m->save.room==item:stayc_game_decor_value(m->save.decor,item/4)==item%4;
    if(kind==STAYC_GAME_SHOP_OUTFIT&&item>=2)snprintf(line,sizeof(line),"保留旧记录，请选新造型");
    else if(m->shop_status!=STAYC_GAME_SHOP_IDLE)snprintf(line,sizeof(line),"%s",stayc_game_shop_status_text(m));
    else if(owned)snprintf(line,sizeof(line),"%s",equipped?"正在使用":"已拥有 · 确定使用");
    else snprintf(line,sizeof(line),"%u 星星 · 确定兑换",stayc_game_shop_price(kind,item));
    detail=text(screen,20,251,200,line,m->shop_status==STAYC_GAME_SHOP_FUNDS||m->shop_status==STAYC_GAME_SHOP_FAILED?0xaf5b63:INK);centered(detail);
    for(unsigned i=0;i<count;i++)box(screen,120-(int)count*6+(int)i*12,276,5,3,i==m->selection?INK:BLUE,1);
}
static void build(const stayc_game_t *m,const stayc_control_t *v) {
    lv_obj_t *old=lv_screen_active();
    screen=lv_obj_create(NULL);lv_obj_remove_style_all(screen);lv_obj_set_size(screen,240,320);
    lv_obj_set_style_bg_color(screen,lv_color_hex(CREAM),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    memset(rows,0,sizeof(rows));memset(row_text,0,sizeof(row_text));memset(falling,0,sizeof(falling));
    detail=progress=progress_text=basket=background=actor=foreground=NULL;compact_actor=false;
    title=text(screen,22,12,112,"Pocket",INK);h2h_header_create(&header,screen);
    box(screen,16,285,208,1,BLUE,0);hint=text(screen,20,291,200,stayc_game_footer_hint(m),INK);centered(hint);
    const char *name="Pocket";char line[144];unsigned member=v?v->member%6:0;
    switch(m->page) {
    case STAYC_GAME_HOME:
        scene(true,56,60,512,false);name=m->selection%7==6?"用户信息":"小房间";
        for(unsigned i=0;i<7;i++)box(screen,85+(int)i*10,231,5,5,i==m->selection?INK:BLUE,3);
        row(0,246,33,home_names[m->selection%7],true,false);centered(row_text[0]);break;
    case STAYC_GAME_ROOM:
        name=stayc_members[member];
        if(v&&v->view==STAYC_MEMBER_MENU){name="选择成员";for(unsigned i=0;i<6;i++)row(i,47+(int)i*37,32,stayc_members[i],i==v->selection,false);break;}
        if(v&&v->view==STAYC_ACTION_MENU){name="丰富互动";unsigned start=v->selection/5*5;for(unsigned i=start;i<8&&i<start+5;i++)row(i-start,62+(int)(i-start)*42,32,stayc_action_label(i),i==v->selection,false);break;}
        scene(true,56,60,512,false);
        if(v&&v->view==STAYC_ACTION_PLAYER){name=stayc_action_label(v->action);detail=text(screen,20,250,200,"",INK);centered(detail);break;}
        centered(text(screen,20,222,200,m->selection==2?item_name(STAYC_GAME_SHOP_OUTFIT,m->save.outfit):m->selection==3?item_name(STAYC_GAME_SHOP_ROOM,m->save.room):m->selection==6?"六位成员，一起在小屋":m->selection==7?"八种互动，慢慢陪伴":"",INK));
        row(0,246,33,room_actions[m->selection%8],true,false);centered(row_text[0]);break;
    case STAYC_GAME_WARDROBE:
        name="衣橱";wallet(m);wardrobe_stage();scene(false,56,60,512,true);picker(m,STAYC_GAME_SHOP_OUTFIT,m->selection,2);break;
    case STAYC_GAME_ROOM_PICKER:
        name="背景陈列";wallet(m);scene(true,56,60,512,true);picker(m,STAYC_GAME_SHOP_ROOM,m->selection,3);break;
    case STAYC_GAME_DECOR_PICKER:
        name=stayc_game_decor_slot_name(m->decor_slot);wallet(m);scene(true,56,60,512,true);picker(m,STAYC_GAME_SHOP_DECOR,m->decor_slot*4+m->selection,4);break;
    case STAYC_GAME_REDEEM: {
        name="星星兑换";wallet(m);box(screen,16,79,208,148,0xffffff,13);
        centered(text(screen,26,90,188,item_name(m->shop_kind,m->shop_item),INK));
        unsigned price=stayc_game_shop_price(m->shop_kind,m->shop_item);
        snprintf(line,sizeof(line),"兑换需要  %u 星星",price);centered(text(screen,26,120,188,line,INK));
        snprintf(line,sizeof(line),m->save.star_balance>=price?"兑换后剩余  %lu":"还差 %lu 星星",(unsigned long)(m->save.star_balance>=price?m->save.star_balance-price:price-m->save.star_balance));centered(text(screen,26,150,188,line,INK));
        centered(text(screen,26,186,188,m->debug_mode?"DEV 体验 · 不改真实存档":"兑换后立即使用",MUTED));
        const char *choices[]={"再看看","确认兑换"};for(unsigned i=0;i<2;i++){rows[i]=box(screen,16+(int)i*108,244,100,33,m->selection==i?LEMON:SKY,9);centered(text(rows[i],2,6,96,m->shop_status==STAYC_GAME_SHOP_PENDING&&i==1?"保存中":choices[i],INK));}break;
    }
    case STAYC_GAME_DECOR:
        name="布置房间";scene(true,56,60,512,false);centered(text(screen,20,222,200,"灯光 · 抱枕 · 盆栽 · 小摆件",INK));
        snprintf(line,sizeof(line),"%s  %s",stayc_game_decor_slot_name(m->selection),item_name(STAYC_GAME_SHOP_DECOR,m->selection*4+stayc_game_decor_value(m->save.decor,m->selection)));row(0,246,33,line,true,false);centered(row_text[0]);break;
    case STAYC_GAME_LIFE_MENU:
        name="一起生活";centered(text(screen,20,48,200,"慢慢来，一起过小日子",INK));
        for(unsigned i=0;i<4;i++){snprintf(line,sizeof(line),"%s  %s",m->save.activity_flags&(4u<<i)?"♥":"·",stayc_game_life_name(i));row(i,80+(int)i*42,32,line,m->selection==i,false);}
        centered(text(screen,20,256,200,"初次完成 +1爱心 +1星星",INK));break;
    case STAYC_GAME_LIFE:
        name=stayc_game_life_name(m->life_kind);scene(true,56,60,512,false);
        detail=text(screen,22,50,196,"",INK);centered(detail);
        for(unsigned i=0;i<2;i++){rows[i]=box(screen,16+(int)i*108,247,100,30,m->selection==i?LEMON:SKY,8);centered(text(rows[i],2,6,96,stayc_game_life_choice(m->life_kind,i),INK));}
        progress_text=text(screen,20,253,200,"",INK);centered(progress_text);break;
    case STAYC_GAME_MUSIC: {
        name="随身听";text(screen,22,52,196,h2h_tracks[m->track%h2h_track_count].title,INK);
        char content[64],mode[80],countdown[64];snprintf(content,sizeof(content),"播放内容  %s",stayc_game_chorus_mode(m)?"副歌":"全曲");snprintf(mode,sizeof(mode),"播放模式  %s",modes[m->save.mode%3]);snprintf(countdown,sizeof(countdown),"开跳倒计时  %s",m->save.countdown&1u?"开":"关");
        bool paired=m->song_count<m->track_count;const char *labels[]={"▶ 播放器","选择歌曲",paired?content:mode,paired?mode:countdown,countdown};
        for(unsigned i=0;i<(paired?5u:4u);i++)row(i,(paired?81:112)+(int)i*39,32,labels[i],m->selection==i,false);
        break;
    }
    case STAYC_GAME_PLAYER:
        name="随身听";box(screen,12,43,216,179,SKY,16);scene(false,56,72,512,false);text(screen,26,49,188,h2h_tracks[m->track%h2h_track_count].title,INK);
        progress=lv_bar_create(screen);lv_obj_set_pos(progress,24,229);lv_obj_set_size(progress,192,6);lv_bar_set_range(progress,0,1000);lv_obj_set_style_bg_color(progress,lv_color_hex(BLUE),LV_PART_INDICATOR);
        progress_text=text(screen,24,239,192,"",INK);centered(progress_text);detail=text(screen,20,262,200,"",INK);centered(detail);break;
    case STAYC_GAME_TRACKS: {
        name=stayc_game_chorus_mode(m)?"选择副歌":"选择全曲";unsigned start=m->selection/5*5;
        for(unsigned i=start;i<m->song_count&&i<start+5;i++)row(i-start,62+(int)(i-start)*42,32,h2h_tracks[i%h2h_track_count].title,i==m->selection,false);
        break;
    }
    case STAYC_GAME_ARCADE: {
        name="口袋游乐场";centered(text(screen,20,49,200,"离线也能一起玩",INK));const char *labels[]={"柠檬接接乐","一起跳舞","心动护照"};for(unsigned i=0;i<3;i++)row(i,83+(int)i*49,32,labels[i],m->selection==i,false);
        const char *tips[]={"●1分 ♥3分 连击加分","记住动作，再按左右键","每枚章 +2爱心 +2星星"};centered(text(screen,20,219,200,tips[m->selection%3],INK));
        snprintf(line,sizeof(line),"最高 %lu分  默契 %lu步",(unsigned long)m->save.best_score,(unsigned long)m->save.echo_best);centered(text(screen,20,241,200,line,INK));break;
    }
    case STAYC_GAME_JOURNEY:
        name="心动护照";for(unsigned i=0;i<6;i++)row(i,49+(int)i*34,31,"",m->selection==i,((m->save.journey_claims|m->settled_claims)&(1u<<i))!=0);detail=text(screen,20,260,200,"",INK);centered(detail);break;
    case STAYC_GAME_ECHO:
        name="一起跳舞";box(screen,12,43,216,232,SKY,16);detail=text(screen,24,51,192,"",INK);centered(detail);scene(false,88,70,256,false);
        progress_text=text(screen,24,151,192,"",INK);centered(progress_text);
        for(unsigned i=0;i<2;i++){rows[i]=box(screen,34+(int)i*96,186,76,49,0xffffff,11);row_text[i]=text(rows[i],8,14,60,i?"右 →":"← 左",INK);centered(row_text[i]);}
        row_text[2]=text(screen,24,246,192,"",INK);centered(row_text[2]);break;
    case STAYC_GAME_GAME:
        name="柠檬接接乐";box(screen,12,43,216,232,SKY,16);detail=text(screen,24,48,192,"",INK);centered(detail);progress_text=text(screen,24,69,192,"",INK);centered(progress_text);
        for(int i=0;i<5;i++)box(screen,27+i*40,95,2,152,0xc9e4f3,0);
        for(int i=0;i<6;i++)falling[i]=text(screen,0,0,20,"●",LEMON);
        basket=box(screen,0,251,32,14,PINK,5);break;
    case STAYC_GAME_RESULT:
        name=m->game_kind?"默契小舞台":"接住小幸运";box(screen,12,43,216,195,m->new_best?LEMON:SKY,16);scene(false,72,69,384,false);
        centered(text(screen,20,53,200,m->new_best?"新纪录！":m->game_reward>=4?"闪闪发光！":"再来一起玩",INK));
        if(m->game_kind)snprintf(line,sizeof(line),"记住 %u步  完成 %u轮",m->echo_best,m->echo_round);else snprintf(line,sizeof(line),"%u分  最长连击 %u",m->score,m->best_combo);centered(text(screen,20,193,200,line,INK));
        snprintf(line,sizeof(line),"爱心 +%u  星星 +%u",m->game_reward,m->game_reward);centered(text(screen,20,216,200,line,INK));
        if(stayc_game_card_count(m)<8)snprintf(line,sizeof(line),"再得 %lu 爱心解锁卡片",(unsigned long)(5-m->save.hearts%5));else snprintf(line,sizeof(line),"收藏已满  挑战新纪录");centered(text(screen,20,251,200,line,INK));break;
    case STAYC_GAME_COLLECTION:
        name="收藏册";snprintf(line,sizeof(line),"已收藏 %u/8  每5爱心解锁",stayc_game_card_count(m));detail=text(screen,20,45,200,line,INK);centered(detail);
        for(unsigned i=0;i<8;i++){bool owned=(m->save.cards&(1u<<i))!=0;rows[i]=box(screen,16+(int)(i%2)*108,73+(int)(i/2)*44,100,39,m->selection==i?LEMON:owned?SKY:0xeeede8,9);row_text[i]=text(rows[i],4,12,92,owned?card_names[i]:"等待心动",owned?INK:MUTED);centered(row_text[i]);}
        for(unsigned i=8;i<10;i++){unsigned owned=i==8?(m->save.owned_outfits&1u)+((m->save.owned_outfits>>1)&1u):stayc_game_room_count(m);snprintf(line,sizeof(line),i==8?"衣橱 %u/2":"背景 %u/3",owned);rows[i]=box(screen,16+(int)(i-8)*108,253,100,27,m->selection==i?LEMON:SKY,8);centered(text(rows[i],4,4,92,line,INK));}break;
    case STAYC_GAME_CARD:
        name="心动收藏";box(screen,18,44,204,226,m->selection%2?PINK:SKY,15);centered(text(screen,29,55,182,card_names[m->selection%8],INK));scene(true,56,77,512,false);centered(text(screen,44,239,152,stayc_members[member],INK));break;
    case STAYC_GAME_BADGE:
        name="应援牌";scene(true,56,73,512,false);centered(text(screen,22,51,196,stayc_members[member],INK));detail=text(screen,20,251,200,m->save.badge_card<8?card_names[m->save.badge_card]:"Pocket",INK);centered(detail);break;
    case STAYC_GAME_SETTINGS: {
        name="设置";char volume[64],countdown[64],mode[80];snprintf(volume,sizeof(volume),"音量  %lu%%",(unsigned long)m->save.volume);snprintf(countdown,sizeof(countdown),"倒计时  %s",m->save.countdown&1u?"开":"关");snprintf(mode,sizeof(mode),"模式  %s",modes[m->save.mode%3]);const char *labels[]={volume,countdown,mode,"用户信息",m->debug_mode?"开发调试  开":"开发调试  关"};for(unsigned i=0;i<5;i++)row(i,58+(int)i*38,32,labels[i],m->selection==i,false);
        if(m->debug_mode||m->selection==4)centered(text(screen,20,254,200,m->debug_mode?"DEV 临时测试，不保存":"全解锁，退出恢复",INK));
        if(m->volume_edit){lv_obj_set_style_bg_color(rows[0],lv_color_hex(INK),0);lv_obj_set_style_text_color(row_text[0],lv_color_hex(CREAM),0);text(rows[0],162,6,36,"↑ ↓",CREAM);}break;
    }
    case STAYC_GAME_ACCOUNT:
        name="用户信息";box(screen,20,76,200,150,SKY,13);
        if(m->debug_mode){centered(text(screen,29,103,182,"DEV 离线测试",INK));centered(text(screen,29,140,182,"退出调试后连接账号",INK));centered(text(screen,29,177,182,"不解锁账号权益",INK));}
        else centered(text(screen,20,116,200,"打开账号页面",INK));
        break;
    }
    lv_label_set_text(title,name);
    toast=text(screen,20,291,200,"",INK);centered(toast);lv_obj_set_style_bg_color(toast,lv_color_hex(LEMON),0);lv_obj_set_style_bg_opa(toast,LV_OPA_COVER,0);lv_obj_add_flag(toast,LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(screen);if(old)lv_obj_delete(old);
}

void stayc_game_ui_render(const stayc_game_t *m,unsigned position,bool playing,int battery,bool storage_ok,bool save_blocked,bool input_ok,unsigned ticks,const stayc_control_t *v) {
    (void)ticks;if(!m)return;
    unsigned member=v?v->member%6:0,view=v?v->view:0,vs=v?v->selection:0,action=v?v->action:0;
    bool shop=m->page==STAYC_GAME_WARDROBE||m->page==STAYC_GAME_ROOM_PICKER||m->page==STAYC_GAME_DECOR_PICKER||m->page==STAYC_GAME_REDEEM;
    bool rebuild=force_rebuild||!screen||lv_screen_active()!=screen||previous.page!=(int)m->page||previous.selection!=m->selection||previous.track!=m->track||previous.member!=member||previous.view!=view||previous.visual_selection!=vs||previous.action!=action||previous.life_kind!=m->life_kind||previous.decor_slot!=m->decor_slot||previous.volume_edit!=m->volume_edit||previous.debug_mode!=m->debug_mode||previous.save.room!=m->save.room||previous.save.decor!=m->save.decor||previous.save.outfit!=m->save.outfit||previous.save.cards!=m->save.cards;
    if(shop&&(previous.shop_status!=m->shop_status||previous.shop_item!=m->shop_item||previous.save.star_balance!=m->save.star_balance||previous.save.owned_outfits!=m->save.owned_outfits||previous.save.owned_rooms!=m->save.owned_rooms||previous.save.owned_decor!=m->save.owned_decor))rebuild=true;
    if((m->page==STAYC_GAME_SETTINGS||m->page==STAYC_GAME_MUSIC)&&(previous.save.volume!=m->save.volume||previous.save.mode!=m->save.mode||previous.save.countdown!=m->save.countdown))rebuild=true;
    if(m->page==STAYC_GAME_JOURNEY&&(previous.save.journey_claims!=m->save.journey_claims||previous.settled_claims!=m->settled_claims))rebuild=true;
    if(m->page==STAYC_GAME_BADGE&&previous.save.badge_card!=m->save.badge_card)rebuild=true;
    if(rebuild)build(m,v);
    force_rebuild=false;previous.page=m->page;previous.selection=m->selection;previous.track=m->track;previous.member=member;previous.view=view;previous.visual_selection=vs;previous.action=action;previous.life_kind=m->life_kind;previous.decor_slot=m->decor_slot;previous.settled_claims=m->settled_claims;previous.volume_edit=m->volume_edit;previous.debug_mode=m->debug_mode;previous.shop_status=m->shop_status;previous.shop_item=m->shop_item;previous.save=m->save;
    char line[144];h2h_header_update(&header,title,m->save.hearts,battery,m->debug_mode,phone_connected);
    const char *footer=stayc_game_footer_hint(m);
    if(m->page==STAYC_GAME_ROOM&&v&&v->view!=STAYC_NORMAL)footer=v->view==STAYC_ACTION_PLAYER?(v->elapsed>=stayc_action_duration(v->action)?"● 再来   长按返回":v->paused?"● 继续   长按返回":"● 暂停   长按返回"):"↑ ↓ 选择   ● 确定";
    lv_label_set_text(hint,footer);
    bool visual_ok=!actor||stayc_game_visual_update(background,actor,foreground,m,v,playing,actor_x,actor_y,actor_scale,compact_actor);
    if(m->page==STAYC_GAME_ROOM&&v&&v->view==STAYC_ACTION_PLAYER){snprintf(line,sizeof(line),"%s  %lu/%us",v->elapsed>=stayc_action_duration(v->action)?"完成啦":v->paused?"暂停":"一起",(unsigned long)(v->elapsed/1000),stayc_action_duration(v->action)/1000);lv_label_set_text(detail,line);}
    if(m->page==STAYC_GAME_LIFE){lv_label_set_text(detail,stayc_game_life_caption(m));for(unsigned i=0;i<2;i++)if(m->life_started)lv_obj_add_flag(rows[i],LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(rows[i],LV_OBJ_FLAG_HIDDEN);if(m->life_started){snprintf(line,sizeof(line),"%s  %lu/%us",m->life_finished?"完成啦":m->life_paused?"暂停":"一起",(unsigned long)(m->life_ms/1000),stayc_game_life_duration(m->life_kind)/1000);lv_label_set_text(progress_text,line);lv_obj_remove_flag(progress_text,LV_OBJ_FLAG_HIDDEN);}else lv_obj_add_flag(progress_text,LV_OBJ_FLAG_HIDDEN);}
    if(m->page==STAYC_GAME_PLAYER){unsigned total=h2h_tracks[m->track%h2h_track_count].duration_ms;if(position>total)position=total;lv_bar_set_value(progress,total?(int)((uint64_t)position*1000/total):0,LV_ANIM_OFF);snprintf(line,sizeof(line),"%u:%02u / %u:%02u",position/60000,position/1000%60,total/60000,total/1000%60);lv_label_set_text(progress_text,line);if(m->countdown_ms)snprintf(line,sizeof(line),"准备开跳  %lu",(unsigned long)((m->countdown_ms+999)/1000));else if(m->audio_error)snprintf(line,sizeof(line),"播放失败  确定重试");else snprintf(line,sizeof(line),"%s  音量%lu%%",playing?"正在播放":"已暂停",(unsigned long)m->save.volume);lv_label_set_text(detail,line);}
    if(m->page==STAYC_GAME_JOURNEY){unsigned claimed=m->save.journey_claims|m->settled_claims;for(unsigned i=0;i<6;i++){unsigned value=stayc_game_journey_progress(m,i),target=stayc_game_journey_target(i);if(value>target)value=target;snprintf(line,sizeof(line),"%s %s %u/%u",claimed&(1u<<i)?"♥":"·",journey_names[i],value,target);lv_label_set_text(row_text[i],line);}lv_label_set_text(detail,claimed&(1u<<m->selection)?"已领取":stayc_game_journey_ready(m)&(1u<<m->selection)?"可领 +2爱心 +2星星":"达成目标 +2爱心 +2星星");}
    if(m->page==STAYC_GAME_ECHO){
        bool completed=m->echo_round_wait_ms&&m->echo_feedback_ms&&m->echo_feedback==STAYC_GAME_FEEDBACK_CORRECT;
        unsigned length=completed?m->echo_length-1:m->echo_length;
        snprintf(line,sizeof(line),"第 %u/5 轮  %u步",completed?m->echo_round:m->echo_round+1,length);lv_label_set_text(detail,line);
        const char *message=m->game_paused?"暂停":m->echo_round_wait_ms?"准备下一轮":m->echo_show?"记住动作":"轮到你啦";
        if(m->echo_feedback_ms&&m->echo_feedback==STAYC_GAME_FEEDBACK_WRONG)message="方向不对，再看一次";
        else if(m->echo_feedback_ms&&m->echo_feedback==STAYC_GAME_FEEDBACK_CORRECT)message="按对了";
        lv_label_set_text(progress_text,message);
        snprintf(line,sizeof(line),"%u/%u 步  机会 %u",completed?length:m->echo_input,length,m->echo_mistakes<3?3-m->echo_mistakes:0);lv_label_set_text(row_text[2],line);
        unsigned cue=stayc_game_echo_display_cue(m);
        for(unsigned i=0;i<2;i++){
            uint32_t color=!m->game_paused&&cue==i?LEMON:0xffffff;
            if(m->echo_feedback_ms&&m->echo_feedback_direction==i)
                color=m->echo_feedback==STAYC_GAME_FEEDBACK_WRONG?0xf4c0c4u:m->echo_feedback==STAYC_GAME_FEEDBACK_CORRECT?0xbfe5cfu:BLUE;
            lv_obj_set_style_bg_color(rows[i],lv_color_hex(color),0);
        }
    }
    if(m->page==STAYC_GAME_GAME){snprintf(line,sizeof(line),"%s %lus   %u分",m->game_paused?"暂停":"剩余",(unsigned long)((m->game_ms+999)/1000),m->score);lv_label_set_text(detail,line);const char *phases[]={"热身","心动","闪耀"};snprintf(line,sizeof(line),"%s  连击 %u  接住 %u",phases[stayc_game_game_phase(m)%3],m->combo,m->caught);lv_label_set_text(progress_text,line);lv_obj_set_x(basket,22+(int)m->basket*40);for(unsigned i=0;i<6;i++){if(m->drops[i].active){lv_obj_remove_flag(falling[i],LV_OBJ_FLAG_HIDDEN);lv_obj_set_pos(falling[i],29+m->drops[i].lane*40,92+m->drops[i].y*150/170);lv_obj_set_style_text_color(falling[i],lv_color_hex(m->drops[i].heart?0xeaafca:0xeab94a),0);lv_label_set_text(falling[i],m->drops[i].heart?"♥":"●");}else lv_obj_add_flag(falling[i],LV_OBJ_FLAG_HIDDEN);}}
    bool unlocked=m->pose_ms&&m->pose==STAYC_GAME_CELEBRATE&&m->last_unlock<8;
    if(!input_ok||!storage_ok||save_blocked||!visual_ok||unlocked){lv_label_set_text(toast,!input_ok?"按键不可用，请重启":save_blocked?"存档保护，仅临时游玩":!visual_ok?"成员素材加载失败":!storage_ok?"存档失败，稍后重试":"解锁了一张新收藏卡");lv_obj_remove_flag(toast,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(hint,LV_OBJ_FLAG_HIDDEN);}else{lv_obj_add_flag(toast,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(hint,LV_OBJ_FLAG_HIDDEN);}
}
