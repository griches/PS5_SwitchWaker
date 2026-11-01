#include "pc/pc_quick_doors.h"
#if defined(__PROSPERO__)
#include "pc/pc_gameplay_options.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_overlap_mng.h"
#include "m_Do/m_Do_graphic.h"
namespace {
PcQuickDoorWalk walk;
bool arrived = false;
const char* eventName() {
    dEvt_control_c* event = g_dComIfG_gameInfo.play.getEvent();
    if (!event->mMode) return nullptr;
    dEvDtEvent_c* data = g_dComIfG_gameInfo.play.getEvtManager().getEventData(event->mEventId);
    return data ? data->getName() : nullptr;
}
bool covered() {
    return mDoGph_gInf_c::isFade() && mDoGph_gInf_c::mFadeSpeed > 0.0f &&
        mDoGph_gInf_c::getFadeRate() >= 1.0f && mDoGph_gInf_c::getFadeColor().a == 255;
}
}
void pc_quick_doors_begin() {
    pc_gameplay_begin_action(PC_GAMEPLAY_QUICK_DOORS);
    walk.begin(pc_gameplay_enabled(PC_GAMEPLAY_QUICK_DOORS));
}
void pc_quick_doors_speed_walk(float& under, float& upper) {
    walk.speed(covered(), eventName(), under, upper);
}
bool pc_quick_doors_cut_wait() { return walk.cutWait(covered(), eventName()); }
bool pc_quick_doors_covered() { return walk.enabled && walk.sped && covered(); }
void pc_quick_doors_arrive(const char* actor, int point, uint32_t& params, int16_t yaw, float& x, float& z) {
    arrived = pc_quick_door_arrival(pc_gameplay_enabled(PC_GAMEPLAY_QUICK_DOORS),
        fopOvlpM_IsDoingReq(), actor, point, params, yaw, x, z);
}
bool pc_quick_doors_take_arrival() { const bool result = arrived; arrived = false; return result; }
void pc_quick_doors_clear_arrival() { arrived = false; }
#else
void pc_quick_doors_begin() {}
void pc_quick_doors_speed_walk(float&, float&) {}
bool pc_quick_doors_cut_wait() { return false; }
bool pc_quick_doors_covered() { return false; }
void pc_quick_doors_arrive(const char*, int, uint32_t&, int16_t, float&, float&) {}
bool pc_quick_doors_take_arrival() { return false; }
void pc_quick_doors_clear_arrival() {}
#endif
