// Exercise the actual PS5 SDL adapter against controllable input and VI retraces.
#include "pc/pc_player_controls.h"
#include "pc/pc_gameplay_options.h"
#include <SDL3/SDL_gamepad.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>
#include <cassert>
#include <initializer_list>

namespace {
bool present = true, blocked, jumpHeld, sprintHeld;
bool gameplayBlocked, captured;
int assignedIndex = 2;
bool jumpEnabled = true, sprintEnabled = true;
float multiplier = 1.5f;
Sint16 stickX = 32767, stickY;
u32 retrace;
SDL_JoystickID controller = 1;
SDL_Gamepad* fakePad = reinterpret_cast<SDL_Gamepad*>(1);
}
namespace pc { bool menuOpen() { return blocked; } }
bool pc_player_controls_blocked() { return gameplayBlocked; }
extern "C" s32 PADGetIndexForPort(u32 port) { assert(port == 0); return present ? assignedIndex : -1; }
extern "C" BOOL PADIsInputCaptured(u32 port) { assert(port == 0); return captured; }
extern "C" SDL_Gamepad* PADGetSDLGamepadForIndex(u32 index) { assert(index == static_cast<u32>(assignedIndex)); return present ? fakePad : nullptr; }
extern "C" SDL_JoystickID SDLCALL SDL_GetGamepadID(SDL_Gamepad* pad) { assert(pad == fakePad); return controller; }
extern "C" bool SDLCALL SDL_GamepadConnected(SDL_Gamepad* pad) { assert(pad == fakePad); return present; }
extern "C" bool SDLCALL SDL_GetGamepadButton(SDL_Gamepad* pad, SDL_GamepadButton button) {
    assert(pad == fakePad);
    if (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) return jumpHeld;
    assert(button == SDL_GAMEPAD_BUTTON_LEFT_STICK);
    return sprintHeld;
}
extern "C" Sint16 SDLCALL SDL_GetGamepadAxis(SDL_Gamepad* pad, SDL_GamepadAxis axis) {
    assert(pad == fakePad);
    assert(axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY);
    return axis == SDL_GAMEPAD_AXIS_LEFTX ? stickX : stickY;
}
extern "C" u32 VIGetRetraceCount() { return retrace; }
bool pc_gameplay_enabled(PcGameplayOption option) {
    return option == PC_GAMEPLAY_JUMP_BUTTON ? jumpEnabled : option == PC_GAMEPLAY_SPRINT && sprintEnabled;
}
float pc_gameplay_sprint_multiplier() { return multiplier; }
static PcMovementDecision frame(bool jumpEligible = true, unsigned elapsed = 1) {
    retrace += elapsed;
    pc_player_controls_poll();
    return pc_player_controls_take(jumpEligible);
}
static void clickSprint() {
    sprintHeld = false; frame();
    sprintHeld = true; frame();
}
int main() {
    assert(pc_player_controls_run_tuning(17.0f) == 17.0f);
    assert(!frame().jump);
    jumpHeld = true; assert(frame().jump);
    assert(!frame().jump);
    assert(!pc_player_controls_take(true).jump); // consumed once
    jumpHeld = false; frame();
    jumpHeld = true; assert(!frame(false).jump);
    assert(!frame().jump); // no midair/event/targeting press buffering
    jumpHeld = false; frame();
    blocked = true; jumpHeld = true; assert(!frame().jump);
    blocked = false; assert(!frame().jump);
    jumpHeld = false; frame();
    jumpHeld = true; jumpEnabled = false; assert(!frame().jump);
    jumpEnabled = true; assert(!frame().jump);
    jumpHeld = false; frame();

    clickSprint(); assert(pc_player_controls_sprinting());
    assert(pc_player_controls_run_tuning(17.0f) == 25.5f);
    assert(pc_player_controls_run_tuning(2.3f) == 2.3f * multiplier);
    assert(frame().sprint); // held click does not toggle repeatedly
    clickSprint(); assert(!pc_player_controls_sprinting());
    assert(pc_player_controls_run_tuning(17.0f) == 17.0f);

    clickSprint();
    jumpHeld = true;
    auto decision = frame(); assert(decision.jump && decision.sprint);
    assert(frame(false).sprint); // jumping and other player procedures retain the latch
    jumpHeld = false;
    // Exactly eight VI retraces, independent of 30/60 Hz input polling.
    stickX = 0;
    for (int i = 0; i < 7; ++i) assert(frame().sprint);
    assert(!frame().sprint);
    stickX = 32767; clickSprint(); stickX = 0;
    for (int i = 0; i < 3; ++i) assert(frame(true, 2).sprint);
    assert(!frame(true, 2).sprint);

    // Quarter-tilt boundary and radial diagonal magnitude; idle must be continuous.
    stickX = 32767; clickSprint(); stickX = 8191;
    for (int i = 0; i < 7; ++i) assert(frame().sprint);
    stickX = 8192; assert(frame().sprint); // >=25% resets idle
    stickX = 0;
    for (int i = 0; i < 7; ++i) assert(frame().sprint);
    stickX = stickY = 6000; assert(frame().sprint); // radial >25%, axes each <25%
    stickX = stickY = 0;
    for (int i = 0; i < 7; ++i) assert(frame().sprint);
    assert(!frame().sprint);

    // Counter wrap and repeated sampling in the same retrace.
    stickX = 32767; clickSprint();
    retrace = 0xfffffffc; frame(); stickX = 0;
    for (int i = 0; i < 7; ++i) assert(frame().sprint);
    assert(frame(true, 0).sprint);
    assert(!frame().sprint);

    stickX = 32767; clickSprint();
    for (float factor : {1.1f, 1.37f, 1.5f, 2.0f}) {
        multiplier = factor;
        assert(frame().sprint);
        assert(pc_player_controls_run_tuning(17.0f) == 17.0f * factor);
        assert(pc_player_controls_run_tuning(2.3f) == 2.3f * factor);
    }
    blocked = true; assert(!frame().sprint);
    blocked = false; assert(!frame().sprint); // PS5 menu return suppresses held controls
    clickSprint(); sprintEnabled = false; assert(!frame().sprint);
    sprintEnabled = true; assert(!frame().sprint);

    clickSprint(); present = false; assert(!frame().sprint);
    present = true; jumpHeld = true; sprintHeld = true;
    decision = frame(); assert(!decision.jump && !decision.sprint);
    jumpHeld = sprintHeld = false; frame();
    clickSprint(); ++controller; jumpHeld = true;
    decision = frame(); assert(!decision.jump && !decision.sprint);
    jumpHeld = sprintHeld = false; frame();
    clickSprint(); gameplayBlocked = true;
    assert(pc_player_controls_run_tuning(17.0f) == 17.0f);
    assert(!frame().sprint);
    gameplayBlocked = false;
    assert(!frame().sprint);
    clickSprint(); captured = true; assert(!frame().sprint);
    captured = false; assert(!frame().sprint);
    pc_player_controls_reset(); jumpHeld = sprintHeld = true;
    decision = frame(); assert(!decision.jump && !decision.sprint);
    assignedIndex = 3; ++controller;
    decision = frame(); assert(!decision.jump && !decision.sprint);
}
