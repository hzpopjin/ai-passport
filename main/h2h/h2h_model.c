#include "h2h_model.h"
#include <string.h>

#define SAVE_MAGIC 0x48324850u
#define SAVE_VERSION 1u
#define TAP_GAP_MS 800u
static uint32_t random_next(h2h_model_t *m) {
    uint32_t x = m->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return m->rng = x ? x : 1;
}
static uint32_t checksum(const h2h_save_t *s) {
    const unsigned char *p = (const unsigned char *)s;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(h2h_save_t, checksum); ++i) hash = (hash ^ p[i]) * 16777619u;
    return hash;
}
unsigned h2h_card_count(const h2h_model_t *m) {
    unsigned n = 0;
    for (unsigned i = 0; i < 8; ++i) n += (m->save.cards >> i) & 1u;
    return n;
}
unsigned h2h_outfit_count(const h2h_model_t *m) {
    unsigned n = h2h_card_count(m);
    return 1u + (n >= 2) + (n >= 4);
}
unsigned h2h_room_count(const h2h_model_t *m) {
    unsigned n = h2h_card_count(m);
    return 1u + (n >= 6) + (n >= 8);
}
bool h2h_chorus_mode(const h2h_model_t *m) { return (m->save.countdown & 2u) != 0; }
unsigned h2h_current_song(const h2h_model_t *m) { return m->song_count ? m->track % m->song_count : 0; }
void h2h_init(h2h_model_t *m, unsigned count, unsigned songs, uint32_t seed) {
    memset(m, 0, sizeof(*m));
    m->save.magic = SAVE_MAGIC; m->save.version = SAVE_VERSION;
    m->save.volume = 35; m->track_count = count;
    m->song_count = songs && songs <= count && (songs == count || songs * 2 == count) ? songs : count;
    m->rng = seed ? seed : 1;
    m->last_unlock = 8; m->save.badge_card = 8;
}
bool h2h_load(h2h_model_t *m, const void *data, size_t size) {
    h2h_save_t s;
    if (!data || size != sizeof(s)) return false;
    memcpy(&s, data, sizeof(s));
    if (s.magic != SAVE_MAGIC || s.version != SAVE_VERSION || s.checksum != checksum(&s) ||
        s.hearts > 999999 || s.listen_ms >= 120000 || s.cards > 255 || s.volume > 100 ||
        s.mode > 2 || s.countdown > (m->song_count < m->track_count ? 3u : 1u) || s.badge_card > 8 || (s.badge_card < 8 && !(s.cards & (1u << s.badge_card))) || s.outfit > 2 || s.room > 2) return false;
    h2h_model_t candidate = *m;
    candidate.save = s;
    unsigned expected = s.hearts / 5;
    if (expected > 8) expected = 8;
    if (h2h_card_count(&candidate) != expected) return false;
    m->save = s;
    m->track = h2h_chorus_mode(m) ? m->song_count : 0;
    return true;
}
void h2h_save_snapshot(const h2h_model_t *m, h2h_save_t *out) {
    *out = m->save;
    out->checksum = checksum(out);
}
static void reward(h2h_model_t *m, unsigned hearts) {
    if (!hearts) return;
    m->save.hearts = hearts > 999999u - m->save.hearts ? 999999u : m->save.hearts + hearts;
    while (h2h_card_count(m) < 8 && h2h_card_count(m) < m->save.hearts / 5) {
        unsigned missing[8], n = 0;
        for (unsigned i = 0; i < 8; ++i) if (!(m->save.cards & (1u << i))) missing[n++] = i;
        unsigned card = missing[random_next(m) % n];
        m->save.cards |= 1u << card; m->last_unlock = card;
        m->pose = H2H_CELEBRATE; m->pose_ms = 2400;
    }
    m->dirty = true;
}
void h2h_listened(h2h_model_t *m, uint32_t ms) {
    /* Only confirmed PCM written by the audio worker reaches this function. */
    uint64_t total = (uint64_t)m->save.listen_ms + ms;
    reward(m, (unsigned)(total / 120000));
    m->save.listen_ms = (uint32_t)(total % 120000);
}
unsigned h2h_menu_count(const h2h_model_t *m) {
    switch (m->page) {
        case H2H_HOME: return 6;
        case H2H_ROOM: return 4;
        case H2H_MUSIC: return m->song_count < m->track_count ? 5 : 4;
        case H2H_TRACKS: return m->song_count;
        case H2H_COLLECTION: return 10;
        case H2H_SETTINGS: return 4;
        case H2H_ACCOUNT: return 3;
        default: return 0;
    }
}
static void clear_taps(h2h_model_t *m) { m->tap_count = 0; m->tap_gap_ms = 0; }
static void page(h2h_model_t *m, h2h_page_t p) {
    if (m->page == H2H_PLAYER && p != H2H_PLAYER) clear_taps(m);
    m->page = p; m->selection = 0;
}
void h2h_start_track(h2h_model_t *m, unsigned song) {
    if (!m->song_count || song >= m->song_count) return;
    clear_taps(m);
    m->track = song + (h2h_chorus_mode(m) ? m->song_count : 0);
    ++m->generation; m->audio_error = false;
    m->start_ms = m->position_ms = 0;
    m->countdown_ms = (m->save.countdown & 1u) ? 5000 : 0;
    m->want_play = !m->countdown_ms;
}
void h2h_seek_relative(h2h_model_t *m, int delta_ms) {
    if(!m->generation || !m->duration_ms || m->audio_error) return;
    int64_t target=(int64_t)m->position_ms+delta_ms;
    uint32_t last=m->duration_ms>20?m->duration_ms-20:0;
    m->start_ms=m->position_ms=target<0?0:target>last?last:(uint32_t)target;
    m->countdown_ms=0;
    ++m->generation; /* Keep playing/paused; never count the skipped interval. */
}
static void finish_taps(h2h_model_t *m) {
    unsigned count = m->tap_count;
    int direction = m->tap_key == H2H_UP ? 1 : -1;
    clear_taps(m);
    if (!count) return;
    int volume = (int)m->save.volume + direction * (int)count * 5;
    m->save.volume = volume < 0 ? 0 : volume > 100 ? 100 : (uint32_t)volume;
    m->dirty = true;
}
static void player_tap(h2h_model_t *m, h2h_key_t key) {
    if (m->tap_count && m->tap_key != key) finish_taps(m);
    if (!m->tap_count) m->tap_key = (uint8_t)key;
    ++m->tap_count;
    m->tap_gap_ms = TAP_GAP_MS;
    if (m->tap_count < 3) return;
    clear_taps(m);
    if (!m->song_count) return;
    unsigned song = h2h_current_song(m);
    unsigned next = key == H2H_UP ? (song + m->song_count - 1) % m->song_count : (song + 1) % m->song_count;
    bool was_playing = m->want_play;
    h2h_start_track(m, next);
    m->countdown_ms = 0;
    m->want_play = was_playing;
}
void h2h_audio_end(h2h_model_t *m, unsigned generation, bool failed) {
    if (generation != m->generation) return;
    finish_taps(m);
    if (failed) { m->audio_error = true; m->want_play = false; m->countdown_ms = 0; return; }
    unsigned next = h2h_current_song(m);
    if (m->save.mode == 0 && m->song_count) next = (next + 1) % m->song_count;
    if (m->save.mode == 1 && m->song_count > 1) next = (next + 1 + random_next(m) % (m->song_count - 1)) % m->song_count;
    h2h_start_track(m, next);
}
static void game_start(h2h_model_t *m) {
    page(m, H2H_GAME); m->game_ms = 45000; m->game_step_ms = m->spawn_ms = 0;
    m->caught = m->game_reward = 0; m->basket = 2;
    m->game_paused = m->game_settled = false;
    memset(m->drops, 0, sizeof(m->drops));
}
static void game_finish(h2h_model_t *m) {
    if (!m->game_settled) {
        m->game_reward = m->caught / 5;
        if (m->game_reward > 5) m->game_reward = 5;
        reward(m, m->game_reward); m->game_settled = true;
    }
    page(m, H2H_RESULT); m->pose = H2H_CELEBRATE; m->pose_ms = 2400;
}
static void back(h2h_model_t *m) {
    if (m->page == H2H_GAME) { page(m, H2H_HOME); return; }
    if (m->page == H2H_PLAYER) {
        if (m->countdown_ms) { m->countdown_ms = 0; m->want_play = false; }
        page(m, H2H_MUSIC); return;
    }
    if (m->page == H2H_TRACKS) { page(m, H2H_MUSIC); return; }
    if (m->page == H2H_CARD) { page(m, H2H_COLLECTION); return; }
    if (m->page == H2H_ACCOUNT) { page(m, H2H_SETTINGS); return; }
    page(m, H2H_HOME);
}
void h2h_input(h2h_model_t *m, h2h_key_t key, h2h_event_t event) {
    if(m->page==H2H_PLAYER && key!=H2H_OK) {
        if(event==H2H_PRESS) { m->suppress_volume[key]=false; return; }
        if(event==H2H_LONG) { m->suppress_volume[key]=true; finish_taps(m); h2h_seek_relative(m,key==H2H_UP?10000:-10000); return; }
        if(event!=H2H_CLICK || m->suppress_volume[key]) return;
        player_tap(m,key);
        return;
    }
    if (key == H2H_OK) {
        if (event == H2H_PRESS) { if(m->page==H2H_PLAYER) finish_taps(m); m->suppress_ok = false; return; }
        if (event == H2H_LONG) { m->suppress_ok = true; back(m); return; }
        if (event != H2H_CLICK || m->suppress_ok) return;
    } else if (event != H2H_PRESS) return;
    if (m->page == H2H_PLAYER) {
        if (key == H2H_OK) {
            if (m->countdown_ms) { m->countdown_ms = 0; m->want_play = false; }
            else if (m->audio_error || !m->generation) h2h_start_track(m, h2h_current_song(m));
            else m->want_play = !m->want_play;
        }
        return;
    }
    if (m->page == H2H_GAME) {
        if (key == H2H_OK) m->game_paused = !m->game_paused;
        else if (!m->game_paused) {
            if (key == H2H_UP && m->basket) --m->basket;
            if (key == H2H_DOWN && m->basket < 4) ++m->basket;
        }
        return;
    }
    if (key != H2H_OK) {
        unsigned n = h2h_menu_count(m);
        if (n) m->selection = (m->selection + (key == H2H_UP ? n - 1 : 1)) % n;
        return;
    }
    switch (m->page) {
        case H2H_HOME: {
            const h2h_page_t destinations[] = {H2H_ROOM,H2H_MUSIC,H2H_GAME,H2H_COLLECTION,H2H_BADGE,H2H_SETTINGS};
            h2h_page_t target = destinations[m->selection];
            if (target == H2H_GAME) game_start(m); else page(m, target);
            break;
        }
        case H2H_ROOM:
            if (m->selection < 2) { m->pose = m->selection == 0 ? H2H_WAVE : H2H_HEART; m->pose_ms = 2000; }
            if (m->selection == 2) { m->save.outfit = (m->save.outfit + 1) % h2h_outfit_count(m); m->dirty = true; }
            if (m->selection == 3) { m->save.room = (m->save.room + 1) % h2h_room_count(m); m->dirty = true; }
            break;
        case H2H_MUSIC:
            if (m->selection == 0) { if (!m->generation) h2h_start_track(m, h2h_current_song(m)); page(m, H2H_PLAYER); }
            else if (m->selection == 1) page(m, H2H_TRACKS);
            else if (m->selection == 2 && m->song_count < m->track_count) {
                unsigned song=h2h_current_song(m); bool was_playing=m->want_play;
                m->save.countdown ^= 2u; m->track=song+(h2h_chorus_mode(m)?m->song_count:0);
                if(m->generation) ++m->generation;
                m->position_ms=m->start_ms=m->countdown_ms=0;
                m->want_play=was_playing; m->audio_error=false; m->dirty=true;
            }
            else if (m->selection == (m->song_count < m->track_count ? 3u : 2u)) {
                m->save.mode = (m->save.mode + 1) % 3; m->dirty = true;
            }
            else if (m->selection == (m->song_count < m->track_count ? 4u : 3u)) {
                m->save.countdown ^= 1u; m->dirty = true;
            }
            break;
        case H2H_TRACKS: h2h_start_track(m, m->selection); page(m, H2H_PLAYER); break;
        case H2H_COLLECTION:
            if (m->selection == 8) { m->save.outfit = (m->save.outfit + 1) % h2h_outfit_count(m); m->dirty = true; }
            else if (m->selection == 9) { m->save.room = (m->save.room + 1) % h2h_room_count(m); m->dirty = true; }
            else if (m->save.cards & (1u << m->selection)) m->page = H2H_CARD;
            break;
        case H2H_CARD: m->save.badge_card = m->selection; m->dirty = true; page(m, H2H_BADGE); break;
        case H2H_RESULT: game_start(m); break;
        case H2H_SETTINGS:
            if (m->selection == 0) m->save.volume = (m->save.volume + 5) % 105;
            else if (m->selection == 1) m->save.countdown ^= 1u;
            else if (m->selection == 2) m->save.mode = (m->save.mode + 1) % 3;
            else { page(m, H2H_ACCOUNT); break; }
            m->dirty = true; break;
        case H2H_ACCOUNT: break; /* Main loop owns slow account actions. */
        default: break;
    }
}
void h2h_tick(h2h_model_t *m, uint32_t ms) {
    m->animation_ms+=ms;
    if (m->tap_gap_ms) {
        if (ms >= m->tap_gap_ms) finish_taps(m);
        else m->tap_gap_ms -= ms;
    }
    if (m->pose_ms) { m->pose_ms = ms >= m->pose_ms ? 0 : m->pose_ms - ms; if (!m->pose_ms) m->pose = H2H_IDLE; }
    if (m->countdown_ms) {
        m->countdown_ms = ms >= m->countdown_ms ? 0 : m->countdown_ms - ms;
        if (!m->countdown_ms) m->want_play = true;
    }
    if (m->page != H2H_GAME || m->game_paused || m->game_settled) return;
    uint32_t used = ms < m->game_ms ? ms : m->game_ms;
    m->game_ms -= used; m->game_step_ms += used;
    while (m->game_step_ms >= 50) {
        m->game_step_ms -= 50; m->spawn_ms += 50;
        if (m->spawn_ms >= 650) {
            m->spawn_ms = 0;
            for (unsigned i = 0; i < 6; ++i) if (!m->drops[i].active) {
                m->drops[i] = (h2h_drop_t){(int)(random_next(m) % 5), 0, true, (random_next(m) % 3) == 0}; break;
            }
        }
        for (unsigned i = 0; i < 6; ++i) if (m->drops[i].active) {
            m->drops[i].y += 5;
            if (m->drops[i].y >= 170) {
                if (m->drops[i].lane == (int)m->basket) ++m->caught;
                m->drops[i].active = false;
            }
        }
    }
    if (!m->game_ms) game_finish(m);
}
