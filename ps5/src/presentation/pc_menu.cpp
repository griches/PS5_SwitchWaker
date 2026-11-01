// PS5 presentation fork of SwitchWaker ba8d0a3 plus existing PS5 menu changes.
// Upstream menu remains untouched; review portable fixes during upstream updates.
// Touch pad opens/closes the menu; Cross confirms and Square goes back.
// Tabs: Graphics / FX / Gameplay / Controls / System. Gameplay uses inline sections.
// The PLAY scene pauses while the menu blocks game input. Other scenes keep
// running without controller input. COS_SMOKE=options-menu retains scripted checks.
// Shared settings keep their existing saved values and adoption callbacks.
#include "pc/pc_settings.h"
#include "pc/pc_menu_policy.h"
#include "pc/pc_menu_smoke.h"
#include "pc/pc_gameplay_menu.h"
#include "pc/pc_menu_widgets.h"
#include "pc/pc_dynres.h"
#include "pc/pc_gpu_opts.h"
#include "pc/pc_hd_textures.h"
#include "pc/game_hooks.h"

#include "pc_internal.h"

#include "pc/pc_controls.h"
#include "pc/pc_profiler.h"
#include "pc/pc_menu_language.h"
#include "pc/pc_ui_style.h"

#include "d/d_com_inf_game.h"
#include "d/d_s_play.h"
#include "d/d_save.h"
#include "d/d_item_data.h"
#include "d/d_item.h"
#include "d/actor/d_a_player.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_layer.h"
#include "f_pc/f_pc_name.h"

#include <imgui.h>
#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>


// Aurora's <dolphin/pad.h> (the game's own comes first on the include path).
extern "C" struct SDL_Gamepad* PADGetSDLGamepadForIndex(u32 index);
extern "C" void PADBlockInput(bool block);

namespace pc {

namespace {

// ---- input ---------------------------------------------------------------------------------------

enum Key : uint32_t {
    kUp = 1u << 0,
    kDown = 1u << 1,
    kLeft = 1u << 2,
    kRight = 1u << 3,
    kConfirm = 1u << 4,
    kBack = 1u << 5,
    kTabLeft = 1u << 6,
    kTabRight = 1u << 7,
    kCombo = 1u << 8, // open / close
};

// GameCube PAD_* bits (dolphin/pad.h) as the COS_INPUT script writes them.
constexpr uint16_t kPadLeft = 0x0001, kPadRight = 0x0002, kPadDown = 0x0004, kPadUp = 0x0008,
                   kPadZ = 0x0010, kPadR = 0x0020, kPadL = 0x0040, kPadA = 0x0100, kPadB = 0x0200;

uint32_t readScript() {
    uint16_t b = 0;
    int8_t sx = 0, sy = 0;
    if (!inputScriptAt(pc_frame_count(), &b, &sx, &sy)) {
        return 0;
    }
    uint32_t k = 0;
    if ((b & (kPadL | kPadR | kPadZ)) == (kPadL | kPadR | kPadZ)) {
        return kCombo;
    }
    k |= (b & kPadUp) || sy > 60 ? kUp : 0;
    k |= (b & kPadDown) || sy < -60 ? kDown : 0;
    k |= (b & kPadLeft) || sx < -60 ? kLeft : 0;
    k |= (b & kPadRight) || sx > 60 ? kRight : 0;
    k |= (b & kPadA) ? kConfirm : 0;
    k |= (b & kPadB) ? kBack : 0;
    k |= (b & kPadL) ? kTabLeft : 0;
    k |= (b & kPadR) ? kTabRight : 0;
    return k;
}

uint32_t readGamepads() {
    uint32_t k = 0;
    for (u32 port = 0; port < 4; port++) {
        SDL_Gamepad* gp = PADGetSDLGamepadForIndex(port);
        if (gp == nullptr) {
            continue;
        }
        // PlayStation 5: the touch pad's click, which the port's SDL driver reports as BACK and
        // the game's default mapping gives to no button.
        bool combo = SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_BACK);
        if (combo) {
            k |= kCombo;
            continue;
        }
        const int lx = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX);
        const int ly = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY);
        k |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -20000 ? kUp : 0;
        k |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 20000 ? kDown : 0;
        k |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -20000 ? kLeft : 0;
        k |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 20000 ? kRight : 0;
        k |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_SOUTH) ? kConfirm : 0;
        // Match the game's GameCube B mapping: Square backs out of menus.
        k |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_WEST) ? kBack : 0;
        k |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ? kTabLeft : 0;
        k |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) ? kTabRight : 0;
    }
    return k;
}



// ---- warp destinations ---------------------------------------------------------------------------

struct Warp {
    const char* stage;
    int room;
    int point;
    const char* label; // nullptr: the stage name
};

// The main places, using catalog keys (the sea's rooms are its 7x7 grid squares, A1 = 1).
const Warp kMainWarps[] = {
    {"sea", 44, 206, "Outset Island"},
    {"LinkRM", 0, 0, "The hero's house"},
    {"sea", 11, 0, "Windfall Island"},
    {"sea", 13, 0, "Dragon Roost Island"},
    {"M_NewD2", 0, 0, "Dragon Roost Cavern"},
    {"sea", 41, 0, "Forest Haven"},
    {"Omori", 0, 3, "Forest Haven (inside)"},
    {"kindan", 0, 0, "Forbidden Woods"},
    {"sea", 26, 0, "Tower of the Gods (sea)"},
    {"Siren", 0, 0, "Tower of the Gods"},
    {"sea", 1, 0, "Forsaken Fortress (sea)"},
    {"MajyuE", 0, 0, "Forsaken Fortress"},
    {"Hyrule", 0, 0, "Sunken castle"},
    {"sea", 45, 0, "Headstone Island"},
    {"M_Dai", 0, 0, "Earth Temple"},
    {"sea", 4, 0, "Gale Isle"},
    {"kaze", 15, 15, "Wind Temple"},
};

// Every stage of the disc with the start native/tools/boot_sweep.py --list picks.
const Warp kAllWarps[] = {
#define W(s, r, p) {s, r, p, nullptr},
    W("ADMumi", 0, 100)
    W("A_R00", 0, 0)
    W("A_nami", 0, 0)
    W("A_umikz", 0, 0)
    W("Abesso", 0, 0)
    W("Abship", 0, 0)
    W("Adanmae", 0, 0)
    W("Amos_T", 0, 0)
    W("Asoko", 0, 0)
    W("Atorizk", 0, 0)
    W("Cave01", 0, 0)
    W("Cave02", 0, 0)
    W("Cave03", 0, 0)
    W("Cave04", 0, 0)
    W("Cave05", 0, 0)
    W("Cave06", 0, 0)
    W("Cave07", 0, 0)
    W("Cave08", 0, 0)
    W("Cave09", 0, 0)
    W("Cave10", 1, 0)
    W("Cave11", 1, 0)
    W("Comori", 0, 0)
    W("DmSpot0", 0, 0)
    W("E3ROOP", 0, 0)
    W("ENDumi", 0, 0)
    W("Ebesso", 0, 0)
    W("Edaichi", 0, 0)
    W("Ekaze", 0, 0)
    W("Fairy01", 0, 0)
    W("Fairy02", 0, 0)
    W("Fairy03", 0, 0)
    W("Fairy04", 0, 0)
    W("Fairy05", 0, 0)
    W("Fairy06", 0, 0)
    W("GTower", 0, 0)
    W("GanonA", 0, 0)
    W("GanonB", 0, 0)
    W("GanonC", 0, 0)
    W("GanonD", 0, 0)
    W("GanonE", 0, 0)
    W("GanonJ", 1, 0)
    W("GanonL", 0, 0)
    W("GanonM", 0, 0)
    W("GanonN", 0, 0)
    W("H_test", 0, 0)
    W("Hyroom", 0, 0)
    W("Hyrule", 0, 0)
    W("ITest61", 0, 0)
    W("ITest62", 0, 0)
    W("ITest63", 0, 0)
    W("I_SubAN", 9, 0)
    W("I_TestM", 0, 0)
    W("I_TestR", 0, 0)
    W("KATA_HB", 0, 0)
    W("KATA_RM", 18, 1)
    W("K_Test2", 0, 0)
    W("K_Test3", 0, 0)
    W("K_Test4", 0, 0)
    W("K_Test5", 0, 0)
    W("K_Test6", 0, 0)
    W("K_Test8", 0, 0)
    W("K_Test9", 0, 0)
    W("K_Testa", 0, 0)
    W("K_Testb", 0, 0)
    W("K_Testc", 0, 0)
    W("K_Testd", 0, 0)
    W("K_Teste", 0, 0)
    W("Kaisen", 0, 0)
    W("LinkRM", 0, 1)
    W("LinkUG", 0, 1)
    W("M2ganon", 0, 0)
    W("M2tower", 0, 16)
    W("M_Dai", 0, 0)
    W("M_DaiB", 0, 0)
    W("M_DaiMB", 12, 0)
    W("M_Dra09", 9, 0)
    W("M_DragB", 0, 0)
    W("M_NewD2", 0, 0)
    W("MajyuE", 0, 0)
    W("MiniHyo", 0, 0)
    W("MiniKaz", 0, 0)
    W("Mjtower", 0, 16)
    W("Msmoke", 0, 0)
    W("Mukao", 0, 0)
    W("Nitiyou", 0, 0)
    W("Obombh", 0, 0)
    W("Obshop", 1, 0)
    W("Ocean", 0, 0)
    W("Ocmera", 0, 0)
    W("Ocrogh", 0, 0)
    W("Ojhous", 0, 0)
    W("Ojhous2", 1, 0)
    W("Omasao", 0, 0)
    W("Omori", 0, 0)
    W("Onobuta", 0, 0)
    W("Opub", 0, 0)
    W("Orichh", 0, 0)
    W("Otkura", 0, 0)
    W("PShip", 0, 0)
    W("PShip2", 0, 0)
    W("PShip3", 0, 0)
    W("Pdrgsh", 0, 0)
    W("Pfigure", 0, 0)
    W("Pjavdou", 0, 0)
    W("Pnezumi", 0, 0)
    W("ShipD", 0, 0)
    W("Siren", 0, 0)
    W("SirenB", 0, 0)
    W("SirenMB", 23, 0)
    W("SubD42", 0, 0)
    W("SubD43", 0, 0)
    W("SubD44", 0, 0)
    W("SubD45", 0, 0)
    W("SubD51", 0, 0)
    W("SubD71", 0, 0)
    W("TEST", 0, 0)
    W("TF_01", 0, 0)
    W("TF_02", 0, 0)
    W("TF_03", 0, 0)
    W("TF_04", 0, 0)
    W("TF_05", 0, 0)
    W("TF_06", 0, 0)
    W("TF_07", 1, 0)
    W("TyuTyu", 0, 0)
    W("VrTest", 0, 0)
    W("WarpD", 0, 0)
    W("Xboss0", 0, 0)
    W("Xboss1", 0, 0)
    W("Xboss2", 0, 0)
    W("Xboss3", 0, 0)
    W("figureA", 0, 0)
    W("figureB", 0, 0)
    W("figureC", 0, 0)
    W("figureD", 0, 0)
    W("figureE", 0, 0)
    W("figureF", 0, 0)
    W("figureG", 0, 0)
    W("kazan", 0, 0)
    W("kaze", 15, 15)
    W("kazeB", 0, 0)
    W("kazeMB", 6, 0)
    W("kenroom", 0, 0)
    W("kinBOSS", 0, 0)
    W("kinMB", 10, 0)
    W("kindan", 0, 0)
    W("ma2room", 0, 0)
    W("ma3room", 0, 0)
    W("majroom", 0, 0)
    W("morocam", 0, 0)
    W("sea", 1, 0)
    W("sea_E", 0, 0)
    W("sea_T", 44, 0)
    W("tincle", 0, 0)
#undef W
};

