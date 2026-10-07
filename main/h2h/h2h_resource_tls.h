#pragma once
#include "h2h_resource_protocol.h"
typedef uint32_t (*h2h_resource_clock_fn)(void *);
typedef bool (*h2h_resource_scope_fn)(const h2h_resource_job_t *,void *);
/* Nonblocking ESP-TLS, fixed HTTPS origin, certificate bundle/hostname checks,
 * bounded header/body and one15s attempt within the25s HTTP ceiling.
 * No redirects, account JWT or device credential headers. */
bool h2h_resource_tls_get(const h2h_resource_job_t *job,void *out,
    h2h_resource_clock_fn clock,h2h_resource_scope_fn alive,void *ctx);
