#pragma once
#include "h2h_model.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { H2H_THEME_PACK_SIZE = 16 };
typedef struct { uint8_t outfit, room; } h2h_theme_t;

/* Pure parser: reject unknown versions and indices before changing saved state. */
bool h2h_theme_parse(const void *bytes, size_t length, h2h_theme_t *theme);
void h2h_theme_apply(h2h_model_t *model, const h2h_theme_t *theme);
