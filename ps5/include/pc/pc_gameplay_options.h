/* Better Wind Waker gameplay options for the PS5 native port.
 * Behavior references: BlueWake mods/betterww/options.txt and game_options.c,
 * based on Better Wind Waker (WideBoner/betterww, revision 4501481).
 */
#ifndef PC_GAMEPLAY_OPTIONS_H
#define PC_GAMEPLAY_OPTIONS_H

#include <stdint.h>

enum PcGameplayOption {
    PC_GAMEPLAY_SWING_TURN,
    PC_GAMEPLAY_NO_SONG_REPLAYS,
    PC_GAMEPLAY_SEA_COMPASS,
    PC_GAMEPLAY_REVEAL_SEA_CHART,
    PC_GAMEPLAY_UNRESTRICTED_BOAT,
    PC_GAMEPLAY_FASTER_BLOCK_PUSH,
    PC_GAMEPLAY_FASTER_MOVEMENT,
    PC_GAMEPLAY_FASTER_GRAPPLE,
    PC_GAMEPLAY_FASTER_TRAVERSAL,
    PC_GAMEPLAY_TINGLE_CHESTS,
    PC_GAMEPLAY_FASTER_BALLAD,
    PC_GAMEPLAY_SKIP_INTRO,
    PC_GAMEPLAY_INSTANT_TEXT,
    PC_GAMEPLAY_KEEP_WATERED_TREES,
    PC_GAMEPLAY_FOREST_WATER_30_MINUTES,
    PC_GAMEPLAY_DPAD_MENUS,
    PC_GAMEPLAY_SHORT_FADES,
    PC_GAMEPLAY_JUMP_BUTTON,
    PC_GAMEPLAY_SPRINT,
    PC_GAMEPLAY_QUICK_DOORS,
    PC_GAMEPLAY_FAST_LOADING,
    PC_GAMEPLAY_OPTION_COUNT
};

inline int pc_gameplay_forest_water_frames(int original, bool longer) {
    return longer ? 54000 : original; // 30 minutes at the original 30 Hz logic rate.
}

// Returns whether D-pad directions were consumed. Analog input takes priority.
inline bool pc_gameplay_menu_direction(bool eligible, bool left, bool right,
                                      bool down, bool up, float& x, float& y) {
    if (!eligible || !(left || right || down || up)) return false;
    if (x > 0.2f || x < -0.2f || y > 0.2f || y < -0.2f) return true;
    const int horizontal = int(right) - int(left);
    const int vertical = int(up) - int(down);
    const float magnitude = horizontal && vertical ? 0.70710678f : 1.0f;
    x = horizontal * magnitude;
    y = vertical * magnitude;
    return true;
}
inline int pc_gameplay_fade_frames(bool shorter) { return shorter ? 6 : 26; }

float pc_gameplay_sprint_multiplier();
void pc_gameplay_options_init();
bool pc_gameplay_enabled(PcGameplayOption option);
// Menu choices are requested immediately; action owners adopt deferred choices here.
void pc_gameplay_begin_action(PcGameplayOption option);
void pc_gameplay_begin_sailing();
enum PcTextSystem { PC_TEXT_NORMAL, PC_TEXT_DEMO };
void pc_gameplay_begin_text(PcTextSystem system);
bool pc_gameplay_text_enabled(PcTextSystem system);

enum PcSailMode { PC_SAIL_ORIGINAL, PC_SAIL_SWIFT, PC_SAIL_BRISK };
PcSailMode pc_gameplay_sail_mode();

