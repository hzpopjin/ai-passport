#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {lv_obj_t *hearts,*battery,*bluetooth,*charging;bool charging_known_active;} h2h_header_t;
/* Call with the BSP LVGL lock held. Bluetooth describes the actual link,
 * independent of advertising, authentication or cloud sync results. */
void h2h_header_create(h2h_header_t *header,lv_obj_t *screen);
/* Only a verified charging signal may enable this indicator. SOC100 or a
 * connected USB data port is not evidence of battery charging. */
void h2h_header_charging(h2h_header_t *header,bool charging);
void h2h_header_update(h2h_header_t *header,lv_obj_t *title,uint32_t hearts,
                       int battery,bool debug,bool connected);
