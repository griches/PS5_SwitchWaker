#include "pc/pc_camera_controls.h"
#if defined(__PROSPERO__)
#include "pc/pc_controls.h"
namespace pc { bool menuOpen(); }
namespace {
PcCameraInput input;
bool initialized;
}
void pc_camera_controls_init() {
    if (initialized) return;
    initialized = true;
    // Keep native stick-down exit disabled while the right stick controls aiming.
    pc_aim_right_stick_set(1);
}
void pc_camera_controls_sample(bool present, bool click) {
    pc_camera_controls_init();
    input.sample(present, pc::menuOpen(), click);
}
const PcCameraInput& pc_camera_controls_input() { return input; }
#endif