struct PcSailTuning {
    float topSpeed, initialSpeed, acceleration, accelerationStep;
    float firstBrakeMax, firstBrakeMin, cruiseSpeed;
    float stopMax, stopMin, idleMax, idleMin;
};
inline PcSailTuning pc_gameplay_sail_tuning(PcSailMode mode) {
    if (mode == PC_SAIL_SWIFT)
        return {110.0f, 160.0f, 0.2f, 0.03f, 10.0f, 2.0f, 30.0f, 2.0f, 0.2f, 2.0f, 0.1f};
    if (mode == PC_SAIL_BRISK)
        return {110.0f, 160.0f, 0.2f, 0.03f, 10.0f, 2.0f, 30.0f, 3.0f, 0.2f, 4.0f, 0.2f};
    return {55.0f, 80.0f, 0.1f, 0.015f, 5.0f, 1.0f, 10.0f, 1.0f, 0.1f, 1.0f, 0.05f};
}
// Keep steering response equal at the same fraction of each sail's top speed.
inline float pc_gameplay_sail_turn_rate(float speed, PcSailMode mode) {
    const float magnitude = speed < 0.0f ? -speed : speed;
    return 4.0f - magnitude / pc_gameplay_sail_tuning(mode).topSpeed * 3.0f;
}
inline int16_t pc_gameplay_sail_wind(int16_t heading) {
    const unsigned angle = (-static_cast<int>(heading) + 0x5000) & 0xe000u;
    return static_cast<int16_t>(angle < 0x8000u ? static_cast<int>(angle)
                                             : static_cast<int>(angle) - 0x10000);
}
// Only the three story paths are bypassed; the outer world boundary remains.
inline bool pc_gameplay_skip_boat_path(bool sea, int pathIndex, bool unrestricted) {
    return unrestricted && sea && pathIndex >= 0 && pathIndex < 3;
}

// Sample at push/pull initialization so duration and its sine phase agree.
inline int pc_gameplay_block_frames(int originalFrames, bool faster) {
    return faster ? 12 : originalFrames;
}
inline float pc_gameplay_block_animation(float originalRate, bool faster) {
    return faster ? 1.4f : originalRate;
}

struct PcRollTuning { float multiplier, base; };
inline PcRollTuning pc_gameplay_roll_tuning(float multiplier, float base, bool faster) {
    return faster ? PcRollTuning{0.3529412f, 20.0f} : PcRollTuning{multiplier, base};
}
inline float pc_gameplay_crawl_speed(float original, bool faster) {
    return faster ? 6.0f : original;
}
struct PcGrappleTuning {
    int firstPersonFrames, thirdPersonFrames, wrapCounterStep;
    float throwSpeed, dropSpeed, wrapScale;
};
inline PcGrappleTuning pc_gameplay_grapple_tuning(bool faster) {
    return faster ? PcGrappleTuning{20, 10, 6, 40.0f, 20.0f, 25.0f}
                  : PcGrappleTuning{40, 20, 1, 20.0f, 10.0f, 17.0f};
}

enum PcTraversalAction {
    PC_TRAVERSAL_LEDGE_START,
    PC_TRAVERSAL_LADDER_START,
    PC_TRAVERSAL_LADDER_MOVE,
    PC_TRAVERSAL_LADDER_END,
    PC_TRAVERSAL_SIDLE
};
inline float pc_gameplay_traversal_rate(float original, PcTraversalAction action, bool faster) {
    if (!faster) return original;
    switch (action) {
    case PC_TRAVERSAL_LEDGE_START:
    case PC_TRAVERSAL_LADDER_START:
    case PC_TRAVERSAL_LADDER_MOVE: return 1.6f;
    case PC_TRAVERSAL_LADDER_END: return 1.4f;
    case PC_TRAVERSAL_SIDLE: return 2.0f;
    }
    return original;
}
inline int pc_gameplay_talk_zoom_frames(int original, bool faster) {
    return faster ? 10 : original;
}

// Pure calculations shared with the focused host checks.
inline int16_t pc_gameplay_swing_heading(int16_t heading, float stick) {
    // Reject invalid input before float-to-int conversion; clamp controller range.
    if (!(stick >= -1.0f && stick <= 1.0f)) return heading;
    const int velocity = static_cast<int>(512.0f * stick);
    if (velocity > -128 && velocity < 128) return heading;
    const unsigned wrapped = (static_cast<unsigned>(heading) - velocity) & 0xffffu;
    return static_cast<int16_t>(wrapped < 0x8000u ? static_cast<int>(wrapped)
                                               : static_cast<int>(wrapped) - 0x10000);
}

inline float pc_gameplay_compass_degrees(int16_t heading, bool corrected) {
    const int angle = corrected ? heading : -static_cast<int>(heading);
    return (angle * 180.0f) / 0x8000;
}

#endif
