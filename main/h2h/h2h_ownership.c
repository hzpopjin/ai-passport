#include "h2h_ownership.h"
#include <string.h>
#define MAX_EPOCH UINT64_C(9007199254740991)
static bool operation_valid(const char *id) {
    if (!id) return false;
    for (unsigned i=0; i<32; ++i) {
        char c=id[i];
        if (!((c>='0' && c<='9') || (c>='a' && c<='f'))) return false;
    }
    return id[32]==0;
}
bool h2h_ownership_can_play(const h2h_ownership_t *state) {
    return state && state->epoch>0 && state->epoch<=MAX_EPOCH && state->activated &&
           state->account_id>0 && !state->reset_required && !state->pending_unbind[0];
}
bool h2h_ownership_unbind(h2h_ownership_t *state, const char operation_id[33]) {
    if (!state || !operation_valid(operation_id)) return false;
    if (state->pending_unbind[0]) return strcmp(state->pending_unbind,operation_id)==0;
    if (!state->activated || !state->epoch || state->epoch>MAX_EPOCH) return false;
    state->pending_epoch=state->epoch;
    memcpy(state->pending_unbind,operation_id,33);
    state->activated=false;
    state->reset_required=true;
    return true;
}
bool h2h_ownership_sync(h2h_ownership_t *state, uint64_t epoch,
                        uint32_t account_id, bool activated, const char *unbind_ack) {
    if (!state || epoch>MAX_EPOCH || epoch<state->epoch ||
        (activated && (!epoch || !account_id)) || (!activated && account_id)) return false;
    /* Within an epoch the server must not change owner or activation status,
       except our local pending-unbind state already locked play early. */
    if (epoch==state->epoch && (account_id!=state->account_id ||
        (!state->pending_unbind[0] && activated!=state->activated))) return false;
    if (state->pending_unbind[0] && unbind_ack &&
        strcmp(state->pending_unbind,unbind_ack)==0) {
        if (epoch<=state->pending_epoch) return false;
        state->pending_unbind[0]=0;
        state->pending_epoch=0;
    }
    if (epoch!=state->epoch) state->reset_required=true;
    state->epoch=epoch;
    state->account_id=account_id;
    state->activated=activated;
    return true;
}
bool h2h_ownership_reset_done(h2h_ownership_t *state, uint64_t expected_epoch) {
    if (!state || state->epoch!=expected_epoch || state->pending_unbind[0]) return false;
    state->reset_required=false;
    return true;
}
