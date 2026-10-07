#pragma once
#include "stayc_game.h"
#include "stayc_scene.h"
#include "lvgl.h"

/* One active scene, LVGL lock held by the caller. The actor uses the existing
 * stayc_visual decoder and its single 15,360-byte buffer. No offscreen canvas. */
lv_obj_t *stayc_game_visual_background(lv_obj_t *parent);
lv_obj_t *stayc_game_visual_actor(lv_obj_t *parent);
lv_obj_t *stayc_game_visual_foreground(lv_obj_t *parent);
bool stayc_game_visual_update(lv_obj_t *background, lv_obj_t *actor,
                             lv_obj_t *foreground, const stayc_game_t *model,
                             const stayc_control_t *controls, bool playing,
                             int x, int y, unsigned scale_q8, bool compact);

/* Pure display sampling of the published member-scenes adapter. Does not
 * advance clocks, charge coins, award progress, or modify the saved outfit. */
void stayc_game_visual_sample(const stayc_game_t *model, unsigned member,
                             unsigned outfit, stayc_frame_t *frame);
unsigned stayc_game_visual_command_count(void);
