#include "stayc_game_player.h"
#include <string.h>

_Static_assert(sizeof(stayc_player_t) <= 384, "Player session exceeds internal-RAM budget");

/* Only the fields used by move(), never a per-event copy of the game model. */
typedef struct {
    stayc_game_page_t page;
    unsigned track, songs, mode;
    bool chorus, playing;
    uint32_t countdown;
} player_before_t;
typedef struct { unsigned track, cursor; bool retrace; } player_target_t;

unsigned stayc_player_history_track(const stayc_player_t *p, unsigned index) {
    return index < p->length ? p->tracks[(p->first + index) % STAYC_PLAYER_HISTORY] : 0;
}

static void append(stayc_player_t *p, unsigned track) {
    /* A manual selection after previous discards the browser's future branch. */
    p->length = p->length ? p->cursor + 1 : 0;
    if (p->length == STAYC_PLAYER_HISTORY) {
        p->first = (p->first + 1) % STAYC_PLAYER_HISTORY;
        --p->length;
        p->truncated = true;
    }
    p->tracks[(p->first + p->length) % STAYC_PLAYER_HISTORY] = (uint8_t)track;
    p->cursor = p->length++;
}

void stayc_player_observe(stayc_player_t *p, const stayc_game_t *m) {
    unsigned format = stayc_game_chorus_mode(m);
    if (!p->initialized || p->mode != m->save.mode || p->format != format ||
        p->debug_epoch != m->debug_epoch) {
        p->mode = m->save.mode;
        p->format = format;
        p->debug_epoch = m->debug_epoch;
        p->length = p->cursor = p->first = 0;
        p->truncated = false;
        if (!p->initialized && !p->rng) p->rng = m->rng ? m->rng : 1;
        p->initialized = true;
        append(p, m->track);
    } else if (m->track != p->last_track) append(p, m->track);
    p->last_track = m->track;
}

void stayc_player_init(stayc_player_t *p, const stayc_game_t *m,
                       stayc_player_random_fn random, void *context) {
    memset(p, 0, sizeof(*p));
    p->random = random;
    p->random_context = context;
    stayc_player_observe(p, m);
}

static void raw(stayc_game_t *m, stayc_game_key_t key, stayc_game_event_t event) {
    stayc_game_input(m, key, event);
}
static void tap(stayc_game_t *m, stayc_game_key_t key) {
    raw(m, key, STAYC_GAME_PRESS);
    raw(m, key, STAYC_GAME_CLICK);
}
static stayc_player_status_t menu(stayc_game_t *m) {
    if (m->page == STAYC_GAME_PLAYER || m->page == STAYC_GAME_TRACKS)
        raw(m, STAYC_GAME_OK, STAYC_GAME_LONG);
    return m->page == STAYC_GAME_MUSIC ? STAYC_PLAYER_OK : STAYC_PLAYER_WRONG_PAGE;
}
static stayc_player_status_t row(stayc_game_t *m, unsigned index) {
    for (unsigned i = 0; m->selection != index && i < 6; ++i) tap(m, STAYC_GAME_DOWN);
    return m->selection == index ? STAYC_PLAYER_OK : STAYC_PLAYER_NAVIGATION;
}
static stayc_player_status_t choose_track(stayc_game_t *m, unsigned track, bool playing) {
    stayc_player_status_t status = menu(m);
    if (status != STAYC_PLAYER_OK) return status;
    if ((status = row(m, 1)) != STAYC_PLAYER_OK) return status;
    tap(m, STAYC_GAME_OK);
    if (m->page != STAYC_GAME_TRACKS || !m->song_count) return STAYC_PLAYER_NAVIGATION;
    unsigned song = track % m->song_count;
    for (unsigned i = 0; m->selection != song && i < m->song_count; ++i) tap(m, STAYC_GAME_DOWN);
    if (m->selection != song) return STAYC_PLAYER_NAVIGATION;
    tap(m, STAYC_GAME_OK);
    if (!playing && (m->want_play || m->countdown_ms)) tap(m, STAYC_GAME_OK);
    return STAYC_PLAYER_OK;
}
static player_before_t before(const stayc_game_t *m) {
    return (player_before_t){m->page, m->track, m->song_count, m->save.mode,
                             stayc_game_chorus_mode(m), m->want_play, m->countdown_ms};
}
static double random_value(stayc_player_t *p) {
    double value;
    if (p->random) value = p->random(p->random_context);
    else {
        uint32_t x = p->rng;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        p->rng = x ? x : 1;
        value = (double)p->rng / 4294967296.0;
    }
    if (value != value || value <= 0) return 0; /* NaN and negative samples. */
    return value > .999999999999 ? .999999999999 : value;
}
static player_target_t target(stayc_player_t *p, player_before_t b, bool next) {
    player_target_t t = {b.track, 0, false};
    if (!b.songs) return t;
    unsigned base = b.chorus ? b.songs : 0, current = b.track % b.songs;
    if (b.mode != 1) {
        t.track = base + (current + (next ? 1 : b.songs - 1)) % b.songs;
    } else if (!next || p->cursor + 1 < p->length) {
        t.cursor = next ? p->cursor + 1 : p->cursor ? p->cursor - 1 : 0;
        t.track = stayc_player_history_track(p, t.cursor);
        t.retrace = true;
    } else {
        /* Browser consumes one random sample even with a one-song catalog. */
        double value = random_value(p);
        t.track = base + (b.songs > 1 ?
            (current + 1 + (unsigned)(value * (b.songs - 1))) % b.songs : current);
    }
    return t;
}
static stayc_player_status_t move(stayc_player_t *p, stayc_game_t *m,
                                  bool next, player_before_t b, bool native_result) {
    if (b.page != STAYC_GAME_MUSIC && b.page != STAYC_GAME_PLAYER && b.page != STAYC_GAME_TRACKS)
        return STAYC_PLAYER_WRONG_PAGE;
    player_target_t t = target(p, b, next);
    stayc_player_status_t status = STAYC_PLAYER_OK;
    if (!native_result || m->track != t.track)
        status = choose_track(m, t.track, b.playing || b.countdown > 0);
    else if (!b.playing && !b.countdown && (m->want_play || m->countdown_ms))
        tap(m, STAYC_GAME_OK);
    if (status != STAYC_PLAYER_OK) return status;
    if (t.retrace) { p->cursor = t.cursor; p->last_track = m->track; }
    stayc_player_observe(p, m);
    return STAYC_PLAYER_OK;
}

