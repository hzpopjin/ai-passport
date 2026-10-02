#pragma once
#include "h2h_model.h"
#include "h2h_cloud.h"
#include <stdbool.h>
void h2h_ui_render(const h2h_model_t *m, unsigned position_ms, bool playing,
                   int battery, bool storage_ok, bool input_ok, unsigned ticks,
                   const h2h_cloud_status_t *cloud, bool ble_connected, int passkey);
