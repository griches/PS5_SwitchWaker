// PS5 presentation only: grouping never changes a setting's key, value or callback.
#ifndef PC_GAMEPLAY_MENU_H
#define PC_GAMEPLAY_MENU_H
#include <cstring>
#include "pc/pc_setting_presentation.h"
inline PcGameplayMenuGroup pc_gameplay_menu_group(const char* key) {
    const auto* entry = pc_setting_presentation_for(key);
    return entry ? entry->group : PC_GAMEPLAY_MENU_MOVEMENT;
}
#endif
