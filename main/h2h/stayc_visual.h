#pragma once
#include "stayc_scene.h"
#include "lvgl.h"
/* All calls require the LVGL lock. One actor is decoded into a fixed 15 KiB buffer. */
void stayc_visual_room(lv_obj_t *screen, unsigned room, unsigned decor);
lv_obj_t *stayc_visual_actor(lv_obj_t *screen);
lv_obj_t *stayc_visual_props(lv_obj_t *screen);
bool stayc_visual_update(lv_obj_t *actor, lv_obj_t *props, unsigned member, unsigned outfit,
                         unsigned action, const stayc_frame_t *frame);
