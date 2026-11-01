// R3 button edge; subject-view aiming and zoom are handled by the camera.
#ifndef PC_CAMERA_CONTROLS_H
#define PC_CAMERA_CONTROLS_H
struct PcCameraInput {
    bool ready = false, click = false, pressed = false;
    void sample(bool present, bool blocked, bool r3) {
        pressed = present && !blocked && ready && r3 && !click;
        ready = present && !blocked;
        click = r3;
    }
};
void pc_camera_controls_init();
void pc_camera_controls_sample(bool present, bool click);
const PcCameraInput& pc_camera_controls_input();
#endif
