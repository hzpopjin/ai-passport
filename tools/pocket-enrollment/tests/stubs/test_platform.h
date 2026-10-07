#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <openssl/hmac.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 4354
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_MAC_EFUSE_FACTORY 0
#define BSP_LCD_BL 21
#define GPIO_MODE_OUTPUT 1
#define pdMS_TO_TICKS(x) (x)
typedef unsigned long nvs_handle_t;
typedef struct { int tx_buffer_size,rx_buffer_size; } usb_serial_jtag_driver_config_t;
typedef struct { uint8_t app_elf_sha256[32]; } esp_app_desc_t;
typedef struct {int key;int64_t sampled_us,changed_us;uint64_t sequence;} bsp_button_snapshot_t;
typedef EVP_MD mbedtls_md_info_t;
#define MBEDTLS_MD_SHA256 256
int usb_serial_jtag_write_bytes(const void*,size_t,int);
int usb_serial_jtag_read_bytes(void*,size_t,int);
int usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t*);
int nvs_flash_init(void);
int nvs_open(const char*,int,nvs_handle_t*);
int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
int nvs_get_str(nvs_handle_t,const char*,char*,size_t*);
int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
int64_t esp_timer_get_time(void);
int esp_read_mac(uint8_t*,int);
void esp_fill_random(void*,size_t);
const esp_app_desc_t* esp_app_get_description(void);
bool bsp_button_snapshot(bsp_button_snapshot_t*);
int bsp_button_init(void*,void*);
int gpio_reset_pin(int);
int gpio_set_direction(int,int);
int gpio_set_level(int,int);
const mbedtls_md_info_t* mbedtls_md_info_from_type(int);
int mbedtls_md_hmac(const mbedtls_md_info_t*,const unsigned char*,size_t,const unsigned char*,size_t,unsigned char*);
