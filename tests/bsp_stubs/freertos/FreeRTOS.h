#pragma once
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void test_bsp_enter_critical(portMUX_TYPE *mux);
void test_bsp_exit_critical(portMUX_TYPE *mux);
#define portENTER_CRITICAL(mux) test_bsp_enter_critical(mux)
#define portEXIT_CRITICAL(mux) test_bsp_exit_critical(mux)
