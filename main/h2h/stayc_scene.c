#include "stayc_scene.h"
#include <string.h>

bool stayc_unpack(const uint8_t *source, size_t length, uint8_t *out, size_t capacity) {
    if (!source || !out || !length || !capacity) return false;
    size_t read = 0, written = 0;
    while (read < length) {
        unsigned control = source[read++], count = (control & 127u) + 1u;
        if (count > capacity - written) return false;
        if (control & 128u) {
            if (read == length) return false;
            memset(out + written, source[read++], count);
        } else {
            if (count > length - read) return false;
            memcpy(out + written, source + read, count);
            read += count;
        }
        written += count;
    }
    return written == capacity;
}

const char *stayc_action_label(unsigned action) {
    static const char *life[] = {"一起吃饭", "手机时光", "羽毛球练习", "迷你高尔夫"};
    return action < 8 ? stayc_actions[0][0][action].label : action < 12 ? life[action - 8] : "";
}
unsigned stayc_action_duration(unsigned action) {
    static const unsigned life[] = {12000, 11000, 13200, 12000};
    return action < 8 ? stayc_actions[0][0][action].duration : action < 12 ? life[action - 8] : 0;
}
bool stayc_control_input(stayc_control_t *s, h2h_model_t *m, h2h_key_t key, h2h_event_t event) {
    if(m->play_allowed&&m->page==H2H_COLLECTION&&key==H2H_OK&&event==H2H_CLICK&&!m->suppress_ok){
        if(m->selection==8){m->save.outfit=(m->save.outfit+1)%2;m->dirty=true;return true;}
        if(m->selection==9){m->save.room=(m->save.room+1)%3;m->dirty=true;return true;}
    }
    if (!m->play_allowed || m->page != H2H_ROOM) { s->view = STAYC_NORMAL; return false; }
    if (s->view == STAYC_NORMAL) {
        if (key == H2H_OK && event == H2H_CLICK && !m->suppress_ok && m->selection < 2) {
            s->view = m->selection ? STAYC_ACTION_MENU : STAYC_MEMBER_MENU;
            s->selection = m->selection ? 0 : s->member;
            return true;
        }
        if (key == H2H_OK && event == H2H_CLICK && !m->suppress_ok && m->selection == 2) {
            m->save.outfit = (m->save.outfit + 1) % 2; m->dirty = true; return true;
        }
        if (key == H2H_OK && event == H2H_CLICK && !m->suppress_ok && m->selection == 3) {
            m->save.room = (m->save.room + 1) % 3; m->dirty = true; return true;
        }
        return false;
    }
    if (key == H2H_OK) {
        if (event == H2H_PRESS) { s->suppress_ok = false; return true; }
        if (event == H2H_LONG) {
            s->suppress_ok = m->suppress_ok = true;
            s->view = s->view == STAYC_ACTION_PLAYER || s->view == STAYC_DECOR_MENU ? STAYC_ACTION_MENU : STAYC_NORMAL;
            s->elapsed = 0; s->paused = false; return true;
        }
        if (event != H2H_CLICK || s->suppress_ok) return true;
        if (s->view == STAYC_MEMBER_MENU) {
            s->preferences_dirty |= s->member != s->selection;
            s->member = s->selection; s->view = STAYC_NORMAL;
        } else if (s->view == STAYC_ACTION_MENU) {
            if(s->selection==12){s->view=STAYC_DECOR_MENU;s->selection=0;return true;}
            s->action = s->selection; s->elapsed = 0; s->paused = false; s->view = STAYC_ACTION_PLAYER;
        } else if (s->view == STAYC_DECOR_MENU) {
            unsigned shift=s->selection*2,value=((s->decor>>shift)&3u)+1;
            s->decor=(s->decor&~(3u<<shift))|((value%3)<<shift);s->preferences_dirty=true;
        } else if (s->elapsed >= stayc_action_duration(s->action)) {
            s->elapsed = 0; s->paused = false;
        } else s->paused = !s->paused;
    } else if (event == H2H_PRESS && s->view != STAYC_ACTION_PLAYER) {
        unsigned n = s->view == STAYC_MEMBER_MENU ? STAYC_MEMBERS : s->view == STAYC_DECOR_MENU ? 4 : 13;
        s->selection = (s->selection + (key == H2H_UP ? n - 1 : 1)) % n;
    }
    return true;
}
void stayc_control_tick(stayc_control_t *s, const h2h_model_t *m, uint32_t ms) {
    if (!m->play_allowed || m->page != H2H_ROOM) { s->view = STAYC_NORMAL; s->paused = false; s->elapsed = 0; return; }
    if (s->view == STAYC_ACTION_PLAYER && !s->paused) {
        unsigned duration = stayc_action_duration(s->action);
        s->elapsed = ms >= duration - s->elapsed ? duration : s->elapsed + ms;
    }
}
void stayc_sample(unsigned member, unsigned outfit, unsigned action, uint32_t ms, stayc_frame_t *out) {
    memset(out, 0, sizeof(*out));
    out->duration = stayc_action_duration(action);
    out->elapsed = ms < out->duration ? ms : out->duration;
    out->done = ms >= out->duration;
    out->scale_q8 = 512; out->x = 56; out->y = 60;
    if (member >= 6 || outfit >= 2 || action >= 12) return;
    if (action < 8) {
        const stayc_action_t *a = &stayc_actions[member][outfit][action];
        unsigned start = 0;
        for (unsigned i = 0; i < a->count; ++i) {
            const stayc_phase_t *p = &a->phases[i];
            if (out->elapsed < start + p->ms || i + 1 == a->count) {
                unsigned t = out->elapsed - start, q = (t < p->ms ? t : p->ms) * 256 / p->ms;
                out->pose = p->pose; out->prop = p->prop; out->attachment = p->attachment; out->flash = p->flash;
                out->scroll = p->scroll; out->progress_q8 = q;
                bool close = action != 6 && action != 7;
                out->scale_q8 = close ? 512 : 256;
                out->x = close ? 56 + 2 * p->dx : 66 + p->dx;
                out->y = close ? 60 + 2 * p->dy : 118 + p->dy;
                out->effect = p->effect;
                int ex = p->from_x + (p->to_x - p->from_x) * (int)q / 256;
                int ey = p->from_y + (p->to_y - p->from_y) * (int)q / 256 - (int)(4 * p->arc * q * (256 - q) / 65536);
                out->effect_x = close ? ex * 2 - 76 : ex;
                out->effect_y = close ? ey * 2 - 176 : ey;
                return;
            }
            start += p->ms;
        }
    }
    unsigned t = out->elapsed;
    if (action == 8) {
        out->y = 75;
        out->pose = t >= 10100 ? 5 : t >= 8400 ? 29 : t >= 1800 && (t - 1800) % 1650 >= 742 ? 17 : 16;
        out->prop = t >= 700 && t < 8400 ? 5 : t < 10100 && t >= 8400 ? 6 : 0;
    } else if (action == 9) {
        out->y = 68;
        out->pose = t < 700 || t >= 10000 ? 5 : t >= 6600 && t < 8800 ? 30 : t >= 6200 && t < 6600 ? 7 : 6;
        out->prop = t >= 700 && t < 10000 ? 2 : 0; out->attachment = 1;
    } else if (action == 10) {
        out->scale_q8 = 256; out->x = 58; out->y = 126;
        unsigned cycle = t >= 1500 ? (t - 1500) % 3000 : 0;
        out->pose = t >= 10800 ? 31 : t < 1500 || cycle < 1000 || cycle >= 2050 ? 8 : cycle < 1500 ? 9 : cycle < 1700 ? 10 : 11;
        out->prop = t < 10800 ? 3 : 0; out->attachment = 1;
        if (t >= 1500 && t < 10800) {
            const stayc_sprite_t *contact = &stayc_sprites[member][outfit][10];
            int sign = contact->hand ? 1 : -1, cx = 65 + contact->anchors[contact->hand][0] + 14 * sign;
            if (sign < 0 && cx > 75) cx = 75;
            if (sign > 0 && cx < 119) cx = 119;
            int cy = 126 + contact->anchors[contact->hand][1] - 11;
            unsigned q = cycle <= 1500 ? cycle * 256 / 1500 : (3000 - cycle) * 256 / 1500;
            out->effect = 2; out->effect_x = 207 + (cx - 207) * (int)q / 256;
            out->effect_y = 91 + (cy - 91) * (int)q / 256;
        }
    } else if (action == 11) {
        out->scale_q8 = 384; out->x = 48; out->y = 108;
        out->pose = t >= 9700 ? 24 : t >= 4600 ? 19 : 18;
        out->prop = t < 9700 ? 7 : 0;
        out->effect = t < 9300 ? 3 : 0;
        unsigned travel = t <= 4600 ? 0 : t >= 9300 ? 256 : (t - 4600) * 256 / 4700;
        out->effect_x = 150 + (int)(54 * (65536 - (256 - travel) * (256 - travel)) / 65536);
        out->effect_y = 221;
    }
}
unsigned stayc_reaction_pose(unsigned card, uint32_t ms) {
    unsigned t = ms % 4000;
    if (t < 88 || t > 3000) return 0;
    switch (card % 8) {
        case 0: return 1;
        case 1: return 20;
        case 2: return t < 1600 ? 21 : 22;
        case 3: return 23;
        case 4: return t < 1000 ? 15 : 4;
        case 5: return t < 1300 ? 24 : 25;
        case 6: return t / 320 % 2 ? 26 : 27;
        default: return 28;
    }
}
