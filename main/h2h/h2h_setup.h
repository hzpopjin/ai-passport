#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Raw key: -2 unavailable, -1 released, 0 UP, 1 DOWN, 2 OK. Starts only
 * after stable release; poll from a task, never an ISR. */
typedef struct {
    int candidate, stable;
    unsigned stage;
    uint64_t changed_ms, pressed_ms, deadline_ms;
    bool armed, pending;
    uint64_t sample_sequence,sample_now_ms;
    int sample_key;
    bool sampled;
} h2h_setup_gesture_t;
void h2h_setup_gesture_init(h2h_setup_gesture_t *g, uint64_t now);
bool h2h_setup_gesture_step(h2h_setup_gesture_t *g, int raw_key, uint64_t now);
/* Timer transition time prevents app-loop latency from extending a short hold.
 * Sequence gaps cancel rather than guessing unobserved presses/releases. */
bool h2h_setup_gesture_sample(h2h_setup_gesture_t *g,int key,uint64_t changed_ms,
                              uint64_t sampled_ms,uint64_t sequence,uint64_t now_ms);
/* Stable discovery URL uses only a public ID, never a credential or claim. */
bool h2h_setup_url(char *out, size_t size, const char *public_id);
