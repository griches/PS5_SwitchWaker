#include "pc/pc_ps5_features.h"
#include "pc/pc_gameplay_options.h"
#include "pc/pc_camera_controls.h"
#include "pc/pc_distant_islands.h"
#include "pc/pc_haptics.h"
#include "pc/pc_hd_visuals.h"
void pc_ps5_features_init() {
    pc_gameplay_options_init();
    pc_camera_controls_init();
    pc_distant_islands_init();
    pc_haptics_init();
    pc_hd_visuals_init();
}
