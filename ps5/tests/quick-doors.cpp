#include "pc/pc_quick_doors.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>

int main() {
    PcQuickDoorWalk walk;
    float under = 1, upper = 1;
    constexpr const char* front = "DEFAULT_KNOB_DOOR_F_OPEN";
    constexpr const char* back = "DEFAULT_KNOB_DOOR_B_OPEN";
    assert(!walk.speed(true, front, under, upper)); // Off by default.
    walk.begin(true);
    assert(!walk.speed(false, front, under, upper)); // Visible opening stays at normal speed.
    for (const char* event : {static_cast<const char*>(nullptr), "", "DEFAULT_SHUTTER_DOOR", "DEFAULT_KNOB_DOOR_F_OPEN_EXTRA"})
        assert(!walk.speed(true, event, under, upper));
    assert(under == 1 && upper == 1 && !walk.cutWait(true, front));
    assert(walk.speed(true, front, under, upper));
    assert(under == 16 && upper == 16 && walk.cutWait(true, front));
    assert(!walk.speed(true, front, under, upper) && under == 16); // Once per action.
    assert(!walk.cutWait(false, front) && !walk.cutWait(true, "LOCKED_DOOR"));
    walk.begin(true); under = 0.75f; upper = 0;
    assert(walk.speed(true, back, under, upper) && under == 12 && upper == 0);
    for (float rate : {0.0f, 0.5f, 16.0f, std::numeric_limits<float>::quiet_NaN()}) {
        walk.begin(true); under = rate; upper = 1;
        assert(!walk.speed(true, front, under, upper) && upper == 1);
    }
    walk.begin(false); under = upper = 1;
    assert(!walk.speed(true, front, under, upper) && !walk.cutWait(true, front));
    // Only actual stage starts for Link, during a scene overlap, with knob modes A/B.
    for (bool enabled : {false, true}) for (bool overlap : {false, true})
    for (const char* actor : {static_cast<const char*>(nullptr), "Link", "Tetra"}) for (int point : {-3, -2, -1, 0, 5})
    for (unsigned mode = 0; mode < 16; ++mode) {
        const uint32_t original = 0x125a0000u | (mode << 12) | 0x789u;
        uint32_t params = original;
        float x = 10, z = -20;
        const bool expected = enabled && overlap && actor && std::strcmp(actor, "Link") == 0 &&
            point >= 0 && (mode == 10 || mode == 11);
        assert(pc_quick_door_arrival(enabled, overlap, actor, point, params, 0, x, z) == expected);
        if (expected) {
            assert(x == 10 && z == -20 + (mode == 10 ? 75 : 84));
            assert(params == ((original & 0x00ff0fffu) | 0xff000000u));
            assert((params & 0xff00f000u) == 0xff000000u); // DEFAULT_START, no point event.
        } else assert(params == original && x == 10 && z == -20);
    }
    // Both arrival distances follow the start's heading in all four cardinal directions.
    for (int angle : {0, 0x4000, -0x8000, -0x4000}) for (unsigned mode : {10u, 11u}) {
        uint32_t params = mode << 12;
        float x = 0, z = 0;
        const float distance = mode == 10 ? 75 : 84;
        assert(pc_quick_door_arrival(true, true, "Link", 0, params, int16_t(angle), x, z));
        assert(std::fabs(std::sqrt(x*x+z*z) - distance) < 1e-4f);
        if (angle == 0) assert(std::fabs(x) < 1e-4f && z > 0);
        if (angle == 0x4000) assert(x > 0 && std::fabs(z) < 1e-4f);
        if (angle == -0x8000) assert(std::fabs(x) < 1e-4f && z < 0);
        if (angle == -0x4000) assert(x < 0 && std::fabs(z) < 1e-4f);
    }
}
