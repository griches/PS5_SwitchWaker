#include "pc/pc_camera_controls.h"
#include <cassert>
namespace { bool blocked; int rightAim; }
namespace pc { bool menuOpen() { return blocked; } }
extern "C" void pc_aim_right_stick_set(int on) { rightAim = on; }
int main() {
    pc_camera_controls_init(); pc_camera_controls_init();
    assert(rightAim == 1);
    pc_camera_controls_sample(true, true); // held at startup is not a press
    assert(!pc_camera_controls_input().pressed);
    pc_camera_controls_sample(true, false);
    pc_camera_controls_sample(true, true);
    assert(pc_camera_controls_input().pressed);
    pc_camera_controls_sample(true, true);
    assert(!pc_camera_controls_input().pressed);
    blocked = true; pc_camera_controls_sample(true, false);
    blocked = false; pc_camera_controls_sample(true, true);
    assert(!pc_camera_controls_input().pressed);
    pc_camera_controls_sample(true, false);
    pc_camera_controls_sample(true, true);
    assert(pc_camera_controls_input().pressed);
    pc_camera_controls_sample(false, false);
    pc_camera_controls_sample(true, true);
    assert(!pc_camera_controls_input().pressed);
}
