#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Persist as one NVS blob. Caller must fail closed if persistence fails.
 * Not a device identity: never delete the independent per-device credential.
 * This pure protocol state is not yet wired into h2h_cloud/main. */
typedef struct {
    uint64_t epoch;
    uint64_t pending_epoch;
    uint32_t account_id;
    bool activated;
    bool reset_required;
    char pending_unbind[33];
} h2h_ownership_t;

bool h2h_ownership_can_play(const h2h_ownership_t *state);
bool h2h_ownership_unbind(h2h_ownership_t *state, const char operation_id[33]);
/* Authenticated TLS response only. Does not acknowledge local save reset.
 * Old responses cannot resurrect a newer local ownership period.
 * An unacknowledged local unbind always keeps play locked. */
bool h2h_ownership_sync(h2h_ownership_t *state, uint64_t epoch,
                        uint32_t account_id, bool activated,
                        const char *unbind_ack);
/* Call only after local progress and resource selection are reset durably. */
bool h2h_ownership_reset_done(h2h_ownership_t *state, uint64_t expected_epoch);