void stayc_player_input(stayc_player_t *p, stayc_game_t *m,
                        stayc_game_key_t key, stayc_game_event_t event) {
    stayc_player_observe(p, m);
    player_before_t b = before(m);
    unsigned generation = m->generation;
    raw(m, key, event);
    if (b.page == STAYC_GAME_PLAYER && event == STAYC_GAME_CLICK &&
        (key == STAYC_GAME_UP || key == STAYC_GAME_DOWN) && m->generation != generation) {
        (void)move(p, m, key == STAYC_GAME_DOWN, b, true);
    } else stayc_player_observe(p, m);
}

stayc_player_status_t stayc_player_command(stayc_player_t *p, stayc_game_t *m,
                                          stayc_player_command_t command, unsigned value) {
    stayc_player_observe(p, m);
    player_before_t b = before(m);
    stayc_player_status_t status;
    switch (command) {
        case STAYC_PLAYER_PREVIOUS: case STAYC_PLAYER_NEXT:
            return move(p, m, command == STAYC_PLAYER_NEXT, b, false);
        case STAYC_PLAYER_MODE:
            if (value > 2) return STAYC_PLAYER_INVALID;
            if (value == m->save.mode) return STAYC_PLAYER_OK;
            if (b.countdown) return STAYC_PLAYER_COUNTDOWN;
            if ((status = menu(m)) != STAYC_PLAYER_OK) return status;
            if ((status = row(m, m->song_count < m->track_count ? 3 : 2)) != STAYC_PLAYER_OK) return status;
            for (unsigned i = 0; m->save.mode != value && i < 3; ++i) tap(m, STAYC_GAME_OK);
            if (b.page == STAYC_GAME_PLAYER || b.page == STAYC_GAME_TRACKS) {
                if ((status = row(m, b.page == STAYC_GAME_PLAYER ? 0 : 1)) != STAYC_PLAYER_OK) return status;
                tap(m, STAYC_GAME_OK);
            }
            break;
        case STAYC_PLAYER_TOGGLE:
            if (b.page == STAYC_GAME_PLAYER) tap(m, STAYC_GAME_OK);
            else {
                bool wants_play = !(b.playing || b.countdown);
                if ((status = menu(m)) != STAYC_PLAYER_OK) return status;
                if ((status = row(m, 0)) != STAYC_PLAYER_OK) return status;
                tap(m, STAYC_GAME_OK);
                if ((m->want_play || m->countdown_ms > 0) != wants_play) tap(m, STAYC_GAME_OK);
            }
            break;
        case STAYC_PLAYER_OPEN:
            if (m->page == STAYC_GAME_ROOM) raw(m, STAYC_GAME_OK, STAYC_GAME_LONG);
            if (m->page == STAYC_GAME_HOME) {
                if ((status = row(m, 1)) != STAYC_PLAYER_OK) return status;
                tap(m, STAYC_GAME_OK);
            }
            if ((status = menu(m)) != STAYC_PLAYER_OK) return status;
            if ((status = row(m, 0)) != STAYC_PLAYER_OK) return status;
            tap(m, STAYC_GAME_OK);
            break;
        default: return STAYC_PLAYER_INVALID;
    }
    stayc_player_observe(p, m);
    return STAYC_PLAYER_OK;
}

void stayc_player_end(stayc_player_t *p, stayc_game_t *m, unsigned generation, bool failed) {
    stayc_player_observe(p, m);
    stayc_game_audio_end(m, generation, failed);
    stayc_player_observe(p, m);
}
