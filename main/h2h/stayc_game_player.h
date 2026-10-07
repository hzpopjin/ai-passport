#pragma once
#include "stayc_game.h"

/* Published v15 player-controls.mjs session layer. History is never saved.
 * The paired STAYC catalog has 12 tracks, each representable by uint8_t.
 * Retain the latest 256 visited tracks; previous stops at the oldest retained
 * entry after overflow. truncated reports this deliberate bounded-RAM limit.
 * A mode, full/chorus or DEV-epoch change starts a new history session. */
#define STAYC_PLAYER_HISTORY 256u
typedef double (*stayc_player_random_fn)(void *context);
typedef struct {
    uint8_t tracks[STAYC_PLAYER_HISTORY];
    unsigned length, cursor, first, last_track, mode, format;
    uint32_t debug_epoch, rng;
    stayc_player_random_fn random;
    void *random_context;
    bool initialized, truncated;
} stayc_player_t;

typedef enum {
    STAYC_PLAYER_PREVIOUS, STAYC_PLAYER_NEXT, STAYC_PLAYER_MODE,
    STAYC_PLAYER_TOGGLE, STAYC_PLAYER_OPEN
} stayc_player_command_t;
typedef enum {
    STAYC_PLAYER_OK, STAYC_PLAYER_INVALID, STAYC_PLAYER_WRONG_PAGE,
    STAYC_PLAYER_COUNTDOWN, STAYC_PLAYER_NAVIGATION
} stayc_player_status_t;

/* Zero-initialized static storage also works. Optional random returns a sample
 * clamped like the browser's Math.random; NULL uses a separate session RNG.
 * All functions run in the model's owning task, without heap, IDF or LVGL. */
void stayc_player_init(stayc_player_t *p, const stayc_game_t *m,
                       stayc_player_random_fn random, void *context);
void stayc_player_observe(stayc_player_t *p, const stayc_game_t *m);
void stayc_player_input(stayc_player_t *p, stayc_game_t *m,
                        stayc_game_key_t key, stayc_game_event_t event);
stayc_player_status_t stayc_player_command(stayc_player_t *p, stayc_game_t *m,
                                          stayc_player_command_t command, unsigned value);
unsigned stayc_player_history_track(const stayc_player_t *p, unsigned index);
/* Raw end + observe matches JS dispatch('audio'). The runtime/main caller must
 * reject late ended events while suspended, paused, failed or on stale audio.
 * Do not move that browser safety filter into the unchanged raw C core. */
void stayc_player_end(stayc_player_t *p, stayc_game_t *m, unsigned generation, bool failed);
