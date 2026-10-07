#include "pocket_log.h"
/* Main host scenarios model the unchanged minimal partition layout. Actual
 * adapter/RTC/Flash behaviour is exercised by test_pocket_log_service.c. */
void pocket_log_start(void){}
void pocket_log_key(uint8_t k,uint8_t e){(void)k;(void)e;}
void pocket_log_state(uint8_t p,uint8_t g,uint8_t c,uint8_t r){(void)p;(void)g;(void)c;(void)r;}
void pocket_log_metrics(uint32_t h,uint32_t m,uint32_t l,uint32_t s){(void)h;(void)m;(void)l;(void)s;}
void pocket_log_error(uint8_t e){(void)e;}
#include "pocket_profile.h"
#include <string.h>
/* Legacy-cloud main-loop harness has no profile partition/feature. Actual
 * profile RPC, ESP bounds and LVGL views are tested in their own suites. */
void h2h_cloud_profile(pocket_profile_view_t *out){if(out)memset(out,0,sizeof(*out));}
void h2h_ui_profile(const pocket_profile_view_t *view){(void)view;}
