// Player-facing platform visibility. Retain registry entries for saved-key compatibility.
#ifndef PC_MENU_POLICY_H
#define PC_MENU_POLICY_H
#include "pc/pc_settings.h"
#include <cstring>
#include "pc/pc_setting_presentation.h"
inline bool pc_menu_setting_visible(const PcSettingDesc* setting) {
    if (!setting) return false;
#if defined(__PROSPERO__)
    if (setting->flags & PC_SETTING_SWITCH_ONLY) return false;
    const auto* entry = pc_setting_presentation_for(setting->key);
    if (entry && entry->hidden) return false;
#endif
    return true;
}
#endif