constexpr int kMainWarpCount = (int)(sizeof(kMainWarps) / sizeof(kMainWarps[0]));
constexpr int kAllWarpCount = (int)(sizeof(kAllWarps) / sizeof(kAllWarps[0]));

const Warp& warpAt(int i) {
    return i < kMainWarpCount ? kMainWarps[i] : kAllWarps[i - kMainWarpCount];
}

// ---- built-in settings ---------------------------------------------------------------------------


// Upstream descriptors may still carry a separate English field.
const char* T(const char* key) { return pc_menu_text(key); }
const char* settingLabel(const PcSettingDesc* d) { return T(d->labelEn ? d->labelEn : d->label); }
const char* settingHelp(const PcSettingDesc* d) { return T(d->helpEn ? d->helpEn : d->help); }

void applyFbScale(const char*, const char* v, void*) {
    const float scale = (float)atof(v);
    setFrameBufferScale(scale);
    pc_dynres_configure(pc_settings_get("COS_DYNRES"), scale);
    pc_fps60_hold("internal resolution changed"); // 60 fps step E: the EFB is resized
}
void applyDynres(const char*, const char* v, void*) {
    pc_dynres_configure(v, (float)atof(pc_settings_get("COS_FB_SCALE")));
    pc_fps60_hold("dynamic resolution changed");
}
void applyDof(const char*, const char* v, void*) { pc_dof_set(strcmp(v, "0") != 0); }
void applyCameraInvertX(const char*, const char* v, void*) { pc_camera_invert_x_set(strcmp(v, "1") == 0); }
void applyCameraInvertY(const char*, const char* v, void*) { pc_camera_invert_y_set(strcmp(v, "1") == 0); }
void applyCameraShake(const char*, const char* v, void*) { pc_camera_shake_set(strcmp(v, "1") == 0); }
void applyHeatHaze(const char*, const char* v, void*) { pc_heat_haze_set(strcmp(v, "1") == 0); }
void applyOverlay(const char*, const char* v, void*) { gConfig.fpsOverlay = strcmp(v, "1") == 0; pc_profiler_reset(); }
void applyOverlayDetail(const char*, const char* v, void*) {
    gConfig.fpsOverlayCompact = strcmp(v, "compact") == 0;
    pc_profiler_mode(v);
}
void applyPerfEvery(const char*, const char* v, void*) { perfSetEvery((unsigned int)atoi(v)); }
void applyPerfLog(const char*, const char* v, void*) { gConfig.perfLog = strcmp(v, "0") != 0; }
void applyHitch(const char*, const char* v, void*) { perfSetHitch((unsigned int)atoi(v)); }
void applyHdStats(const char*, const char* v, void*) { pc_hd_textures_set_stats_every((unsigned int)atoi(v)); }
void applyGpuGroups(const char*, const char* v, void*) { pc_gpu_groups_set(atoi(v)); }
void applyFps60(const char*, const char* v, void*) { pc_fps60_set(strcmp(v, "1") == 0); }

#define CHOICES(name) name, (int)(sizeof(name) / sizeof(name[0]))

constexpr PcSettingTab kCameraTab = PC_SETTING_TAB_GAMEPLAY;
constexpr int kCameraOrder = 90;

const PcSettingChoice kFbScale[] = {
    {"0", "Match output"},
    {"1.0", "854x480 (1.0)"},  {"1.125", "960x540 (1.125)"}, {"1.25", "1067x600 (1.25)"},
    {"1.5", "1280x720 (1.5)"}, {"2.0", "1707x960 (2.0)"},    {"2.25", "1920x1080 (2.25)"},
    {"3.0", "2560x1440 (3.0)"}, {"4.5", "3840x2160 (4K, 4.5)"},
};
// PlayStation 5: the size of the picture the port hands to the console (ps5/src/startup.c reads it)
const PcSettingChoice kPs5Output[] = {{"2160", "3840x2160 (4K)"}, {"1440", "2560x1440"}, {"1080", "1920x1080"}};
const PcSettingChoice kAspect[] = {{"16:9", "16:9"}, {"4:3", "4:3 (GameCube)"},
};
const PcSettingChoice kOnOff[] = {{"0", "Off"}, {"1", "On"}};
// Existing saved keys mean "effect enabled". Invert presentation for Remove options.
const PcSettingChoice kDisableEffect[] = {{"1", "Off"}, {"0", "On"}};
const PcSettingChoice kHdMaxSize[] = {{"auto", "Automatic (1024)"}, {"256", "256"}, {"512", "512"},
                                      {"1024", "1024"}, {"full", "No limit"}};
// "auto" (the default): on while the 60 fps row is on (docs/FPS60_PLAN.md step E), off at 30 fps;
// "0" keeps it off even with 60 fps (the user's choice is respected); "1" on at 30 fps too.
const PcSettingChoice kDynres[] = {{"auto", "With 60 fps"},
                                   {"0", "Off"},
                                   {"1", "Always"}};
const PcSettingChoice kDetail[] = {{"compact", "Compact"}, {"full", "Expanded"},
    {"graph", "Frame graph"}, {"profiler", "Profiler"}};
const PcSettingChoice kOverlayPosition[] = {{"tl", "Top left"},{"tr", "Top right"},
    {"bl", "Bottom left"},{"br", "Bottom right"}};
const PcSettingChoice kOverlaySize[] = {{"small", "Small"},{"standard", "Standard"},{"large", "Large"}};
void applyOverlayPosition(const char*,const char* value,void*) { pc_profiler_position(value); }
void applyOverlaySize(const char*,const char* value,void*) { pc_profiler_scale(value); }
void applyOverlayBehindMenu(const char*,const char* value,void*) { pc_profiler_behind_menu(value); }
const PcSettingChoice kPrecompile[] = {
    {"boot", "Startup shaders"}, {"full", "Full"}, {"all", "All (no loading screen)"}, {"off", "Off"}};
const PcSettingChoice kPrecompileScreen[] = {
    {"auto", "Automatic"}, {"priority", "Priority only"},
    {"always", "Always"}, {"never", "Never"}};
const PcSettingChoice kPerfEvery[] = {
    {"0", "Off"}, {"30", "Every 30 frames"},
    {"60", "Every 60 frames"}, {"120", "Every 120 frames"},
    {"300", "Every 300 frames"}};
const PcSettingChoice kGpuGroups[] = {{"0", "Off"}, {"1", "Per group"},
                                      {"2", "Per J3D material"}};
const PcSettingChoice kHitch[] = {{"0", "Off"}, {"33", "Over 33 ms"},
                                  {"40", "Over 40 ms"}, {"50", "Over 50 ms"},
                                  {"100", "Over 100 ms"}};
const PcSettingChoice kHdStats[] = {{"0", "Off"}, {"150", "Every 150 frames"},
                                    {"300", "Every 300 frames"},
                                    {"600", "Every 600 frames"}};

