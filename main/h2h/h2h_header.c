#include "h2h_header.h"
#include <stdio.h>
LV_FONT_DECLARE(h2h_font_16);
static lv_obj_t *label(lv_obj_t *screen,const lv_font_t *font,uint32_t color) {
    lv_obj_t *o=lv_label_create(screen);lv_obj_set_style_text_font(o,font,0);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_label_set_text(o,"");
    lv_obj_set_width(o,LV_SIZE_CONTENT);return o;
}
void h2h_header_create(h2h_header_t *h,lv_obj_t *screen) {
    h->hearts=label(screen,&h2h_font_16,0x26466b);
    h->battery=label(screen,&h2h_font_16,0x26466b);
    lv_point_t size;lv_text_get_size(&size,"100%",&h2h_font_16,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    lv_obj_set_width(h->battery,size.x);lv_obj_set_style_text_align(h->battery,LV_TEXT_ALIGN_RIGHT,0);
    h->bluetooth=label(screen,&lv_font_montserrat_14,0x3479be);
    lv_label_set_text(h->bluetooth,LV_SYMBOL_BLUETOOTH);lv_obj_add_flag(h->bluetooth,LV_OBJ_FLAG_HIDDEN);
    h->charging=label(screen,&lv_font_montserrat_14,0x26466b);
    lv_label_set_text(h->charging,LV_SYMBOL_CHARGE);lv_obj_add_flag(h->charging,LV_OBJ_FLAG_HIDDEN);
    h->charging_known_active=false;
}
void h2h_header_charging(h2h_header_t *h,bool charging) {
    h->charging_known_active=charging;
    if(charging)lv_obj_remove_flag(h->charging,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(h->charging,LV_OBJ_FLAG_HIDDEN);
}
static void baseline_position(lv_obj_t *o,int x,int baseline) {
    const lv_font_t *font=lv_obj_get_style_text_font(o,0);
    lv_obj_set_pos(o,x,baseline-(int)font->line_height+(int)font->base_line);
}
void h2h_header_update(h2h_header_t *h,lv_obj_t *title,uint32_t hearts,int battery,bool debug,bool connected) {
    char value[24];snprintf(value,sizeof(value),debug?"DEV":"♥%lu",(unsigned long)hearts);lv_label_set_text(h->hearts,value);
    if(battery<0)snprintf(value,sizeof(value),"--");else snprintf(value,sizeof(value),"%d%%",battery>100?100:battery);
    lv_label_set_text(h->battery,value);lv_obj_update_layout(h->hearts);lv_obj_update_layout(h->battery);lv_obj_update_layout(h->charging);
    const lv_font_t *font=lv_obj_get_style_text_font(title,0);
    int baseline=(int)lv_obj_get_y(title)+(int)font->line_height-(int)font->base_line;
    int battery_x=224-(int)lv_obj_get_width(h->battery);
    int charge_width=h->charging_known_active?(int)lv_obj_get_width(h->charging)+4:0;
    int hearts_x=battery_x-charge_width-7-(int)lv_obj_get_width(h->hearts);
    baseline_position(h->battery,battery_x,baseline);baseline_position(h->hearts,hearts_x,baseline);
    baseline_position(h->charging,battery_x-charge_width,baseline);
    /* Reserve the icon slot on both states, so reconnect never shifts digits. */
    baseline_position(h->bluetooth,hearts_x-18,baseline);
    if(connected)lv_obj_remove_flag(h->bluetooth,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(h->bluetooth,LV_OBJ_FLAG_HIDDEN);
    int title_width=hearts_x-18-6-(int)lv_obj_get_x(title);lv_obj_set_width(title,title_width>8?title_width:8);
    lv_obj_set_height(title,20); /* Keep long page titles on one header line. */
}
