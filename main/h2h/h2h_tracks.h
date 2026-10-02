#pragma once
#include <stdint.h>
typedef struct {
    const char *title, *artist;
    uint32_t duration_ms;
    const uint8_t *start, *end;
    uint32_t packet_count, pre_skip, pcm_samples;
    uint16_t packet_bytes;
} h2h_track_t;
extern const h2h_track_t h2h_tracks[];
extern const unsigned h2h_track_count;
extern const unsigned h2h_song_count;