float sMenuTextScale = 1.0f;
float sMenuOpacity = 0.6f;
const PcSettingChoice kMenuTransparency[] = {
    {"0", "0%"}, {"10", "10%"}, {"20", "20%"}, {"30", "30%"},
    {"40", "40%"}, {"50", "50%"}, {"60", "60%"}, {"70", "70%"}, {"80", "80%"}
};
void applyMenuTransparency(const char*, const char* value, void*) {
    sMenuOpacity = 0.6f;
    for (const auto& choice : kMenuTransparency) {
        if (strcmp(value, choice.value) == 0) {
            sMenuOpacity = 1.0f - std::atoi(value) * .01f;
            break;
        }
    }
}
bool sMenuHighContrast = false;
const PcSettingChoice kMenuTextSize[] = {
    {"0", "Standard"}, {"1", "Large"}, {"2", "Extra large"}
};
void applyMenuTextSize(const char*, const char* value, void*) {
    sMenuTextScale = strcmp(value, "1") == 0 ? 1.15f : strcmp(value, "2") == 0 ? 1.3f : 1.0f;
}
void applyMenuContrast(const char*, const char* value, void*) {
    sMenuHighContrast = strcmp(value, "1") == 0;
}

const PcSettingChoice kMenuLanguages[] = {
    {"auto", "Game language"}, {"en", "English"}, {"de", "Deutsch"},
    {"fr", "Français"}, {"es", "Español"}, {"it", "Italiano"}
};
void applyMenuLanguage(const char*, const char* value, void*) { pc_menu_language_set(value); }
const PcSettingDesc kBuiltins[] = {
    {"COS_MENU_LANGUAGE", "Menu language", "Changes this menu's language. Does not change the game's language.",
     PC_SETTING_TAB_DEBUG, 0, CHOICES(kMenuLanguages), "auto", applyMenuLanguage, nullptr, 940},
    {"COS_MENU_TEXT_SIZE", "Menu text size", "Changes text size in this menu. Does not affect the HUD.",
     PC_SETTING_TAB_GRAPHICS, 0, CHOICES(kMenuTextSize), "0", applyMenuTextSize, nullptr, 950},
    {"COS_MENU_TRANSPARENCY", "Menu transparency", "Shows more of the game behind the menu. High contrast keeps the background solid.",
     PC_SETTING_TAB_GRAPHICS, 0, CHOICES(kMenuTransparency), "40", applyMenuTransparency, nullptr, 955},
    {"COS_MENU_HIGH_CONTRAST", "High contrast", "Makes the menu background solid and the text brighter.",
     PC_SETTING_TAB_GRAPHICS, 0, CHOICES(kOnOff), "0", applyMenuContrast, nullptr, 960},
    // Gráficos
    {"COS_FB_SCALE", "Internal resolution",
     "Sets how sharply the game is drawn. Lower resolutions can improve performance.",
     PC_SETTING_TAB_GRAPHICS, PC_SETTING_PER_MODE, CHOICES(kFbScale), "0", applyFbScale,
     nullptr, 10},
    {"COS_DYNRES", "Dynamic resolution",
     "Lowers resolution during slowdowns. The HUD stays sharp. Choose With 60 fps to use it only in 60 fps mode.",
     PC_SETTING_TAB_GRAPHICS, PC_SETTING_PER_MODE, CHOICES(kDynres), "auto", applyDynres, nullptr, 20},
    {"COS_DOF", "Depth of field", "Blurs distant scenery, like the original game.",
     PC_SETTING_TAB_GRAPHICS, 0, CHOICES(kOnOff), "0", applyDof, nullptr, 50},
    {"COS_CAMERA_INVERT_X", "Invert horizontal",
     "Reverses left and right for the camera and aiming.",
     kCameraTab, 0, CHOICES(kOnOff), "0", applyCameraInvertX, nullptr, kCameraOrder + 0},
    {"COS_CAMERA_INVERT_Y", "Invert vertical",
     "Reverses up and down for the camera and aiming.",
     kCameraTab, 0, CHOICES(kOnOff), "0", applyCameraInvertY, nullptr, kCameraOrder + 1},
    {"COS_CAMERA_SHAKE", "Remove camera shake",
     "On removes camera shake. Off restores it. Controller vibration is unchanged.",
     kCameraTab, 0, CHOICES(kDisableEffect), "0", applyCameraShake, nullptr, kCameraOrder + 2},
    {"COS_HEAT_HAZE", "Remove heat haze",
     "On removes heat distortion. Off restores it. The original effect can be too strong at 60 fps.",
     kCameraTab, 0, CHOICES(kDisableEffect), "0", applyHeatHaze, nullptr, kCameraOrder + 3},
    {"COS_ASPECT", "Aspect ratio", "16:9 widescreen or the GameCube's original 4:3.",
     PC_SETTING_TAB_GRAPHICS, PC_SETTING_RESTART, CHOICES(kAspect), "16:9", nullptr, nullptr,
     70},
    {"COS_PS5_OUTPUT", "Output resolution",
     "Sets the resolution sent to the console. Match output resolution uses this for internal resolution too.",
     PC_SETTING_TAB_GRAPHICS, PC_SETTING_RESTART, CHOICES(kPs5Output), "2160", nullptr, nullptr, 71},
    {"COS_HD_TEXTURES", "HD textures", "Uses an installed HD texture pack.",
     PC_SETTING_TAB_GRAPHICS, 0, CHOICES(kOnOff), "0", nullptr, nullptr, 80},
    {"COS_HD_MAX_SIZE", "HD texture size limit",
     "Limits HD textures to this width or height. Auto uses 1024 pixels. Changes apply gradually.",
     PC_SETTING_TAB_GRAPHICS, PC_SETTING_PER_MODE, CHOICES(kHdMaxSize), "full", nullptr, nullptr, 81},
    // Rendimiento
    {"COS_FPS_OVERLAY", "FPS counter", "Shows frame rate and performance details.",
     PC_SETTING_TAB_PERFORMANCE, 0, CHOICES(kOnOff), "0", applyOverlay, nullptr, 20},
    {"COS_FPS_OVERLAY_DETAIL", "Counter layout",
     "Choose a simple counter, details, a frame-time graph or a profiler. Memory shows game heaps only.",
     PC_SETTING_TAB_PERFORMANCE, 0, CHOICES(kDetail), "compact", applyOverlayDetail, nullptr, 30},
    {"COS_FPS_OVERLAY_POSITION", "Counter position", "Chooses which corner shows the counter.",
     PC_SETTING_TAB_PERFORMANCE,0,CHOICES(kOverlayPosition),"bl",applyOverlayPosition,nullptr,31},
    {"COS_FPS_OVERLAY_SIZE", "Counter size", "Changes the size of the counter and graph.",
     PC_SETTING_TAB_PERFORMANCE,0,CHOICES(kOverlaySize),"standard",applyOverlaySize,nullptr,32},
    {"COS_FPS_OVERLAY_BEHIND_MENU", "Counter behind menu", "Draws the counter behind the options menu. Keeps the menu centered.",
     PC_SETTING_TAB_PERFORMANCE,0,CHOICES(kOnOff),"0",applyOverlayBehindMenu,nullptr,33},
    {"COS_PRECOMPILE", "Shader preloading",
     "Chooses which shaders to prepare before playing.",
     PC_SETTING_TAB_PERFORMANCE, PC_SETTING_RESTART, CHOICES(kPrecompile), "off", nullptr,
     nullptr, 40},
    {"COS_PRECOMPILE_SCREEN", "Shader loading screen",
     "Auto shows the loading screen when no shader cache is available.",
     PC_SETTING_TAB_PERFORMANCE, PC_SETTING_RESTART, CHOICES(kPrecompileScreen), "auto", nullptr, nullptr, 50},
    {"COS_FPS60", "60 fps",
     "Adds in-between frames for smoother motion. Gameplay stays at 30 fps. May fall back to 30 fps during slowdowns.",
     PC_SETTING_TAB_PERFORMANCE, PC_SETTING_PER_MODE, CHOICES(kOnOff), "1", applyFps60, nullptr, 60},
    // Depuración
    {"COS_PERF_EVERY", "Performance log interval",
     "Frames between performance log entries.",
     PC_SETTING_TAB_DEBUG, 0, CHOICES(kPerfEvery), "0", applyPerfEvery, nullptr, 105},
    {"COS_HITCH_MS", "Slow-frame logging",
     "Writes a [cos] hitch line to the log for every game frame that takes longer than this.",
     PC_SETTING_TAB_DEBUG, 0, CHOICES(kHitch), "0", applyHitch, nullptr, 107},
    {"COS_GPU_GROUPS", "GPU timers per group",
     "Times the GPU per draw group (sky, background, opaque, particles...) in the perf-switch "
     "lines.",
     PC_SETTING_TAB_DEBUG, 0, CHOICES(kGpuGroups), "0", applyGpuGroups, nullptr, 100},
    {"COS_PERF_LOG", "Performance logging",
     "Shows or hides CPU performance log lines. The interval and hitch threshold control what is recorded.",
     PC_SETTING_TAB_DEBUG, 0, CHOICES(kOnOff), "1", applyPerfLog, nullptr, 110},
    {"COS_HD_STATS_EVERY", "HD texture stats",
     "How many frames between [cos] hd-textures lines (hits, loads, MiB) while HD textures are on.",
     PC_SETTING_TAB_DEBUG, 0, CHOICES(kHdStats), "0", applyHdStats, nullptr, 112},
};

