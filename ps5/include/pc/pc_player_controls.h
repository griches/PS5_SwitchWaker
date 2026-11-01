// PS5 controller adaptation of Bluewake's jump and sprint behavior.
#ifndef PC_PLAYER_CONTROLS_H
#define PC_PLAYER_CONTROLS_H
#include <stdint.h>
struct PcMovementDecision { bool jump; bool sprint; };

struct PcMovementInput {
    bool connected = false;
    bool previousJump = false, previousSprint = false;
    bool jumpPressed = false, sprinting = false;
    uint32_t lastRetrace = 0, idleRetraces = 0;

    void sample(bool present, bool blocked, bool jump, bool sprint, float tiltSquared,
                uint32_t retrace, bool sprintEnabled) {
        jumpPressed = false;
        if (!present) { *this = {}; return; }
        const uint32_t elapsed = connected ? retrace - lastRetrace : 0;
        if (connected && !blocked) {
            jumpPressed = jump && !previousJump;
            if (sprint && !previousSprint && sprintEnabled) {
                sprinting = !sprinting;
                idleRetraces = 0;
            }
        }
        connected = true;
        lastRetrace = retrace;
        previousJump = jump;
        previousSprint = sprint;
        if (blocked || !sprintEnabled) { sprinting = false; idleRetraces = 0; }
        if (sprinting) {
            // Bluewake: eight retraces below one quarter of full stick tilt.
            if (tiltSquared < 0.25f * 0.25f) {
                idleRetraces += elapsed < 8 ? elapsed : 8;
                if (idleRetraces >= 8) sprinting = false;
            } else idleRetraces = 0;
        }
    }
    PcMovementDecision take(bool jumpEligible, bool jumpEnabled) {
        const bool jump = jumpEligible && jumpEnabled && jumpPressed;
        jumpPressed = false;
        // Sprint is a controller latch, independent of jumping or the current player action.
        return {jump, sprinting};
    }
};
void pc_player_controls_poll();
PcMovementDecision pc_player_controls_take(bool jumpEligible);
bool pc_player_controls_sprinting();
float pc_player_controls_run_tuning(float original);
#endif
