#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "h2h_model.h"

enum { STAYC_MEMBERS = 6, STAYC_OUTFITS = 2, STAYC_POSES = 32, STAYC_FRAME_BYTES = 15360 };
typedef struct {
    uint32_t offset, length;
    int8_t anchors[4][2]; /* left hand, right hand, mouth, grip */
    int8_t offset_y;
    uint16_t scale_q8;
    uint8_t hand;
} stayc_sprite_t;
extern const char *const stayc_members[STAYC_MEMBERS];
extern const stayc_sprite_t stayc_sprites[STAYC_MEMBERS][STAYC_OUTFITS][STAYC_POSES];

/* Exact-size decoding: reject truncation, excess input and output overflow. */
bool stayc_unpack(const uint8_t *source, size_t length, uint8_t *out, size_t capacity);

typedef struct {
    uint16_t ms;
    uint8_t pose;
    int8_t dx, dy;
    uint8_t prop, attachment, flash, scroll, effect;
    int16_t from_x, from_y, to_x, to_y;
    uint8_t arc;
} stayc_phase_t;
typedef struct { const char *label; const stayc_phase_t *phases; uint8_t count; uint16_t duration; } stayc_action_t;
typedef struct { int16_t x, y, w, h; uint32_t color; uint8_t radius; } stayc_box_t;
typedef struct { const stayc_box_t *boxes; unsigned count; } stayc_room_t;
extern const stayc_action_t stayc_actions[6][2][8];
extern const stayc_room_t stayc_rooms[3];
extern const stayc_room_t stayc_decor[4][3];
extern const char *const stayc_decor_labels[4][3];
enum { STAYC_NORMAL, STAYC_MEMBER_MENU, STAYC_ACTION_MENU, STAYC_ACTION_PLAYER, STAYC_DECOR_MENU };
typedef struct {
    unsigned member, view, selection, action, decor;
    uint32_t elapsed;
    bool paused, suppress_ok, preferences_dirty;
} stayc_control_t;
typedef struct {
    unsigned pose, prop, attachment, effect;
    int x, y, scale_q8, effect_x, effect_y;
    bool flash, scroll, done;
    unsigned progress_q8;
    unsigned duration, elapsed;
} stayc_frame_t;

const char *stayc_action_label(unsigned action);
unsigned stayc_action_duration(unsigned action);
bool stayc_control_input(stayc_control_t *s, h2h_model_t *model, h2h_key_t key, h2h_event_t event);
void stayc_control_tick(stayc_control_t *s, const h2h_model_t *model, uint32_t ms);
void stayc_sample(unsigned member, unsigned outfit, unsigned action, uint32_t ms, stayc_frame_t *out);
unsigned stayc_reaction_pose(unsigned card, uint32_t ms);
