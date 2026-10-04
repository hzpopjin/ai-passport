#include "h2h_ownership.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    h2h_ownership_t state={0};
    const char *op="0123456789abcdef0123456789abcdef";
    assert(!h2h_ownership_can_play(&state));
    assert(h2h_ownership_sync(&state,1,7,true,NULL));
    assert(!h2h_ownership_can_play(&state));
    assert(!h2h_ownership_reset_done(&state,0));
    assert(h2h_ownership_reset_done(&state,1));
    assert(h2h_ownership_can_play(&state)); /* Offline keeps activated state. */
    assert(!h2h_ownership_sync(&state,1,8,true,NULL));
    assert(h2h_ownership_unbind(&state,op));
    assert(!h2h_ownership_can_play(&state));
    h2h_ownership_t reboot=state; /* NVS blob round-trip after successful commit. */
    assert(!h2h_ownership_can_play(&reboot));
    assert(!h2h_ownership_sync(&state,1,7,true,op));
    assert(h2h_ownership_sync(&state,2,0,false,"wrong"));
    assert(state.pending_unbind[0]);
    assert(!h2h_ownership_reset_done(&state,2));
    assert(h2h_ownership_sync(&state,2,0,false,op));
    assert(!state.pending_unbind[0]);
    assert(h2h_ownership_sync(&state,3,8,true,NULL));
    assert(!h2h_ownership_can_play(&state));
    assert(h2h_ownership_reset_done(&state,3));
    assert(h2h_ownership_can_play(&state));
    assert(!h2h_ownership_sync(&state,1,7,true,NULL));
    assert(h2h_ownership_sync(&state,4,0,false,NULL));
    assert(!h2h_ownership_can_play(&state));
    assert(!h2h_ownership_unbind(&state,op));
    puts("Ownership host tests: PASS");
    return 0;
}
