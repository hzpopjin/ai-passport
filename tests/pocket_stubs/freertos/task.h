#pragma once
#include "pocket_sdk.h"
typedef void *TaskHandle_t;
TaskHandle_t xTaskGetHandle(const char *name);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t handle);
