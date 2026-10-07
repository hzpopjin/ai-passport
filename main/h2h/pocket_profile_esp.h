#pragma once
#include "pocket_profile.h"
/* Read-only layout discovery; absent/mismatched layout produces no Flash I/O. */
bool pocket_profile_esp_io(pocket_profile_io_t *io);
