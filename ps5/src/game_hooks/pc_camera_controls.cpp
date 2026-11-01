// R3 requests native first-person actions, without synthesizing stick movement.
#if defined(__PROSPERO__)
#include "pc/pc_camera_controls.h"
#include "d/d_camera.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter.h"
#include "d/actor/d_a_player_main.h"
namespace pc { bool menuOpen(); }
bool dCamera_c::ps5CameraControlled() {
    return mPadId == 0 && mpPlayerActor && mpPlayerActor == dComIfGp_getLinkPlayer() &&
        mCurStyle >= 0 && mCurStyle < dCamParam_c::style_num &&
        !dComIfGp_event_runCheck() && !dComIfGp_evmng_cameraPlay() && !dMenu_flag() &&
        !pc::menuOpen() && !chkFlag(0x800000) &&
        static_cast<daPy_py_c*>(mpPlayerActor)->mDemo.getDemoMode() == 0;
}
void dCamera_c::ps5CameraPad() {
    if (!ps5CameraControlled() ||
        !pc_camera_controls_input().pressed) return;
    const bool first = mCamParam.Algorythmn(mCurStyle) == dCamAlg_SUBJECT_CAMERA_e &&
        dCamParam_c::styles[mCurStyle].m00 == 0x53533031u; // SS01, not an item view
    if (first) {
        setComStat(dCamAttnStts_00002000_e);
        return;
    }
    // Include the native C-stick orbit mode (12), so L2 reset is unnecessary.
    // Retain the native entry restrictions for movement, swimming and targeting.
    if ((mCurMode != 0 && mCurMode != 12 && mCurMode != 0x13) ||
        dComIfGp_getAttention().Lockon() || dComIfGp_getAttention().LockonTruth() ||
        mStickMainValueLast >= 0.5f ||
        dComIfGp_checkPlayerStatus0(mPadId, daPyStts0_SWIM_e | daPyStts0_CRAWL_e)) return;
    m144 = 1;
    m184 = 1;
    setComStat(dCamAttnStts_00001000_e);
    setComStat(dCamAttnStts_00000400_e);
}
#endif
