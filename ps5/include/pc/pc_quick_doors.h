// Native adaptation of Bluewake's covered-screen knob-door shortcuts.
#ifndef PC_QUICK_DOORS_H
#define PC_QUICK_DOORS_H
#include <cmath>
#include <cstdint>
#include <cstring>

inline bool pc_quick_door_event(const char* name) {
    return name && (std::strcmp(name, "DEFAULT_KNOB_DOOR_F_OPEN") == 0 ||
                    std::strcmp(name, "DEFAULT_KNOB_DOOR_B_OPEN") == 0);
}
struct PcQuickDoorWalk {
    bool enabled = false, sped = false;
    void begin(bool on) { enabled = on; sped = false; }
    bool speed(bool covered, const char* event, float& under, float& upper) {
        if (!enabled || sped || !covered || !pc_quick_door_event(event) ||
            !(under > 0.5f && under < 16.0f)) return false;
        under *= 16.0f;
        if (upper > 0.5f && upper < 16.0f) upper *= 16.0f;
        sped = true;
        return true;
    }
    bool cutWait(bool covered, const char* event) const {
        return enabled && sped && covered && pc_quick_door_event(event);
    }
};
inline bool pc_quick_door_arrival(bool enabled, bool overlap, const char* actor, int point,
                                   uint32_t& params, int16_t yaw, float& x, float& z) {
    const unsigned mode = (params >> 12) & 15u;
    if (!enabled || !overlap || !actor || std::strcmp(actor, "Link") != 0 || point < 0 ||
        (mode != 10u && mode != 11u)) return false;
    const float distance = mode == 10u ? 75.0f : 84.0f;
    const float angle = float(yaw) * (3.14159265f / 32768.0f);
    x += distance * std::sin(angle);
    z += distance * std::cos(angle);
    // Ordinary start, no start-point event; preserve all other parameter bits.
    params = (params & 0x00ff0fffu) | 0xff000000u;
    return true;
}
void pc_quick_doors_begin();
void pc_quick_doors_speed_walk(float& under, float& upper);
bool pc_quick_doors_cut_wait();
bool pc_quick_doors_covered();
void pc_quick_doors_arrive(const char* actor, int point, uint32_t& params, int16_t yaw, float& x, float& z);
bool pc_quick_doors_take_arrival();
void pc_quick_doors_clear_arrival();
#endif