// Presets are derived from effective settings, never saved as a second source of truth.
struct FxPresetEntry { const char* key; const char* original; const char* enhanced; };
constexpr FxPresetEntry kFxPreset[] = {
    {"COS_HD_SSAO", "0", "1"}, {"COS_COLOUR_GRADE", "original", "vivid"},
    {"COS_FXAA", "0", "1"}, {"COS_SHARPENING", "0", "1"},
    {"COS_HD_BLOOM", "0", "1"}, {"COS_DOF", "0", "1"},
    {"COS_HD_FAR_BLUR", "0", "1"}, {"COS_HD_DISTANT_ISLANDS", "0", "1"},
    {"COS_HD_CLOUD_SHADOWS", "0", "1"}, {"COS_HD_EDGE_LIGHT", "0", "1"},
    {"COS_HD_TORCH_LIGHT", "0", "1"},
};
const char* fxPresetValue() {
    bool original = true, enhanced = true;
    for (const auto& entry : kFxPreset) {
        const char* value = pc_settings_get(entry.key);
        original &= strcmp(value, entry.original) == 0;
        enhanced &= strcmp(value, entry.enhanced) == 0;
    }
    return original ? "original" : enhanced ? "enhanced" : "custom";
}
const PcSettingChoice kFxPresets[] = {{"original", "Original"}, {"enhanced", "Enhanced"}, {"custom", "Custom"}};
const PcSettingDesc kFxPresetRow = {"FX_PRESET", "Preset",
    "Original turns all effects off. Enhanced enables all effects with Vivid colours. Individual changes use Custom.",
    PC_SETTING_TAB_FX, 0, CHOICES(kFxPresets), "original", nullptr, nullptr, -1000};

// ---- menu state ----------------------------------------------------------------------------------

enum class RowKind { MenuDemo, RestoreDefaults, DeveloperTools, Setting, Warp, Sailing, PictoBox, DeluxePictoBox, Screenshot, Reload };

struct Row {
    RowKind kind;
    const PcSettingDesc* desc = nullptr;
};

const char* const kTabNames[PC_SETTING_TABS] = {"Graphics", "FX",
    "Gameplay",
    "Controls", "System"};

const char* gameplayGroupName(int group) {
    static const char* names[] = {"Visuals", "Movement", "Camera", "Sailing", "Text and songs", "Transitions", "Exploration"};
    return T(names[group]);
}

struct Menu {
    bool initialized = false;
    bool open = false;
    bool warpPage = false;
    bool toolsPage = false;
    int systemRow = 0;
    int tab = 0;
    int row[PC_SETTING_TABS] = {};
    int warpRow = 0;
    bool scrollToRow = false;
    bool scrolling = false;
    float scrollViewportHeight = 0.f;
    float scrollContentMax = 0.f;
    double openedAt = 0.0;
    double pageChangedAt = 0.0;
    int displayedPage = -1;
    uint32_t prevKeys = 0;
    uint32_t repeatKey = 0;
    unsigned int repeatFrames = 0;
    bool waitRelease = false;
    unsigned int opened = 0;
    bool pendingShot = false;
    bool demo = false, demoReady = false;
    double demoStarted = 0.0;
    std::string toast;
    unsigned int toastUntil = 0;
} m;

int& selectedMenuRow(int tab) {
    if (tab == PC_SETTING_TAB_DEBUG && m.toolsPage) return m.systemRow;
    return m.row[tab];
}

void toast(const std::string& text) {
    m.toast = text;
    m.toastUntil = pc_frame_count() + 90;
    writef(STDERR_FILENO, "[cos] menu: %s\n", text.c_str());
}

void* isPlayScene(void* proc, void*) {
    return fpcM_GetName(proc) == fpcNm_PLAY_SCENE_e ? proc : nullptr;
}

// The PLAY scene exists (the game proper, not the title or the name entry).
bool inPlay() {
    return fpcM_Search(isPlayScene, nullptr) != nullptr;
}

std::vector<Row> rowsOf(int tab) {
    std::vector<Row> rows;
    if (tab == PC_SETTING_TAB_FX) rows.push_back({RowKind::Setting, &kFxPresetRow});
    if (tab == PC_SETTING_TAB_DEBUG && !m.toolsPage) {
        rows.push_back({RowKind::RestoreDefaults});
        rows.push_back({RowKind::DeveloperTools});
    }
    if (tab == PC_SETTING_TAB_DEBUG && m.toolsPage) {
        rows.push_back({RowKind::Warp});
        rows.push_back({RowKind::Sailing});
        rows.push_back({RowKind::PictoBox});
        rows.push_back({RowKind::DeluxePictoBox});
        rows.push_back({RowKind::Screenshot});
        rows.push_back({RowKind::MenuDemo});
        rows.push_back({RowKind::Reload});
    }
    for (int i = 0; i < pc_settings_count(); i++) {
        const PcSettingDesc* d = pc_settings_at(i);
        if (pc_menu_setting_visible(d) && pc_menu_setting_tab(d) == tab) {
            if (tab == PC_SETTING_TAB_DEBUG &&
                pc_menu_setting_is_tool(d->key) != m.toolsPage) continue;
            rows.push_back({RowKind::Setting, d});
        }
    }
    if (tab == PC_SETTING_TAB_GAMEPLAY) {
        // One list: group related settings, retaining their existing order within each section.
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            const auto ga=pc_gameplay_menu_group(a.desc->key), gb=pc_gameplay_menu_group(b.desc->key);
            return ga != gb ? ga < gb : pc_menu_setting_order(a.desc->key, a.desc->order) <
                pc_menu_setting_order(b.desc->key, b.desc->order);
        });
    } else {
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            const int aOrder = a.kind == RowKind::RestoreDefaults ? 615 :
                a.kind == RowKind::DeveloperTools ? 620 :
                pc_menu_setting_order(a.desc ? a.desc->key : "", a.desc ? a.desc->order : 0);
            const int bOrder = b.kind == RowKind::RestoreDefaults ? 615 :
                b.kind == RowKind::DeveloperTools ? 620 :
                pc_menu_setting_order(b.desc ? b.desc->key : "", b.desc ? b.desc->order : 0);
            return aOrder < bOrder;
        });
    }
    return rows;
}

const char* choiceLabel(const PcSettingDesc* d, const char* value) {
    for (int i = 0; i < d->choiceCount; i++) {
        if (strcmp(d->choices[i].value, value) == 0) {
            return T(d->choices[i].labelEn ? d->choices[i].labelEn : d->choices[i].label);
        }
    }
    return value[0] != '\0' ? value : "-";
}

PcOperationMode rowMode(const PcSettingDesc* d) {
    return pc_settings_mode();
}

void cycle(const PcSettingDesc* d, int dir) {
    if (d == &kFxPresetRow) {
        // Custom is a derived state. Cycle between the two actionable presets.
        const char* current = fxPresetValue();
        int index = strcmp(current, "original") == 0 ? 0 : strcmp(current, "enhanced") == 0 ? 1 : 2;
        index = index == 2 ? (dir > 0 ? 0 : 1) : 1 - index;
        for (const auto& entry : kFxPreset) {
            if (pc_settings_locked(entry.key)) {
                toast(std::string(T("Preset")) + T(": set by the environment"));
                return;
            }
        }
        for (const auto& entry : kFxPreset) {
            if (pc_settings_set_mode(entry.key, pc_settings_mode(), index == 0 ? entry.original : entry.enhanced) < 0) {
                toast(T("Could not save the setting"));
                return;
            }
        }
        return;
    }
    if (pc_settings_locked(d->key)) {
        toast(std::string(settingLabel(d)) + T(": set by the environment"));
        return;
    }
    if (d->choiceCount <= 0) {
        return;
    }
    const PcOperationMode mode = rowMode(d);
    const char* cur = pc_settings_get_mode(d->key, mode);
    int idx = -1;
    for (int i = 0; i < d->choiceCount; i++) {
        if (strcmp(d->choices[i].value, cur) == 0) {
            idx = i;
        }
    }
    idx = idx < 0 ? 0 : (idx + dir + d->choiceCount) % d->choiceCount;
    const char* value = d->choices[idx].value;
    writef(STDERR_FILENO, "[cos] menu: %s%s%s -> %s\n", d->key, (d->flags & PC_SETTING_PER_MODE) ? "@" : "",
           (d->flags & PC_SETTING_PER_MODE) ? (mode == PC_MODE_DOCKED ? "docked" : "handheld") : "", value);
    if (pc_settings_set_mode(d->key, mode, value) < 0)
        toast(T("Could not save the setting"));
}

void setOpen(bool open) {
    if (open == m.open) {
        return;
    }
    m.open = open;
    m.warpPage = false;
    m.toolsPage = false;
    m.waitRelease = true;
    PADBlockInput(open);
    if (open) {
        m.openedAt = ImGui::GetTime();
        m.displayedPage = -1;
        m.opened++;
        if (inPlay()) {
            // The game is frozen while the menu is open, so a rumble running now would never get the
            // game's stop (the pad keeps a rumble until told otherwise), and a warp from the menu ends
            // the scene without it: the motor stops here, as the game's own pause screen does, and
            // the vibration manager's state is reset to match (the game sends new rumbles after).
            dComIfGp_getVibration().Kill();
        }
        applyMenuLanguage(nullptr, pc_settings_get("COS_MENU_LANGUAGE"), nullptr);
        applyMenuTextSize(nullptr, pc_settings_get("COS_MENU_TEXT_SIZE"), nullptr);
        applyMenuTransparency(nullptr, pc_settings_get("COS_MENU_TRANSPARENCY"), nullptr);
        applyMenuContrast(nullptr, pc_settings_get("COS_MENU_HIGH_CONTRAST"), nullptr);
        m.scrollToRow = true;
        writef(STDERR_FILENO, "[cos] menu: open at frame %u (%s)\n", pc_frame_count(),
               inPlay() ? "game paused" : "not in the PLAY scene: the game runs on without input");
        heapReport("options menu opened");
    } else {
        writef(STDERR_FILENO, "[cos] menu: closed at frame %u\n", pc_frame_count());
    }
}

