// Covered-screen loading policy; counters use game retraces, not wall-clock time.
#ifndef PC_FAST_LOAD_H
#define PC_FAST_LOAD_H
#include <cstdint>
struct PcFastLoadState {
    bool covered = false, active = false;
    uint32_t start = 0;
    void update(bool enabled, bool hidden, bool blocked, uint32_t retrace) {
        if (!enabled || !hidden || blocked) { *this = {}; return; }
        if (!covered) { covered = true; start = retrace; }
        const uint32_t elapsed = retrace - start;
        // Give the covered image six retraces to reach the display. Never
        // accelerate more than 600 retraces in a single continuous transition.
        active = elapsed >= 6 && elapsed < 606;
    }
};
void pc_fast_load_update();
bool pc_fast_load_active();
#endif
