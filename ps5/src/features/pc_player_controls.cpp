#include "pc/pc_player_controls.h"
#include "pc/pc_gameplay_options.h"

#if defined(__PROSPERO__)
#include <SDL3/SDL_gamepad.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>

// The native menu pauses the game and suppresses held buttons on return.
namespace pc { bool menuOpen(); }
namespace {
PcMovementInput input;
SDL_JoystickID controller;
}
void pc_player_controls_poll() {
    const int index = PADGetIndexForPort(0);
    SDL_Gamepad* pad = index >= 0 ? PADGetSDLGamepadForIndex(index) : nullptr;
    const SDL_JoystickID id = pad ? SDL_GetGamepadID(pad) : 0;
    if (id != controller) { input = {}; controller = id; }
    const float x = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f : 0.0f;
    const float y = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f : 0.0f;
    input.sample(pad && SDL_GamepadConnected(pad),
        pc::menuOpen() || PADIsInputCaptured(0) || pc_player_controls_blocked(),
        pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),
        pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK),
        x*x + y*y, VIGetRetraceCount(), pc_gameplay_enabled(PC_GAMEPLAY_SPRINT));
}
PcMovementDecision pc_player_controls_take(bool jumpEligible) {
    if (pc_player_controls_blocked()) { input.jumpPressed = false; input.sprinting = false; }
    return input.take(jumpEligible, pc_gameplay_enabled(PC_GAMEPLAY_JUMP_BUTTON));
}
void pc_player_controls_reset() { input = {}; }
bool pc_player_controls_sprinting() { return input.sprinting && !pc_player_controls_blocked(); }
float pc_player_controls_run_tuning(float original) {
    return original * (pc_player_controls_sprinting() ? pc_gameplay_sprint_multiplier() : 1.0f);
}
#else
void pc_player_controls_poll() {}
void pc_player_controls_reset() {}
bool pc_player_controls_blocked() { return true; }
PcMovementDecision pc_player_controls_take(bool) { return {false, false}; }
bool pc_player_controls_sprinting() { return false; }
float pc_player_controls_run_tuning(float original) { return original; }
#endif