void doWarp(const Warp& w) {
    if (!inPlay()) {
        toast(T("Travel only works in the game"));
        return;
    }
    setOpen(false);
    writef(STDERR_FILENO, "[cos] menu: warp to %s room %d point %d\n", w.stage, w.room, w.point);
    dComIfGp_setNextStage(w.stage, (s16)w.point, (s8)w.room, -1);
}

// "Navegar (barco, vela y batuta)": the sailing preset (pc_preset.cpp) on the file being played, in
// memory only (the game's own save screen keeps it), then to its spawn on the boat.
void doSailing() {
    if (!inPlay()) {
        toast(T("Sailing only works in the game"));
        return;
    }
    applySailingPreset(true);
    setOpen(false);
    writef(STDERR_FILENO, "[cos] menu: sailing preset, warp to %s room %d point %d\n", kSailingSpawn.stage,
           kSailingSpawn.room, kSailingSpawn.point);
    dComIfGp_setNextStage(kSailingSpawn.stage, (s16)kSailingSpawn.point, (s8)kSailingSpawn.room, -1);
}

void givePictoBox(bool deluxe) {
    if (!inPlay()) {
        toast(T("Load a game first."));
        return;
    }
    const u8 wanted = deluxe ? dItemNo_DELUXE_PICTO_BOX_e : dItemNo_PICTO_BOX_e;
    const u8 item = dComIfGs_getItem(dInvSlot_CAMERA_e);
    if (item == wanted) {
        toast(deluxe ? T("You already have a Deluxe Picto Box.") : T("You already have a Picto Box."));
        return;
    }
    execItemGet(wanted);
    // Keep equipped buttons in sync when switching either way for testing.
    for (int i = 0; i < 3; ++i) {
        const u8 equipped = dComIfGp_getSelectItem(i);
        if (equipped == dItemNo_PICTO_BOX_e || equipped == dItemNo_DELUXE_PICTO_BOX_e)
            g_dComIfG_gameInfo.play.setSelectItem(i, wanted);
    }
    toast(deluxe ? T("Deluxe Picto Box added. Equip it from the Items menu.")
                 : T("Picto Box added. Equip it from the Items menu."));
}

void activate(const Row& r, int dir) {
    switch (r.kind) {
    case RowKind::MenuDemo:
        if (dir == 0) {
            m.demo = true;
            m.demoReady = false;
            m.toolsPage = m.warpPage = false;
            m.tab = 0;
            m.row[0] = 0;
            m.scrollToRow = true;
            m.toast.clear();
        }
        break;
    case RowKind::RestoreDefaults:
        if (dir == 0) {
            if (pc_settings_restore_defaults() != 0) {
                toast(T("Could not remove the settings file."));
                break;
            }
            toast(T("Defaults restored. Restart the game to apply all changes."));
        }
        break;
    case RowKind::DeveloperTools:
        if (dir == 0) { m.toolsPage = true; m.scrollToRow = true; }
        break;
    case RowKind::Setting:
        cycle(r.desc, dir);
        break;
    case RowKind::Warp:
        if (dir == 0) {
            m.warpPage = true;
            m.scrollToRow = true;
        }
        break;
    case RowKind::Sailing:
        if (dir == 0) {
            doSailing();
        }
        break;
    case RowKind::PictoBox:
        if (dir == 0) givePictoBox(false);
        break;
    case RowKind::DeluxePictoBox:
        if (dir == 0) givePictoBox(true);
        break;
    case RowKind::Screenshot:
        if (dir == 0) {
            m.pendingShot = true;
        }
        break;
    case RowKind::Reload:
        if (dir == 0) {
            pc_settings_reload();
            toast(std::string(T("Settings reloaded from ")) + pc_settings_path());
        }
        break;
    }
}

// Pressed this frame, or held long enough to repeat (directions only).
uint32_t pressed(uint32_t keys) {
    uint32_t edge = keys & ~m.prevKeys;
    const uint32_t dirs = keys & (kUp | kDown | kLeft | kRight);
    if (dirs != 0 && dirs == m.repeatKey) {
        m.repeatFrames++;
        if (m.repeatFrames >= 10 && (m.repeatFrames - 10) % 3 == 0) {
            edge |= dirs;
        }
    } else {
        m.repeatKey = dirs;
        m.repeatFrames = 0;
    }
    m.prevKeys = keys;
    return edge;
}

void selectTab(int tab) {
    if (m.tab == tab) return;
    m.tab = tab;
    m.warpPage = false;
    m.toolsPage = false;
    m.scrollToRow = true;
}

constexpr double kDemoIntroSeconds = 2.0;
constexpr double kDemoEndPauseSeconds = 1.0;

// A short presentation tour, driven by UI time while gameplay is paused.
bool menuDemoFrame(uint32_t keys) {
    if (!m.demo) return false;
    if (!m.demoReady) {
        if (keys == 0) {
            m.demoReady = true;
            m.demoStarted = ImGui::GetTime();
            setOpen(false);
        }
        return true;
    }
    if (keys) {
        m.demo = false;
        m.waitRelease = true; // The cancelling press must not activate a setting.
        return true;
    }
    constexpr double secondsPerTab = 3.75;
    const double elapsed = ImGui::GetTime() - m.demoStarted - kDemoIntroSeconds;
    if (elapsed < 0.) return true;
    if (elapsed >= secondsPerTab * PC_SETTING_TABS + kDemoEndPauseSeconds) {
        m.demo = false;
        setOpen(false);
        return true;
    }
    if (!m.open) setOpen(true);
    if (elapsed >= secondsPerTab * PC_SETTING_TABS) return true;
    const int tab = int(elapsed / secondsPerTab);
    selectTab(tab);
    const auto rows = rowsOf(tab);
    // Hold the top briefly, sweep the rows, then hold the last row before switching.
    const float progress = std::clamp(float((elapsed - tab * secondsPerTab - .65) / 2.6), 0.f, 1.f);
    const int row = rows.empty() ? 0 : int(progress * (rows.size() - 1));
    if (m.row[tab] != row) {
        m.row[tab] = row;
        m.scrollToRow = true;
    }
    return true;
}

void handleInput(uint32_t keys) {
    const uint32_t p = pressed(keys);
    if (m.waitRelease) {
        if ((keys & ~(kUp | kDown | kLeft | kRight)) == 0) {
            m.waitRelease = false;
        }
        return;
    }
    if (!m.open) {
        if (p & kCombo) {
            setOpen(true);
        }
        return;
    }
    if (p & kCombo) {
        setOpen(false);
        return;
    }
    if (p & (kTabLeft | kTabRight)) {
        selectTab((m.tab + ((p & kTabRight) ? 1 : PC_SETTING_TABS - 1)) % PC_SETTING_TABS);
        return;
    }
    if (m.warpPage) {
        const int count = kMainWarpCount + kAllWarpCount;
        if (p & kBack) {
            m.warpPage = false;
            m.scrollToRow = true;
            return;
        }
        if (p & (kUp | kDown | kLeft | kRight)) {
            const int step = (p & kLeft) ? -10 : (p & kRight) ? 10 : (p & kUp) ? -1 : 1;
            m.warpRow = std::max(0, std::min(count - 1, m.warpRow + step));
            m.scrollToRow = true;
        }
        if (p & kConfirm) {
            doWarp(warpAt(m.warpRow));
        }
        return;
    }
    if (p & kBack) {
        if (m.tab == PC_SETTING_TAB_DEBUG && m.toolsPage) {
            m.toolsPage = false; m.scrollToRow = true; return;
        }
        setOpen(false);
        return;
    }
    const std::vector<Row> rows = rowsOf(m.tab);
    if (rows.empty()) {
        return;
    }
    int& row = selectedMenuRow(m.tab);
    row = std::min(row, (int)rows.size() - 1);
    if (p & kUp) {
        row = (row + (int)rows.size() - 1) % (int)rows.size();
        m.scrollToRow = true;
    }
    if (p & kDown) {
        row = (row + 1) % (int)rows.size();
        m.scrollToRow = true;
    }
    if (p & kConfirm) {
        activate(rows[row], rows[row].kind == RowKind::Setting ? 1 : 0);
    }
    if (p & (kLeft | kRight)) {
        const Row& r = rows[row];
        if (r.kind == RowKind::Setting) {
            activate(r, (p & kLeft) ? -1 : 1);
        }
    }
}

// ---- drawing -------------------------------------------------------------------------------------

const ImVec4 kAccent = ui::accent;
const ImVec4 kDim = ui::muted;
const ImVec4 kWarn(1.00f, 0.55f, 0.45f, 1.0f);

std::string rowLabel(const Row& r) {
    switch (r.kind) {
    case RowKind::MenuDemo: return T("Menu demo");
    case RowKind::RestoreDefaults: return T("Restore defaults");
    case RowKind::DeveloperTools: return T("Developer tools…");
    case RowKind::Warp: return T("Travel to a stage...");
    case RowKind::Sailing: return T("Set up sailing");
    case RowKind::PictoBox: return T("Give Picto Box");
    case RowKind::DeluxePictoBox: return T("Give Deluxe Picto Box");
    case RowKind::Screenshot: return T("Take a screenshot");
    case RowKind::Reload: return T("Reload saved settings");
    case RowKind::Setting: break;
    }
    std::string s = settingLabel(r.desc);
    return s;
}

