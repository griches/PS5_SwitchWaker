#pragma once
#include <cstring>
#include "pc/pc_settings.h"
// PS5 presentation only. Keys, callbacks, defaults and saved values stay feature-owned.
enum PcGameplayMenuGroup {
    PC_GAMEPLAY_MENU_COMFORT,
    PC_GAMEPLAY_MENU_MOVEMENT,
    PC_GAMEPLAY_MENU_CAMERA,
    PC_GAMEPLAY_MENU_SAILING,
    PC_GAMEPLAY_MENU_PACING,
    PC_GAMEPLAY_MENU_TRANSITIONS,
    PC_GAMEPLAY_MENU_EXPLORATION,
    PC_GAMEPLAY_MENU_GROUP_COUNT
};
enum class PcMenuTab { Default = -1, Graphics, FX, Gameplay, Controls, System };
enum class PcMenuSection { None, Display, Textures, Camera, Buttons, Menu, Counter, Enhancements, Vibration, TestActions, Utilities, Logging, PostProcessing };
struct PcSettingPresentation {
    const char* key;
    PcGameplayMenuGroup group;
    bool hidden;
    int order = 0;
    PcMenuTab tab = PcMenuTab::Default;
    PcMenuSection section = PcMenuSection::None;
};
inline constexpr PcSettingPresentation pc_setting_presentation[] = {
    {"COS_MENU_LANGUAGE", PC_GAMEPLAY_MENU_MOVEMENT, false, 590, PcMenuTab::System, PcMenuSection::Menu},
    {"COS_FPS_OVERLAY_POSITION", PC_GAMEPLAY_MENU_MOVEMENT, false, 720, PcMenuTab::System, PcMenuSection::Counter},
    {"COS_FPS_OVERLAY_SIZE", PC_GAMEPLAY_MENU_MOVEMENT, false, 730, PcMenuTab::System, PcMenuSection::Counter},
    {"COS_FPS_OVERLAY_BEHIND_MENU", PC_GAMEPLAY_MENU_MOVEMENT, false, 705, PcMenuTab::System, PcMenuSection::Counter},
    {"COS_FPS_OVERLAY_DETAIL", PC_GAMEPLAY_MENU_MOVEMENT, false, 710, PcMenuTab::System, PcMenuSection::Counter},
    {"COS_FPS_OVERLAY", PC_GAMEPLAY_MENU_MOVEMENT, false, 700, PcMenuTab::System, PcMenuSection::Counter},
    {"COS_HAPTICS_STRENGTH", PC_GAMEPLAY_MENU_MOVEMENT, false, 540, PcMenuTab::Controls, PcMenuSection::Vibration},
    {"COS_HAPTICS", PC_GAMEPLAY_MENU_MOVEMENT, false, 530, PcMenuTab::Controls, PcMenuSection::Vibration},
    {"COS_FPS60", PC_GAMEPLAY_MENU_MOVEMENT, false, 130, PcMenuTab::Graphics, PcMenuSection::Display},
    {"COS_PS5_OUTPUT", PC_GAMEPLAY_MENU_MOVEMENT, false, 100, PcMenuTab::Graphics, PcMenuSection::Display},
    {"COS_FB_SCALE", PC_GAMEPLAY_MENU_MOVEMENT, false, 110, PcMenuTab::Graphics, PcMenuSection::Display},
    {"COS_ASPECT", PC_GAMEPLAY_MENU_MOVEMENT, false, 120, PcMenuTab::Graphics, PcMenuSection::Display},
    {"COS_HD_TEXTURES", PC_GAMEPLAY_MENU_MOVEMENT, false, 200, PcMenuTab::Graphics, PcMenuSection::Textures},
    {"COS_HD_MAX_SIZE", PC_GAMEPLAY_MENU_MOVEMENT, false, 210, PcMenuTab::Graphics, PcMenuSection::Textures},
    {"COS_HD_DISTANT_ISLANDS", PC_GAMEPLAY_MENU_MOVEMENT, false, 350, PcMenuTab::FX, PcMenuSection::Enhancements},
    {"COS_FXAA", PC_GAMEPLAY_MENU_MOVEMENT, false, 312, PcMenuTab::FX, PcMenuSection::PostProcessing},
    {"COS_SHARPENING", PC_GAMEPLAY_MENU_MOVEMENT, false, 314, PcMenuTab::FX, PcMenuSection::PostProcessing},
    {"COS_COLOUR_GRADE", PC_GAMEPLAY_MENU_MOVEMENT, false, 310, PcMenuTab::FX, PcMenuSection::PostProcessing},
    {"COS_HD_SSAO", PC_GAMEPLAY_MENU_MOVEMENT, false, 300, PcMenuTab::FX, PcMenuSection::PostProcessing},
    {"COS_DOF", PC_GAMEPLAY_MENU_MOVEMENT, false, 330, PcMenuTab::FX, PcMenuSection::PostProcessing},
    {"COS_HD_FAR_BLUR", PC_GAMEPLAY_MENU_MOVEMENT, false, 340, PcMenuTab::FX, PcMenuSection::PostProcessing},
    {"COS_HD_BLOOM", PC_GAMEPLAY_MENU_MOVEMENT, false, 320, PcMenuTab::FX, PcMenuSection::PostProcessing},
    {"COS_HD_CLOUD_SHADOWS", PC_GAMEPLAY_MENU_MOVEMENT, false, 360, PcMenuTab::FX, PcMenuSection::Enhancements},
    {"COS_HD_EDGE_LIGHT", PC_GAMEPLAY_MENU_MOVEMENT, false, 370, PcMenuTab::FX, PcMenuSection::Enhancements},
    {"COS_HD_TORCH_LIGHT", PC_GAMEPLAY_MENU_MOVEMENT, false, 380, PcMenuTab::FX, PcMenuSection::Enhancements},
    {"COS_MENU_TEXT_SIZE", PC_GAMEPLAY_MENU_MOVEMENT, false, 600, PcMenuTab::System, PcMenuSection::Menu},
    {"COS_MENU_TRANSPARENCY", PC_GAMEPLAY_MENU_MOVEMENT, false, 605, PcMenuTab::System, PcMenuSection::Menu},
    {"COS_MENU_HIGH_CONTRAST", PC_GAMEPLAY_MENU_MOVEMENT, false, 610, PcMenuTab::System, PcMenuSection::Menu},

    {"COS_JUMP_BUTTON", PC_GAMEPLAY_MENU_MOVEMENT, false, 500, PcMenuTab::Controls, PcMenuSection::Buttons},
    {"COS_SPRINT_SPEED", PC_GAMEPLAY_MENU_MOVEMENT, false, 510, PcMenuTab::Controls, PcMenuSection::Buttons},
    {"COS_DPAD_MENUS", PC_GAMEPLAY_MENU_MOVEMENT, false, 520, PcMenuTab::Controls, PcMenuSection::Buttons},
    {"COS_BWW_FASTER_MOVEMENT", PC_GAMEPLAY_MENU_MOVEMENT, false, 120, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_FASTER_GRAPPLE", PC_GAMEPLAY_MENU_MOVEMENT, false, 130, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_FASTER_BLOCK_PUSH", PC_GAMEPLAY_MENU_MOVEMENT, false, 140, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_FASTER_TRAVERSAL", PC_GAMEPLAY_MENU_MOVEMENT, false, 150, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_SWING_TURN", PC_GAMEPLAY_MENU_MOVEMENT, false, 190, PcMenuTab::Gameplay, PcMenuSection::None},

    {"COS_CAMERA_INVERT_X", PC_GAMEPLAY_MENU_CAMERA, false, 400, PcMenuTab::Controls, PcMenuSection::Camera},
    {"COS_CAMERA_INVERT_Y", PC_GAMEPLAY_MENU_CAMERA, false, 410, PcMenuTab::Controls, PcMenuSection::Camera},
    {"COS_CAMERA_SHAKE", PC_GAMEPLAY_MENU_COMFORT, false, 402, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_HEAT_HAZE", PC_GAMEPLAY_MENU_COMFORT, false, 403, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_SAIL", PC_GAMEPLAY_MENU_SAILING, false, 110, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_UNRESTRICTED_BOAT", PC_GAMEPLAY_MENU_SAILING, false, 260, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_SEA_COMPASS", PC_GAMEPLAY_MENU_SAILING, false, 250, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_INSTANT_TEXT", PC_GAMEPLAY_MENU_PACING, false, 100, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_NO_SONG_REPLAYS", PC_GAMEPLAY_MENU_PACING, false, 180, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_SKIP_INTRO", PC_GAMEPLAY_MENU_TRANSITIONS, false, 200, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_FASTER_BALLAD", PC_GAMEPLAY_MENU_PACING, false, 210, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_SHORT_FADES", PC_GAMEPLAY_MENU_TRANSITIONS, false, 310, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_QUICK_DOORS", PC_GAMEPLAY_MENU_TRANSITIONS, false, 360, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_FAST_LOADING", PC_GAMEPLAY_MENU_TRANSITIONS, false, 370, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_TINGLE_CHESTS", PC_GAMEPLAY_MENU_EXPLORATION, false, 160, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_BWW_REVEAL_SEA_CHART", PC_GAMEPLAY_MENU_SAILING, false, 280, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_FOREST_WATER_KEEP_TREES", PC_GAMEPLAY_MENU_EXPLORATION, false, 260, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_FOREST_WATER_30_MINUTES", PC_GAMEPLAY_MENU_EXPLORATION, false, 270, PcMenuTab::Gameplay, PcMenuSection::None},
    {"COS_PERF_LOG", PC_GAMEPLAY_MENU_MOVEMENT, false, 1000, PcMenuTab::System, PcMenuSection::Logging},
    {"COS_PERF_EVERY", PC_GAMEPLAY_MENU_MOVEMENT, false, 1010, PcMenuTab::System, PcMenuSection::Logging},
    {"COS_HITCH_MS", PC_GAMEPLAY_MENU_MOVEMENT, false, 1020, PcMenuTab::System, PcMenuSection::Logging},
    {"COS_HD_STATS_EVERY", PC_GAMEPLAY_MENU_MOVEMENT, false, 1030, PcMenuTab::System, PcMenuSection::Logging},
    {"COS_DYNRES", PC_GAMEPLAY_MENU_MOVEMENT, true},
    {"COS_GPU_GROUPS", PC_GAMEPLAY_MENU_MOVEMENT, true},
    {"COS_GAME_LANGUAGE", PC_GAMEPLAY_MENU_MOVEMENT, true},
    {"COS_PRECOMPILE", PC_GAMEPLAY_MENU_MOVEMENT, true},
    {"COS_PRECOMPILE_SCREEN", PC_GAMEPLAY_MENU_MOVEMENT, true},
};
inline const PcSettingPresentation* pc_setting_presentation_for(const char* key) {
    if (key) for (const auto& entry : pc_setting_presentation)
        if (std::strcmp(key,entry.key)==0) return &entry;
    return nullptr;
}

inline int pc_menu_setting_order(const char* key, int upstreamOrder) {
    const auto* entry = pc_setting_presentation_for(key);
    return entry && entry->order ? entry->order : 500 + upstreamOrder;
}

// Presentation tabs: Graphics / FX / Gameplay / Controls / System.
inline int pc_menu_setting_tab(const PcSettingDesc* setting) {
    const auto* entry = pc_setting_presentation_for(setting->key);
    return entry && entry->tab != PcMenuTab::Default ? int(entry->tab) : int(setting->tab);
}
inline int pc_menu_setting_section(const char* key) {
    const auto* entry = pc_setting_presentation_for(key);
    return entry ? int(entry->section) : 0;
}

inline bool pc_menu_setting_is_tool(const char* key) {
    const int section = pc_menu_setting_section(key);
    // Unclassified debug settings and the Logging section belong to Developer tools.
    return section == int(PcMenuSection::None) || section == int(PcMenuSection::Logging);
}
