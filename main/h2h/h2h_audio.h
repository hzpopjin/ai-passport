#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct { unsigned generation, track, volume; bool play; uint32_t start_ms; } h2h_audio_request_t;
typedef struct {
    unsigned generation;
    uint64_t played_samples;
    uint32_t position_ms;
    bool playing, ended, failed;
} h2h_audio_status_t;
esp_err_t h2h_audio_start(void);
void h2h_audio_request(const h2h_audio_request_t *request);
h2h_audio_status_t h2h_audio_status(void);
