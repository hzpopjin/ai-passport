#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { H2H_HOME, H2H_ROOM, H2H_MUSIC, H2H_PLAYER, H2H_TRACKS,
    H2H_GAME, H2H_RESULT, H2H_COLLECTION, H2H_CARD, H2H_BADGE, H2H_SETTINGS, H2H_ACCOUNT } h2h_page_t;
typedef enum { H2H_UP, H2H_DOWN, H2H_OK } h2h_key_t;
typedef enum { H2H_PRESS, H2H_CLICK, H2H_LONG } h2h_event_t;
typedef enum { H2H_IDLE, H2H_WAVE, H2H_HEART, H2H_DANCE, H2H_CELEBRATE } h2h_pose_t;
typedef struct {
    uint32_t magic, version, hearts, listen_ms, cards, outfit, room, volume, mode, countdown, badge_card, checksum;
} h2h_save_t;
typedef struct { int lane, y; bool active, heart; } h2h_drop_t;
typedef struct {
    h2h_save_t save;
    h2h_page_t page;
    unsigned selection, track, track_count, song_count, generation, last_unlock;
    bool want_play, audio_error, dirty, game_paused, game_settled, suppress_ok;
    uint32_t countdown_ms, pose_ms, game_ms, game_step_ms, spawn_ms, rng;
    unsigned caught, game_reward, basket;
    uint32_t position_ms, duration_ms, start_ms, animation_ms;
    bool suppress_volume[2];
    uint32_t tap_gap_ms;
    uint8_t tap_count, tap_key;
    h2h_pose_t pose;
    h2h_drop_t drops[6];
} h2h_model_t;

void h2h_init(h2h_model_t *m, unsigned track_count, unsigned song_count, uint32_t seed);
bool h2h_load(h2h_model_t *m, const void *data, size_t size);
void h2h_save_snapshot(const h2h_model_t *m, h2h_save_t *out);
void h2h_input(h2h_model_t *m, h2h_key_t key, h2h_event_t event);
void h2h_tick(h2h_model_t *m, uint32_t elapsed_ms);
void h2h_listened(h2h_model_t *m, uint32_t pcm_ms);
void h2h_audio_end(h2h_model_t *m, unsigned generation, bool failed);
unsigned h2h_card_count(const h2h_model_t *m);
unsigned h2h_outfit_count(const h2h_model_t *m);
unsigned h2h_room_count(const h2h_model_t *m);
unsigned h2h_menu_count(const h2h_model_t *m);
void h2h_start_track(h2h_model_t *m, unsigned track);
unsigned h2h_current_song(const h2h_model_t *m);
bool h2h_chorus_mode(const h2h_model_t *m);

void h2h_seek_relative(h2h_model_t *m, int delta_ms);
