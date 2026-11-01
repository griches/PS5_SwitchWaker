#include "pc/pc_fast_load.h"
#if defined(__PROSPERO__)
#include "pc/pc_gameplay_options.h"
#include "pc/pc_quick_doors.h"
#include "f_op/f_op_overlap_mng.h"
#include "JSystem/JFramework/JFWDisplay.h"
#include <aurora/aurora.h>
#include <dolphin/vi.h>
namespace pc { bool menuOpen(); }
namespace { PcFastLoadState state; }
void pc_fast_load_update() {
    JFWDisplay* display = JFWDisplay::getManager();
    JUTFader* fader = display ? display->getFader() : nullptr;
    const bool hidden = (fopOvlpM_IsDoingReq() && fader &&
        fader->getStatus() == JUTFader::WaitOut && fader->mColor.a == 255) ||
        pc_quick_doors_covered();
    state.update(pc_gameplay_enabled(PC_GAMEPLAY_FAST_LOADING), hidden, pc::menuOpen(), VIGetRetraceCount());
    // Aurora snapshots this per frame, before work is handed to either worker.
    aurora_set_present_suppressed(state.active);
}
bool pc_fast_load_active() { return state.active; }
#else
void pc_fast_load_update() {}
bool pc_fast_load_active() { return false; }
#endif
