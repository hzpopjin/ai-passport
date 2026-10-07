#pragma once
#include "h2h_pocket.h"
#include "h2h_relay_transport.h"
#include "h2h_fullsave.h"
#include "h2h_resource_protocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    bool (*hmac)(const uint8_t key[32],const char *bytes,size_t length,uint8_t out[32]);
    bool (*sha)(const void *bytes,size_t length,uint8_t out[32]);
    void (*random)(void *bytes,size_t length);
    h2h_pocket_write_fn write;
    void *write_context;
    h2h_fullsave_t *full;
    const h2h_fullsave_io_t *full_io;
    h2h_resource_capabilities_t resources;
    h2h_resource_work_status_t (*resource_work)(const h2h_resource_job_t *,h2h_resource_progress_t *,void *);
    void *resource_context;
} h2h_relay_ops_t;
typedef enum {H2H_RELAY_IDLE,H2H_RELAY_BEGIN,H2H_RELAY_CLAIM,H2H_RELAY_CARD_CONFIRM,H2H_RELAY_CONFIRM,H2H_RELAY_CAPABILITIES,H2H_RELAY_SYNC,H2H_RELAY_RESOURCE_MANIFEST,H2H_RELAY_RESOURCE_CHUNK,H2H_RELAY_RESOURCE_WORK,H2H_RELAY_RESOURCE_ACK,H2H_RELAY_ACK,H2H_RELAY_COMPLETE,H2H_RELAY_ERROR} h2h_relay_stage_t;
typedef struct {
    h2h_relay_ops_t ops;
    h2h_pocket_t *pocket;
    uint8_t key[32];bool enrolled,active,gate;
    char device[33],session[33],request[33],operation[24],pair_code[33],error[40];
    char request_json[3800],ack_json[1050],last_request[33],last_response_mac[65],session_mac[65];
    uint32_t account,started,ttl_ms;
    uint64_t pair_epoch,confirmed_epoch,server_epoch;
    bool server_active,owner_authorized,pair_after_sync,pending_only;
    unsigned version,max_chunk;
    bool full_selected;
    char sync_request[33];
    uint8_t sent_full[164],ack_full[164];
    uint64_t sent_full_revision,ack_full_generation;
    h2h_relay_stage_t stage;
    h2h_pocket_record_t sent,ack_record;
    bool stars_valid;uint32_t stars;uint64_t stars_epoch,stars_generation;
    h2h_resource_capabilities_t resource_caps;
    h2h_resource_job_t resource_job;
    h2h_resource_progress_t resource_progress;
    char resource_id[33],resource_sha[65],resource_ack_json[300];
    uint32_t resource_revision;
    uint64_t resource_model_generation;
    bool resource_selected,resource_wifi,resource_started,resource_applied,resource_job_taken;
    bool resource_ack_confirmed,resource_complete_pending,resource_offered,resource_ticket_offer;
} h2h_relay_t;
void h2h_relay_init(h2h_relay_t *r,h2h_pocket_t *p,const char device[33],const uint8_t *trusted_key,const h2h_relay_ops_t *ops);
/* All calls including RPC run on the cloud worker under the model mutex. */
size_t h2h_relay_rpc(h2h_relay_t *r,const char *input,size_t length,char out[H2H_RELAY_RPC_MAX+1],uint32_t now_ms);
bool h2h_relay_confirm(h2h_relay_t *r,uint32_t now_ms);
void h2h_relay_disconnect(h2h_relay_t *r);
void h2h_relay_stars(h2h_relay_t *r,uint64_t epoch,uint64_t generation,uint32_t stars);
/* Take/done are short model-locked operations; execute resource_work outside it. */
bool h2h_relay_resource_take(h2h_relay_t *r,h2h_resource_job_t *out);
bool h2h_relay_resource_done(h2h_relay_t *r,const h2h_resource_job_t *job,
                            h2h_resource_work_status_t status,const h2h_resource_progress_t *progress,
                            uint32_t now_ms);
