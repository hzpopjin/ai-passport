#pragma once
#include <stdint.h>
#include "pocket_log_ring.h"

/* Opt-in by exact dedicated partition; no task or Flash access in the default
 * layout. Setters only update a small public RAM/RTC checkpoint. */
void pocket_log_start(void);
void pocket_log_key(uint8_t key,uint8_t event);
void pocket_log_state(uint8_t page,uint8_t game,uint8_t cloud_state,uint8_t result);
void pocket_log_metrics(uint32_t heap,uint32_t minimum,uint32_t largest,uint32_t main_stack_bytes);
void pocket_log_error(uint8_t code);
