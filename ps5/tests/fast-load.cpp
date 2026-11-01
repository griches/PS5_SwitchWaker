#include "pc/pc_fast_load.h"
#include <cassert>
#include <cstdint>

int main() {
    PcFastLoadState state;
    state.update(false, true, false, 0);
    state.update(false, true, false, 100);
    assert(!state.active && !state.covered); // Off by default, even on a black screen.
    state.update(true, false, false, 100);
    assert(!state.active);
    state.update(true, true, false, 100);
    for (uint32_t retrace = 100; retrace < 106; ++retrace) {
        state.update(true, true, false, retrace);
        state.update(true, true, false, retrace); // Several frame hooks must not double-count.
        assert(!state.active);
    }
    state.update(true, true, false, 106); assert(state.active);
    state.update(true, true, false, 705); assert(state.active);
    state.update(true, true, false, 706); assert(!state.active);
    state.update(true, true, false, 900); assert(!state.active); // Timeout never rearms in the same black.
    state.update(true, false, false, 901); assert(!state.active && !state.covered);
    state.update(true, true, false, 902);
    state.update(true, true, false, 908); assert(state.active);
    state.update(true, true, true, 908); assert(!state.active && !state.covered); // Menu opens immediately.
    state.update(true, true, false, 908); assert(!state.active);
    state.update(true, true, false, 914); assert(state.active);
    state.update(false, true, false, 914); assert(!state.active && !state.covered); // Live setting Off.
    state.update(true, true, false, 914); assert(!state.active);
    state.update(true, true, false, 920); assert(state.active);
    state.update(true, false, false, 920); assert(!state.active); // Fade becomes visible in this frame.
    // Retrace counter wrap is handled with unsigned elapsed arithmetic.
    state.update(true, true, false, UINT32_MAX - 3);
    state.update(true, true, false, 1); assert(!state.active);
    state.update(true, true, false, 2); assert(state.active);
    // Typical 30 Hz frames advance two retraces; settling lasts three full frames.
    state = {};
    for (uint32_t retrace = 0; retrace < 6; retrace += 2) {
        state.update(true, true, false, retrace); assert(!state.active);
    }
    state.update(true, true, false, 6); assert(state.active);
    // A transition that is never wholly covered cannot be accelerated.
    for (uint32_t retrace = 7; retrace < 200; ++retrace) {
        state.update(true, false, false, retrace); assert(!state.active);
    }
}
