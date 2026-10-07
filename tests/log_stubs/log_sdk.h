#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define RTC_NOINIT_ATTR
#define MALLOC_CAP_8BIT 1
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_APP_FACTORY 0
#define ESP_PARTITION_SUBTYPE_DATA_NVS 2
#define ESP_PARTITION_SUBTYPE_DATA_PHY 1
#define ESP_RST_POWERON 1
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
typedef int esp_partition_type_t;
typedef struct {int type;uint8_t subtype;uint32_t address,size;char label[17];bool encrypted,readonly;} esp_partition_t;
typedef struct {uint8_t app_elf_sha256[32];} esp_app_desc_t;
typedef void *TaskHandle_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label);
int esp_partition_read(const esp_partition_t *p,size_t off,void *out,size_t n);
int esp_partition_write(const esp_partition_t *p,size_t off,const void *in,size_t n);
int esp_partition_erase_range(const esp_partition_t *p,size_t off,size_t n);
const esp_app_desc_t *esp_app_get_description(void);
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);
size_t heap_caps_get_largest_free_block(int caps);
int64_t esp_timer_get_time(void);
int esp_reset_reason(void);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t handle);
int xTaskCreate(void (*task)(void *),const char *name,unsigned bytes,void *ctx,unsigned priority,TaskHandle_t *handle);
void vTaskDelay(unsigned ticks);
void vTaskDelete(TaskHandle_t handle);
