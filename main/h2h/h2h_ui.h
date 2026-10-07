#pragma once
#include <stdbool.h>
void h2h_ui_reset(void);
/* Call under LVGL lock. Hidden diagnostics do not grant activation. */
void h2h_ui_setup_state(bool diagnostics, bool confirm_debug, int ble_error, bool local_waiting);
#include "h2h_model.h"
#include "h2h_cloud.h"
#include "h2h_resource_runtime.h"
#include <stdbool.h>
#include "stayc_scene.h"
/* Call under bsp_lvgl_lock immediately before render; resources remain mapped until afterward. */
void h2h_ui_resources(const h2h_resource_view_t *view);
void h2h_ui_render(const h2h_model_t *m, unsigned position_ms, bool playing,
                   int battery, bool storage_ok, bool input_ok, unsigned ticks,
                   const h2h_cloud_status_t *cloud, bool ble_connected, int passkey,
                   const stayc_control_t *visuals);

/* Stable public pixel buffer owned by main; caller holds the LVGL lock. */
void h2h_ui_profile(const pocket_profile_view_t *view);
