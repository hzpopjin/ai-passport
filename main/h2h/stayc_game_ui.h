#pragma once
#include "stayc_game.h"
#include "stayc_scene.h"

/* Nonblocking native controls. Return true when an extension consumed input;
 * otherwise the caller dispatches stayc_game_input(). Gate access in main. */
bool stayc_game_ui_input(stayc_game_t *model, stayc_control_t *visuals,
                         h2h_key_t key, h2h_event_t event);
void stayc_game_ui_tick(stayc_game_t *model, stayc_control_t *visuals,
                        uint32_t elapsed_ms);

/* Caller holds the BSP LVGL lock. Account/activation screens remain in the
 * existing production UI. Reset when switching back from that UI. */
void stayc_game_ui_reset(void);
void stayc_game_ui_phone_connection(bool connected);
void stayc_game_ui_render(const stayc_game_t *model, unsigned position_ms,
                          bool playing, int battery, bool storage_ok,
                          bool save_blocked, bool input_ok, unsigned ticks,
                          const stayc_control_t *visuals);
