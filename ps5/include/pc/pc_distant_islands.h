#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

// BlueWake hd_look.c's policy, expressed without guest addresses or actor-layout
// sentinels: original islands first, then nearest extra models within a budget.
// Classify at request time, independently of later option/player changes.
// A native request starts within the original show distance, not its unload band.
inline bool pc_island_optional_request(float base,float distance) {
    return !std::isfinite(base) || base<0 || !std::isfinite(distance) || distance>base;
}

struct PcIslandCandidate {
    float base = 0, distance = 0;
    bool extended = false, loaded = false, spilled = false, wanted = false;
};
struct PcIslandBudget {
    unsigned limit = 15;
    void select(PcIslandCandidate* islands, size_t count, bool enabled, bool player) {
        unsigned loaded = 0, spilled = 0;
        for (size_t i = 0; i < count; ++i) {
            loaded += islands[i].loaded;
            spilled += islands[i].spilled;
        }
        if (enabled && spilled) limit = std::min(limit, loaded > spilled ? loaded - spilled - 1 : 0);
        unsigned remaining = limit;
        for (size_t i = 0; i < count; ++i) {
            auto& island = islands[i];
            island.wanted = false;
            if (!enabled || !player || !std::isfinite(island.distance) || island.base < 0) continue;
            if (island.distance < island.base + 20000) {
                if (remaining) --remaining;
                // Preserve an already extended near island, avoiding a visible pop.
                island.wanted = island.extended;
            }
        }
        while (enabled && player && remaining) {
            size_t best = count;
            for (size_t i = 0; i < count; ++i) {
                const auto& island = islands[i];
                if (island.wanted || island.base < 0 || !std::isfinite(island.distance) ||
                    island.distance < island.base + 20000 || island.distance >= island.base + 120000) continue;
                if (best == count || island.distance < islands[best].distance) best = i;
            }
            if (best == count) break;
            islands[best].wanted = true;
            --remaining;
        }
    }
    void allocationFailed() { if (limit) --limit; }
};
#if defined(__PROSPERO__)
class daLodbg_c;
void pc_distant_islands_init();
void pc_distant_islands_attach(daLodbg_c* actor);
void pc_distant_islands_detach(daLodbg_c* actor);
void pc_distant_islands_update();
void pc_distant_islands_begin_load(const daLodbg_c* actor,float distance);
bool pc_distant_islands_extra(const daLodbg_c* actor);
void pc_distant_islands_allocation_failed();
#endif
