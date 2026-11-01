#include "pc/pc_distant_islands.h"
#if defined(__PROSPERO__)
#include "pc/pc_settings.h"
#include "d/actor/d_a_lod_bg.h"
#include "d/d_com_inf_game.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JKernel/JKRSolidHeap.h"
#include <cstring>
#include <dolphin/vi.h>
namespace {
struct Entry { daLodbg_c* actor; float base; bool extended; bool optionalLoad; };
Entry entries[64]{};
size_t count;
PcIslandBudget budget;
bool enabled, initialized, frameValid;
u32 lastRetrace;
void apply(const char*, const char* value, void*) { enabled = std::strcmp(value, "1") == 0; frameValid = false; }
const PcSettingChoice toggle[] = {{"0", "Off"}, {"1", "On"}};
const PcSettingDesc setting = {
    "COS_HD_DISTANT_ISLANDS", "Distant islands", "Shows islands farther away. Detail and fades update while sailing.",
    PC_SETTING_TAB_GRAPHICS, 0, toggle, 2, "0", apply, nullptr, 65};
}
void pc_distant_islands_init() {
    if (initialized) return;
    initialized = true;
    pc_settings_register(&setting);
    apply(setting.key, pc_settings_get(setting.key), nullptr);
}
void pc_distant_islands_attach(daLodbg_c* actor) {
    frameValid = false;
    if (count == 0) budget = {};
    if (count < 64) entries[count++] = {actor, actor->scale.x, false, false};
}
void pc_distant_islands_detach(daLodbg_c* actor) {
    frameValid = false;
    for (size_t i=0; i<count; ++i) if (entries[i].actor == actor) {
        entries[i] = entries[--count];
        break;
    }
}
void pc_distant_islands_update() {
    pc_distant_islands_init();
    const u32 retrace = VIGetRetraceCount();
    if (frameValid && lastRetrace == retrace) return;
    lastRetrace = retrace;
    frameValid = true;
    const auto* player = dComIfGp_getPlayer(0);
    PcIslandCandidate candidates[64]{};
    for (size_t i=0; i<count; ++i) {
        const auto& entry = entries[i];
        const auto* actor = entry.actor;
        const float dx = player ? actor->current.pos.x - player->current.pos.x : 0;
        const float dz = player ? actor->current.pos.z - player->current.pos.z : 0;
        const bool loaded = actor->mModelData != nullptr;
        const bool spilled = loaded && ((actor->mDataHeap && actor->mDataHeap->getParent() != daLodbg_c::sLocalHeap) ||
            (actor->mDataHeap2 && actor->mDataHeap2->getParent() != daLodbg_c::sLocalHeap));
        candidates[i] = {entry.base, std::sqrt(dx*dx+dz*dz), entry.extended, loaded, spilled, false};
    }
    budget.select(candidates, count, enabled, player != nullptr);
    for (size_t i=0; i<count; ++i) {
        auto& entry = entries[i];
        entry.extended = candidates[i].wanted;
        entry.actor->scale.x = entry.base + (entry.extended ? 100000 : 0);
        entry.actor->scale.z = entry.actor->scale.x + 20000;
    }
}
void pc_distant_islands_begin_load(const daLodbg_c* actor,float distance) {
    for(size_t i=0;i<count;++i) if(entries[i].actor==actor) {
        entries[i].optionalLoad=pc_island_optional_request(entries[i].base,distance);
        return;
    }
}
bool pc_distant_islands_extra(const daLodbg_c* actor) {
    for(size_t i=0;i<count;++i) if(entries[i].actor==actor) return entries[i].optionalLoad;
    return false;
}
void pc_distant_islands_allocation_failed() { budget.allocationFailed(); frameValid = false; }
#endif
