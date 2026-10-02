#pragma once
#include <stdint.h>
#include <stdbool.h>
#define ESP_AUDIO_ERR_OK 0
#define ESP_AUDIO_ERR_DATA_LACK (-3)
#define ESP_AUDIO_SIMPLE_DEC_TYPE_OGG 6
typedef void *esp_audio_simple_dec_handle_t;
typedef struct { int dec_type; } esp_audio_simple_dec_cfg_t;
typedef struct { uint8_t *buffer; uint32_t len; bool eos; uint32_t consumed; } esp_audio_simple_dec_raw_t;
typedef struct { uint8_t *buffer; uint32_t len,decoded_size,needed_size; } esp_audio_simple_dec_out_t;
typedef struct { uint32_t sample_rate,channel,bits_per_sample; } esp_audio_simple_dec_info_t;
int esp_audio_simple_dec_open(const esp_audio_simple_dec_cfg_t *cfg,esp_audio_simple_dec_handle_t *handle);
int esp_audio_simple_dec_close(esp_audio_simple_dec_handle_t handle);
int esp_audio_simple_dec_process(esp_audio_simple_dec_handle_t handle,esp_audio_simple_dec_raw_t *raw,esp_audio_simple_dec_out_t *out);
int esp_audio_simple_dec_get_info(esp_audio_simple_dec_handle_t handle,esp_audio_simple_dec_info_t *info);