std::string rowValue(const Row& r) {
    switch (r.kind) {
    case RowKind::MenuDemo: return T("Start");
    case RowKind::RestoreDefaults: return T("Restore");
    case RowKind::DeveloperTools: return T("Open");
    case RowKind::Warp: return inPlay() ? T("Choose") : T("in game only");
    case RowKind::PictoBox:
    case RowKind::DeluxePictoBox:
    case RowKind::Sailing: return inPlay() ? T("Apply") : T("in game only");
    case RowKind::Screenshot: return T("Capture");
    case RowKind::Reload: return T("Reload");
    case RowKind::Setting: break;
    }
    if (r.desc == &kFxPresetRow) return choiceLabel(r.desc, fxPresetValue());
    return choiceLabel(r.desc, pc_settings_get_mode(r.desc->key, rowMode(r.desc)));
}

const char* inactiveNote(const PcSettingDesc* d) {
    if (!d) return "";
    if (strcmp(d->key, "COS_HD_MAX_SIZE") == 0 && strcmp(pc_settings_get("COS_HD_TEXTURES"), "1") != 0)
        return T("HD textures are off");
    if (strncmp(d->key, "COS_FPS_OVERLAY_", 16) == 0 && strcmp(pc_settings_get("COS_FPS_OVERLAY"), "1") != 0)
        return T("FPS counter is off");
    if (strcmp(d->key, "COS_HAPTICS_STRENGTH") == 0 && strcmp(pc_settings_get("COS_HAPTICS"), "enhanced") != 0)
        return T("Enhanced vibration only");
    return "";
}

std::string rowNote(const Row& r) {
    if (r.kind != RowKind::Setting) {
        return "";
    }
    const PcSettingDesc* d = r.desc;
    if (pc_settings_locked(d->key)) {
        return T("Locked at launch");
    }
    if (d->flags & PC_SETTING_RESTART) {
        return pc_settings_restart_pending(d->key) ? T("Restart pending")
                                                   : T("Restart required");
    }
    if (strcmp(d->key, "COS_HD_TEXTURES") == 0 && !pc_settings_has_subscriber(d->key)) {
        return T("loader not included yet");
    }
    return inactiveNote(d);
}

std::string rowHelp(const Row& r) {
    switch (r.kind) {
    case RowKind::MenuDemo: return T("Shows the touchpad hint, tours the tabs, then closes the menu. Menu controls stop the demo. Settings stay unchanged.");
    case RowKind::RestoreDefaults: return T("Deletes the settings file and restores default options. Saves and shader caches are kept. Restart the game afterwards.");
    case RowKind::DeveloperTools: return T("Diagnostics, logs and test actions. Some actions change game progress.");
    case RowKind::Warp:
        return T("Moves Link to the selected location.");
    case RowKind::Sailing:
        return T("Gives Link the boat, sail and Wind Waker, then moves him to sea near Windfall. Save the game to keep these changes.");
    case RowKind::PictoBox:
        return T("Gives the regular Picto Box, replacing Deluxe if owned. Save normally to keep it.");
    case RowKind::DeluxePictoBox:
        return T("Adds or upgrades to the Deluxe Picto Box. Equip it from Items. Save normally to keep it.");
    case RowKind::Screenshot:
        return T("Saves the game's image (without the menu) as shot-<frame>.png in the game's folder.");
    case RowKind::Reload:
        return std::string(T("Reads ")) + pc_settings_path() + T(" again.");
    case RowKind::Setting: break;
    }
    const char* descHelp = settingHelp(r.desc);
    std::string help = descHelp != nullptr ? descHelp : "";
    return help;
}

bool gameplayOptionRow(const Row& r, bool selected, bool locked, const ImVec4& dim) {
    const auto label = rowLabel(r);
    auto value = rowValue(r);
    return pc_ps5_gameplay_option_row(label.c_str(), value.c_str(),
        r.kind == RowKind::DeveloperTools,
        selected, locked, dim, kAccent, r.kind == RowKind::Setting);
}

// Presentation only: use UI time because game frames stop while the menu is open.
float menuEase(double startedAt, float duration) {
    const float t = std::clamp(float((ImGui::GetTime() - startedAt) / duration), 0.f, 1.f);
    return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
}

void scrollSelectedRow(bool firstRow) {
    const float height = ImGui::GetWindowHeight();
    const float maxScroll = ImGui::GetScrollMaxY();
    const bool resized = height != m.scrollViewportHeight || maxScroll != m.scrollContentMax;
    m.scrollViewportHeight = height;
    m.scrollContentMax = maxScroll;
    if (!m.scrollToRow && !m.scrolling && !resized) return;

    // Keep a little breathing room at each edge, without recentering every row.
    const float margin = ImGui::GetFontSize() * .65f;
    const float top = ImGui::GetWindowPos().y + margin;
    const float bottom = ImGui::GetWindowPos().y + height - margin;
    const float current = ImGui::GetScrollY();
    float target = current;
    // The first selectable row follows a section heading (and sometimes a
    // breadcrumb). Returning to it must reveal the whole top of the screen.
    if (firstRow)
        target = 0.f;
    else if (ImGui::GetItemRectMin().y < top)
        target += ImGui::GetItemRectMin().y - top;
    else if (ImGui::GetItemRectMax().y > bottom)
        target += ImGui::GetItemRectMax().y - bottom;
    target = std::clamp(target, 0.f, maxScroll);
    const float delta = target - current;
    m.scrolling = std::abs(delta) > .5f;
    // Exponential easing remains consistent at 30/60 fps and can be retargeted
    // immediately while holding a direction or skipping through travel locations.
    const float blend = 1.f - std::exp(-24.f * ImGui::GetIO().DeltaTime);
    ImGui::SetScrollY(m.scrolling ? current + delta*blend : target);
}

