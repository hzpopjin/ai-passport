#pragma once
#include "h2h_resource.h"
#include <stdbool.h>
#include <stdint.h>

#define H2H_RESOURCE_PROFILE "pocket-visual-v2"
#define H2H_RESOURCE_URL_MAX 512u
typedef struct {
    uint32_t capacity[2];
    unsigned key_count;
    char key_ids[8][33];
    bool wifi_usable;
} h2h_resource_capabilities_t;
typedef enum {
    H2H_RESOURCE_JOB_NONE, H2H_RESOURCE_JOB_BEGIN, H2H_RESOURCE_JOB_CHUNK,
    H2H_RESOURCE_JOB_WIFI, H2H_RESOURCE_JOB_FINISH, H2H_RESOURCE_JOB_RECEIPT
} h2h_resource_job_kind_t;
/* One bounded operation runs outside the model mutex on the existing worker.
 * No borrowed JSON, ticket or stack pointers survive that boundary. */
typedef struct {
    h2h_resource_job_kind_t kind;
    uint64_t epoch, model_generation;
    uint32_t revision, connection_generation, offset, length;
    uint32_t locator_started_ms, locator_ttl_ms, session_started_ms, session_ttl_ms;
    char session[33], work_id[33], locator[H2H_RESOURCE_URL_MAX];
    h2h_resource_manifest_t manifest;
    unsigned char data[1024], chunk_sha256[32];
    bool restart;
} h2h_resource_job_t;
typedef enum {
    H2H_RESOURCE_WORK_OK, H2H_RESOURCE_WORK_WAIT, H2H_RESOURCE_WORK_FAILED,
    H2H_RESOURCE_WORK_TRANSPORT_FAILED, H2H_RESOURCE_WORK_RESTART_REQUIRED,
    H2H_RESOURCE_WORK_UNSUPPORTED
} h2h_resource_work_status_t;
typedef struct {
    uint32_t cursor, active_generation;
    unsigned char prefix_sha256[32], manifest_sha256[32];
    bool durable_visible;
} h2h_resource_progress_t;
/* A locator has one short-lived opaque ticket, never account/device headers.
 * The pinned origin and SHA path are part of this proposed v2 TLS contract. */
bool h2h_resource_locator_valid(const char *url, const unsigned char sha256[32]);
bool h2h_resource_http_range_valid(int status, uint32_t offset, uint32_t length,
                                   size_t received, const char *content_range,
                                   const char *content_encoding);
