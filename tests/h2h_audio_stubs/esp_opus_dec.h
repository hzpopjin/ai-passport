#pragma once
#include <stdint.h>
#include <stdbool.h>
#define ESP_AUDIO_ERR_OK 0
#define ESP_OPUS_DEC_FRAME_DURATION_20_MS 3
typedef struct { uint32_t sample_rate; uint8_t channel; int frame_duration; bool self_delimited; } esp_opus_dec_cfg_t;
typedef struct { uint8_t *buffer; uint32_t len,consumed; } esp_audio_dec_in_raw_t;
typedef struct { uint8_t *buffer; uint32_t len,decoded_size; } esp_audio_dec_out_frame_t;
typedef struct { uint32_t sample_rate,channel,bits_per_sample; } esp_audio_dec_info_t;
int esp_opus_dec_open(void *cfg,uint32_t size,void **handle);
int esp_opus_dec_close(void *handle);
int esp_opus_dec_decode(void *handle,esp_audio_dec_in_raw_t *raw,esp_audio_dec_out_frame_t *out,esp_audio_dec_info_t *info);