void drawMenu() {
    ImGuiIO& io = ImGui::GetIO();
    const float opening = menuEase(m.openedAt, .16f);
    // Scale the controller menu from a 720-line reference layout.
    const float baseScale = std::max(1.0f, io.DisplaySize.y / 720.0f * 1.7f);
    const float scale = baseScale * sMenuTextScale;
    const ImVec4 kDim = sMenuHighContrast ? ImVec4(0.92f, 0.94f, 0.98f, 1.0f)
                                        : ui::muted;
    bool overlayLeft=false;
    const float inset=pc_profiler_menu_inset(&overlayLeft), margin=io.DisplaySize.x*.05f;
    const float left=overlayLeft ? std::max(margin,inset) : margin;
    const float right=io.DisplaySize.x-(overlayLeft ? margin : std::max(margin,inset));
    const ImVec2 size(std::min(right-left, 1100.f * io.DisplaySize.y / 720.f),
                      io.DisplaySize.y * 0.86f);
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0),io.DisplaySize,IM_COL32(0,8,14,int(80*opening*(sMenuHighContrast ? 1.f : sMenuOpacity))));
    ImGui::SetNextWindowPos(ImVec2((left+right)*.5f, io.DisplaySize.y * (0.5f + .012f*(1.f-opening))), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    pc_ps5_menu_style_begin(sMenuHighContrast, scale, sMenuOpacity);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (.15f + .85f*opening));
    ImGui::SetNextWindowBgAlpha(sMenuHighContrast ? 1.0f : sMenuOpacity);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin("##cos_options_menu", nullptr, kFlags)) {
        ImGui::End();
        ImGui::PopStyleVar();
        pc_ps5_menu_style_end();
        return;
    }
    static float previousScale;
    if (previousScale != scale) { m.scrollToRow = true; previousScale = scale; }
    ImGui::SetWindowFontScale(scale);
    ImGui::SetWindowFocus();

    pc_ps5_menu_heading(T("Options"));
    // Tabs.
    const float tabW = (ImGui::GetContentRegionAvail().x - (PC_SETTING_TABS - 1) * ImGui::GetStyle().ItemSpacing.x) / PC_SETTING_TABS;
    for (int t = 0; t < PC_SETTING_TABS; t++) {
        if (t > 0) {
            ImGui::SameLine();
        }
        if (pc_ps5_menu_tab(T(kTabNames[t]), t, t == m.tab, tabW)) selectTab(t);
    }
    ImGui::Spacing();

    const int page = m.tab*4 + (m.toolsPage ? 1 : 0) + (m.warpPage ? 2 : 0);
    if (page != m.displayedPage) {
        // Opening already animates the whole window. Only subsequent navigation
        // fades the content; repeated tab presses immediately select the latest page.
        m.pageChangedAt = m.displayedPage < 0 ? ImGui::GetTime() - .14 : ImGui::GetTime();
        m.displayedPage = page;
        m.scrolling = false;
        m.scrollToRow = true;
    }
    const float pageAlpha = .3f + .7f*menuEase(m.pageChangedAt, .14f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * pageAlpha);
    std::string help, helpTitle, helpNote;
    // Measure translated help before reserving its space. Short descriptions keep
    // more settings visible; long ones and large text get a taller panel.
    const auto previewRows=rowsOf(m.tab);
    if (m.warpPage) {
        const auto& warp=warpAt(m.warpRow);
        helpTitle=warp.label ? T(warp.label) : warp.stage;
        help=T("Up/down: choose. Left/right: skip 10 locations.");
    } else if (!previewRows.empty()) {
        const auto& selected=previewRows[std::clamp(selectedMenuRow(m.tab),0,int(previewRows.size())-1)];
        helpTitle=rowLabel(selected);
        help=rowHelp(selected);
        helpNote=rowNote(selected);
    }
    const bool hasToast=!m.toast.empty() && pc_frame_count()<m.toastUntil;
    const auto& measuredNote=hasToast ? m.toast : helpNote;
    const float fontSize=ImGui::GetFontSize();
    const float helpWidth=std::max(fontSize,ImGui::GetContentRegionAvail().x-fontSize*1.2f);
    const auto wrappedHeight=[helpWidth](const std::string& text,float size) {
        return text.empty() ? 0.f : pc_ps5_menu_text_height(text.c_str(),size,helpWidth);
    };
    const float measuredHeight=fontSize*.8f + ImGui::GetStyle().ItemSpacing.y*3.f +
        wrappedHeight(helpTitle,fontSize) + wrappedHeight(help,fontSize*.88f) + wrappedHeight(measuredNote,fontSize*.88f);
    const float helpHeight=std::clamp(measuredHeight,fontSize*4.2f,std::max(fontSize*4.2f,size.y*.36f));
    const float footer=helpHeight+fontSize*1.8f+ImGui::GetStyle().ItemSpacing.y*3.f;
    // Each screen retains its own scroll position instead of inheriting the
    // previous screen's offset (especially the much longer travel list).
    ImGui::PushID(page);
    if (ImGui::BeginChild("##rows", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_NoNav)) {
        // Child windows scale their text by their parent's font scale already.
        if (m.warpPage) {
            ImGui::TextColored(kAccent, "%s", T("Developer tools / Travel"));
            const int count = kMainWarpCount + kAllWarpCount;
            for (int i = 0; i < count; i++) {
                if (i == 0) {
                    pc_ps5_menu_section(T("Main places"));
                } else if (i == kMainWarpCount) {
                    char heading[160];
                    snprintf(heading,sizeof(heading),T("Every stage on the disc (%d)"),kAllWarpCount);
                    pc_ps5_menu_section(heading);
                }
                const Warp& w = warpAt(i);
                const char* label = w.label != nullptr ? T(w.label) : w.stage;
                char location[64];
                snprintf(location, sizeof(location), "%s %d:%d", w.stage, w.room, w.point);
                ImGui::PushID(i);
                if (pc_ps5_gameplay_option_row(label, location, false, i == m.warpRow, false, kDim, kAccent, false)) {
                    m.warpRow = i;
                    doWarp(w);
                }
                if (i == m.warpRow) scrollSelectedRow(i == 0);
                ImGui::PopID();
            }
            const Warp& selectedWarp = warpAt(m.warpRow);
            helpTitle = selectedWarp.label ? T(selectedWarp.label) : selectedWarp.stage;
            help = T("Up/down: choose. Left/right: skip 10 locations.");
        } else {
            if (m.tab == PC_SETTING_TAB_DEBUG && m.toolsPage) {
                ImGui::TextColored(kAccent, "%s", T("System / Developer tools"));
            }
            const std::vector<Row> rows = rowsOf(m.tab);
            int& row = selectedMenuRow(m.tab);
            row = rows.empty() ? 0 : std::min(row, (int)rows.size() - 1);
            int previousSection = -1;
            for (int i = 0; i < (int)rows.size(); i++) {
                const Row& r = rows[i];
                const int section = r.desc
                    ? (m.tab == PC_SETTING_TAB_GAMEPLAY ? 1 + int(pc_gameplay_menu_group(r.desc->key))
                                                       : pc_menu_setting_section(r.desc->key))
                    : (r.kind == RowKind::RestoreDefaults || r.kind == RowKind::DeveloperTools) ? int(PcMenuSection::Menu)
                    : (r.kind == RowKind::Warp || r.kind == RowKind::Sailing || r.kind == RowKind::PictoBox || r.kind == RowKind::DeluxePictoBox) ? int(PcMenuSection::TestActions) : int(PcMenuSection::Utilities);
                if (section && section != previousSection) {
                    static const char* names[] = {"", "Display", "Textures", "Camera and aiming", "Buttons", "Menu", "FPS counter", "Visual enhancements", "Vibration", "Test actions", "Utilities", "Logging", "Post-processing"};
                    if (previousSection >= 0) ImGui::Spacing();
                    pc_ps5_menu_section(m.tab == PC_SETTING_TAB_GAMEPLAY
                        ? gameplayGroupName(section-1) : T(names[section]));
                }
                previousSection = section;
                const bool locked = r.kind == RowKind::Setting && pc_settings_locked(r.desc->key);
                ImGui::PushID(i);
                if (gameplayOptionRow(r, i == row, locked, kDim)) {
                    if (row == i) activate(r, r.kind == RowKind::Setting ? 1 : 0);
                    row = i;
                }
                if (i == row) scrollSelectedRow(i == 0);
                ImGui::PopID();
            }
            if (!rows.empty()) {
                helpTitle = rowLabel(rows[row]);
                help = rowHelp(rows[row]);
                helpNote = rowNote(rows[row]);
            }
        }
        m.scrollToRow = false;
    }
    ImGui::EndChild();
    ImGui::PopID();
    ImVec4 helpBackground=ui::surface;
    helpBackground.w*=sMenuHighContrast ? 1.f : sMenuOpacity * sMenuOpacity;
    ImGui::PushStyleColor(ImGuiCol_ChildBg,helpBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(ImGui::GetFontSize()*.6f,ImGui::GetFontSize()*.4f));
    const bool helpVisible = ImGui::BeginChild("##help",ImVec2(0,helpHeight),ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoNav);
    if (helpVisible) {
        ImGui::SetWindowFontScale(1.f);
        ImGui::PushTextWrapPos(0.f);
        pc_ps5_menu_text(helpTitle.c_str(), kAccent);
        ImGui::SetWindowFontScale(.88f);
        if (!m.toast.empty() && pc_frame_count() < m.toastUntil)
            ImGui::TextColored(ui::warm,"%s",m.toast.c_str());
        else if (!helpNote.empty())
            ImGui::TextColored(ui::warm,"%s",helpNote.c_str());
        pc_ps5_menu_text(help.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(); // Page transition; footer remains steady.
    ImGui::Spacing();
    const auto currentRows = rowsOf(m.tab);
    const int currentRow = selectedMenuRow(m.tab);
    const char* action = T("Choose");
    if (m.warpPage) action = T("Travel");
    else if (currentRow >= 0 && currentRow < int(currentRows.size())) {
        const auto kind = currentRows[currentRow].kind;
        if (kind == RowKind::Setting) action = T("Change");
        else if (kind == RowKind::DeveloperTools || kind == RowKind::Warp)
            action = T("Open");
        else if (kind == RowKind::Screenshot) action = T("Capture");
        else if (kind == RowKind::Reload) action = T("Reload");
        else if (kind == RowKind::RestoreDefaults) action = T("Restore");
        else if (kind == RowKind::MenuDemo) action = T("Start");
        else if (kind == RowKind::Sailing || kind == RowKind::PictoBox || kind == RowKind::DeluxePictoBox) action = T("Apply");
    }
    pc_ps5_menu_footer(action,
        T("Back"), T("Tabs"), T("Close"));
    ImGui::End();
    ImGui::PopStyleVar(); // Opening transition.
    pc_ps5_menu_style_end();
}

void saveShot(unsigned int frame, const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height, void*) {
    saveFramePng(frame, rgb, width, height);
}

} // namespace

bool menuBuiltinSetting(const char* key) {
    for (const PcSettingDesc& d : kBuiltins) {
        if (key != nullptr && strcmp(d.key, key) == 0) {
            return true;
        }
    }
    return false;
}

void menuInit() {
    if (m.initialized) {
        return;
    }
    m.initialized = true;
    for (const PcSettingDesc& d : kBuiltins) {
        // A module that registered the key already (e.g. the HD texture loader) keeps its row.
        bool known = false;
        for (int i = 0; i < pc_settings_count(); i++) {
            known = known || strcmp(pc_settings_at(i)->key, d.key) == 0;
        }
        if (!known) {
            pc_settings_register(&d);
        }
    }
    // Registration does not invoke callbacks. Camera getters cache environment
    // values, which do not include descriptor defaults on a fresh configuration.
    // Adopt the resolved settings before the first camera update.
    applyCameraInvertX(nullptr, pc_settings_get("COS_CAMERA_INVERT_X"), nullptr);
    applyCameraInvertY(nullptr, pc_settings_get("COS_CAMERA_INVERT_Y"), nullptr);
    applyCameraShake(nullptr, pc_settings_get("COS_CAMERA_SHAKE"), nullptr);
    applyHeatHaze(nullptr, pc_settings_get("COS_HEAT_HAZE"), nullptr);
    applyDof(nullptr, pc_settings_get("COS_DOF"), nullptr);
    // Overlay state also needs explicit adoption before its first frame.
    applyOverlay(nullptr, pc_settings_get("COS_FPS_OVERLAY"), nullptr);
    applyOverlayDetail(nullptr, pc_settings_get("COS_FPS_OVERLAY_DETAIL"), nullptr);
    applyOverlayPosition(nullptr, pc_settings_get("COS_FPS_OVERLAY_POSITION"), nullptr);
    applyOverlaySize(nullptr, pc_settings_get("COS_FPS_OVERLAY_SIZE"), nullptr);
    applyOverlayBehindMenu(nullptr, pc_settings_get("COS_FPS_OVERLAY_BEHIND_MENU"), nullptr);
    applyMenuLanguage(nullptr, pc_settings_get("COS_MENU_LANGUAGE"), nullptr);
    applyMenuTextSize(nullptr, pc_settings_get("COS_MENU_TEXT_SIZE"), nullptr);
    applyMenuTransparency(nullptr, pc_settings_get("COS_MENU_TRANSPARENCY"), nullptr);
    applyMenuContrast(nullptr, pc_settings_get("COS_MENU_HIGH_CONTRAST"), nullptr);
    menuSmokeInit();
    writef(STDERR_FILENO, "[cos] menu: %d settings; open with %s; settings file %s\n", pc_settings_count(),
           "Touch pad", pc_settings_path());
}

