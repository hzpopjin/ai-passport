#pragma once
#include "h2h_model.h"
#include "h2h_cloud.h"
#include "h2h_resource_runtime.h"
#include <stdbool.h>
/* Call under bsp_lvgl_lock immediately before render; resources remain mapped until afterward. */
void h2h_ui_resources(const h2h_resource_view_t *view);
void h2h_ui_render(const h2h_model_t *m, unsigned position_ms, bool playing,
                   int battery, bool storage_ok, bool input_ok, unsigned ticks,
                   const h2h_cloud_status_t *cloud, bool ble_connected, int passkey);
