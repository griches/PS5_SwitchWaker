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
    SDL_Gamepad* pad = PADGetSDLGamepadForIndex(0);
    const SDL_JoystickID id = pad ? SDL_GetGamepadID(pad) : 0;
    if (id != controller) { input = {}; controller = id; }
    const float x = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f : 0.0f;
    const float y = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f : 0.0f;
    input.sample(pad && SDL_GamepadConnected(pad), pc::menuOpen(),
        pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),
        pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK),
        x*x + y*y, VIGetRetraceCount(), pc_gameplay_enabled(PC_GAMEPLAY_SPRINT));
}
PcMovementDecision pc_player_controls_take(bool jumpEligible) {
    return input.take(jumpEligible, pc_gameplay_enabled(PC_GAMEPLAY_JUMP_BUTTON));
}
bool pc_player_controls_sprinting() { return input.sprinting; }
float pc_player_controls_run_tuning(float original) {
    return original * (input.sprinting ? pc_gameplay_sprint_multiplier() : 1.0f);
}
#else
void pc_player_controls_poll() {}
PcMovementDecision pc_player_controls_take(bool) { return {false, false}; }
bool pc_player_controls_sprinting() { return false; }
float pc_player_controls_run_tuning(float original) { return original; }
#endif