void menuSmokeState(int* tab, int* row, int* capture) {
    if (tab) *tab = m.tab;
    if (row) *row = selectedMenuRow(m.tab);
    if (capture) *capture = -1;
}

bool menuOpen() {
    return m.open;
}

// Draw-only overlay: called for every presentation, including interpolation frames.
void menuOverlayFrame() {
    if (!m.initialized || ImGui::GetCurrentContext() == nullptr) return;
    // Replay the same touchpad hint for the demo, on every presentation frame.
    if (m.demo && m.demoReady && !m.open) {
        const float elapsed = float(ImGui::GetTime() - m.demoStarted);
        const float fade = std::clamp(std::min((elapsed - .2f) / .25f,
            (float(kDemoIntroSeconds) - elapsed) / .3f), 0.f, 1.f);
        if (fade > 0.f) pc_ps5_menu_welcome(fade * fade * (3.f - 2.f * fade),
            sMenuHighContrast, sMenuTextScale);
        return;
    }
    // Once per launch, after the title/game scene is available.
    static bool welcomeDone;
    static double welcomeStart = -1.;
    if (m.open) welcomeDone = true;
    if (!welcomeDone) {
        const auto readyScene = [](void* proc, void*) -> void* {
            const auto name = fpcM_GetName(proc);
            return name == fpcNm_TITLE_e ||
                (name == fpcNm_PLAY_SCENE_e && dComIfGp_getLinkPlayer()) ? proc : nullptr;
        };
        if (welcomeStart < 0. && fpcM_Search(readyScene, nullptr)) welcomeStart = ImGui::GetTime();
        if (welcomeStart >= 0.) {
            const float elapsed = float(ImGui::GetTime() - welcomeStart) - 1.f;
            if (elapsed >= 6.f) welcomeDone = true;
            else if (elapsed >= 0.f) {
                const float fade = std::clamp(std::min(elapsed / .35f, (6.f - elapsed) / .6f), 0.f, 1.f);
                pc_ps5_menu_welcome(fade * fade * (3.f - 2.f * fade), sMenuHighContrast,
                    sMenuTextScale);
            }
        }
    }
}

void menuFrame() {
    if (!m.initialized || ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    // The COS_INPUT script drives the menu only in its own smoke test: other scripts press L+R+Z
    // for the game (pad-echo).
    const uint32_t keys = (menuSmokeEnabled() ? readScript() : 0) | readGamepads();
    if (!menuDemoFrame(keys)) handleInput(keys);
    if (!m.open) {
        return;
    }
    if (inPlay()) {
        // The hit-stop pause of the PLAY scene, renewed every frame while the menu is open
        // (dScnPly_Execute returns early while it runs; it ends two frames after the last renewal).
        dScnPly_ply_c::setPauseTimer(2);
    }
    drawMenu();
}

// ---- the Switch debug server (switch/native/source/cos_debug.cpp): its thread asks, the game thread acts
std::mutex sDebugMu;
struct DebugWarp {
    bool pending = false;
    char stage[8] = {};
    int room = 0, point = 0;
    int layer = -1;  // -1: the game's own choice (time of day, story)
} sDebugWarp;
// "event NAME": an event of the stage's event list ordered as the event sweep orders it (the player as the
// ordering actor, pc_event_sweep.cpp), every frame until an event runs, kDebugEventFrames at most. For
// testing a cutscene without a save at that point (issue #3: warp Pjavdou 0 1 8, then event getperl_jab).
constexpr unsigned int kDebugEventFrames = 240;
struct DebugEvent {
    bool pending = false;
    char name[32] = {};
    unsigned int frames = 0;
} sDebugEvent;
struct DebugStatus {
    unsigned int frame = 0;
    bool play = false;
    char stage[8] = {};
    int room = -1;
} sDebugStatus;

void debugFrameEnd(unsigned int frame) {
    const bool play = inPlay();
    std::lock_guard<std::mutex> lk(sDebugMu);
    sDebugStatus.frame = frame;
    sDebugStatus.play = play;
    snprintf(sDebugStatus.stage, sizeof(sDebugStatus.stage), "%s", play ? dComIfGp_getStartStageName() : "");
    sDebugStatus.room = play ? dComIfGp_getStartStageRoomNo() : -1;
    // a warp waits for the game (a file being played) and for no scene change in progress
    if (sDebugWarp.pending && play && !dComIfGp_isEnableNextStage()) {
        sDebugWarp.pending = false;
        setOpen(false);
        writef(STDERR_FILENO, "[cos] debug server: warp to %s room %d point %d layer %d\n", sDebugWarp.stage,
               sDebugWarp.room, sDebugWarp.point, sDebugWarp.layer);
        dComIfGp_setNextStage(sDebugWarp.stage, (s16)sDebugWarp.point, (s8)sDebugWarp.room, (s8)sDebugWarp.layer);
    }
    if (sDebugEvent.pending && play && !dComIfGp_isEnableNextStage() && dComIfGp_getPlayer(0) != nullptr) {
        if (sDebugEvent.frames > 0 && dComIfGp_event_runCheck()) {
            sDebugEvent.pending = false;
            writef(STDERR_FILENO, "[cos] debug server: event %s running\n", sDebugEvent.name);
        } else if (sDebugEvent.frames++ >= kDebugEventFrames) {
            sDebugEvent.pending = false;
            writef(STDERR_FILENO, "[cos] debug server: event %s did not start\n", sDebugEvent.name);
        } else {
            const s16 idx = dComIfGp_evmng_getEventIdx(sDebugEvent.name);
            if (idx < 0) {
                sDebugEvent.pending = false;
                writef(STDERR_FILENO, "[cos] debug server: no event %s in this stage's list\n", sDebugEvent.name);
            } else {
                // from the play scene's layer, as the sweep (a process made from the root layer is drawn twice)
                layer_class* saved = fpcLy_CurrentLayer();
                fpcLy_SetCurrentLayer(&static_cast<process_node_class*>((void*)fpcM_Search(isPlayScene, nullptr))->mLayer);
                fopAcM_orderOtherEventId(dComIfGp_getPlayer(0), idx, 0xFF, 0xFFFF, 1);
                fpcLy_SetCurrentLayer(saved);
            }
        }
    }
}

void menuFrameEnd(unsigned int frame) {
    debugFrameEnd(frame);
    if (m.pendingShot) {
        m.pendingShot = false;
        if (captureFrame(frame, saveShot, nullptr)) {
            char name[64];
            snprintf(name, sizeof(name), T("Screenshot saved: shot-%06u.png"), frame);
            toast(name);
        } else {
            toast(T("Could not take the screenshot"));
        }
    }
    if (menuSmokeEnabled()) {
        menuSmokeFrame(frame, m.open, m.opened, inPlay());
    }
}

} // namespace pc

// The warp list, numbered as the menu's Travel page: "n  stage room point  label" lines.
extern "C" int pc_debug_warp_list(char* out, size_t size) {
    size_t used = 0;
    for (int i = 0; i < pc::kMainWarpCount + pc::kAllWarpCount && used < size; i++) {
        const pc::Warp& w = pc::warpAt(i);
        const int n = snprintf(out + used, size - used, "%3d  %-8s room %2d point %3d  %s\n", i, w.stage, w.room, w.point,
                               w.label != nullptr ? w.label : "");
        if (n < 0) {
            break;
        }
        used += (size_t)n;
    }
    return (int)std::min(used, size);
}

// A warp from the debug server: index >= 0 picks the list's entry, else stage / room / point / layer (-1: the
// game's choice; e.g. 8 for Jabun's cave with Jabun, Pjavdou 0 1 8). The game thread applies it once a file is
// being played (pc_menu debugFrameEnd). 0: no such entry.
extern "C" int pc_debug_warp(int index, const char* stage, int room, int point, int layer, char* done, size_t size) {
    pc::Warp w{stage, room, point, nullptr};
    if (index >= 0) {
        if (index >= pc::kMainWarpCount + pc::kAllWarpCount) {
            return 0;
        }
        w = pc::warpAt(index);
    }
    std::lock_guard<std::mutex> lk(pc::sDebugMu);
    pc::sDebugWarp.pending = true;
    snprintf(pc::sDebugWarp.stage, sizeof(pc::sDebugWarp.stage), "%s", w.stage);
    pc::sDebugWarp.room = w.room;
    pc::sDebugWarp.point = w.point;
    pc::sDebugWarp.layer = index >= 0 ? -1 : layer;
    snprintf(done, size, "%s room %d point %d layer %d", pc::sDebugWarp.stage, w.room, w.point, pc::sDebugWarp.layer);
    return 1;
}

// An event of the current stage's list, by name, from the debug server (see sDebugEvent). 0: name too long.
extern "C" int pc_debug_event(const char* name) {
    if (strlen(name) >= sizeof(pc::sDebugEvent.name)) {
        return 0;
    }
    std::lock_guard<std::mutex> lk(pc::sDebugMu);
    snprintf(pc::sDebugEvent.name, sizeof(pc::sDebugEvent.name), "%s", name);
    pc::sDebugEvent.frames = 0;
    pc::sDebugEvent.pending = true;
    return 1;
}

// The game's state at the last frame end: frame, in play, stage, room.
extern "C" int pc_debug_status(char* out, size_t size) {
    std::lock_guard<std::mutex> lk(pc::sDebugMu);
    return snprintf(out, size, "frame %u\nstage %s room %d%s\n", pc::sDebugStatus.frame,
                    pc::sDebugStatus.play ? pc::sDebugStatus.stage : "-", pc::sDebugStatus.room,
                    pc::sDebugStatus.play ? "" : " (not playing a file)");
}
