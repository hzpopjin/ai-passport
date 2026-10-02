#pragma once
#include "h2h_model.h"
typedef struct { unsigned pose; int dx,dy; bool hearts; } h2h_motion_t;
h2h_motion_t h2h_motion(const h2h_model_t *model,bool playing);
