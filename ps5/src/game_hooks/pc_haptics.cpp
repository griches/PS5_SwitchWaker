#include "pc/pc_haptics.h"
#if defined(__PROSPERO__)
#include "pc/pc_settings.h"
#include "d/d_com_inf_game.h"
#include "JSystem/JUtility/JUTGamePad.h"
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_timer.h>
#include <cstring>
#include <cstdlib>
#include <mutex>
extern "C" SDL_Gamepad* PADGetSDLGamepadForIndex(u32 index);
namespace pc { bool menuOpen(); }
namespace {
enum Mode { Original, Enhanced, Off };
Mode mode = Original;
bool initialized, reset;
double strength = 0.8;
PcHapticShape shape;
std::mutex outputMutex;
PcHapticExpiry expiry;
SDL_JoystickID sentPad;
SDL_TimerID watchdog;
void stopLocked() {
    if (sentPad) if (auto* pad = SDL_GetGamepadFromID(sentPad)) SDL_RumbleGamepad(pad,0,0,0);
    expiry.deadline = 0;
    sentPad = 0;
}
Uint32 SDLCALL expire(void*, SDL_TimerID, Uint32 interval) {
    std::lock_guard lock(outputMutex);
    if (expiry.expired(SDL_GetTicksNS())) stopLocked();
    return interval;
}
void apply(const char* key, const char* value, void*) {
    if (std::strcmp(key,"COS_HAPTICS_STRENGTH") == 0) {
        char* end = nullptr;
        const double parsed = std::strtod(value,&end);
        strength = end != value && *end == '\0' && std::isfinite(parsed) ? std::clamp(parsed/100.0,0.0,1.0) : 0.8;
    } else mode = std::strcmp(value,"enhanced") == 0 ? Enhanced : std::strcmp(value,"off") == 0 ? Off : Original;
    reset = true;
}
const PcSettingChoice modes[] = {{"original", "Original"},{"enhanced", "Enhanced"},{"off", "Off"}};
const PcSettingChoice strengths[] = {{"0", "0%"},{"20", "20%"},{"40", "40%"},{"60", "60%"},{"80", "80%"},{"100", "100%"}};
const PcSettingDesc settings[] = {
    {"COS_HAPTICS","Controller vibration","Original uses the game's vibration. Enhanced adds stronger effects. The game's vibration setting must also be on.",PC_SETTING_TAB_GAMEPLAY,0,modes,3,"original",apply,nullptr,18},
    {"COS_HAPTICS_STRENGTH","Enhanced vibration strength","Changes vibration strength in Enhanced mode only.",PC_SETTING_TAB_GAMEPLAY,0,strengths,6,"80",apply,nullptr,19}
};
}
void pc_haptics_init() {
    if (initialized) return;
    initialized = true;
    for (const auto& setting : settings) {
        pc_settings_register(&setting);
        apply(setting.key,pc_settings_get(setting.key),nullptr);
    }
}
bool pc_haptics_forward_motor(int port) {
    pc_haptics_init();
    return port != 0 || (mode == Original && !pc::menuOpen());
}
void pc_haptics_tick() {
    pc_haptics_init();
    if (reset) {
        std::lock_guard lock(outputMutex);
        // Stop any native motor command when adopting a new mode.
        if (auto* pad=PADGetSDLGamepadForIndex(0)) SDL_RumbleGamepad(pad,0,0,0);
        stopLocked();
        shape = {};
        reset = false;
    }
    if (mode != Enhanced || pc::menuOpen()) {
        std::lock_guard lock(outputMutex);
        stopLocked();
        if ((mode == Off || pc::menuOpen())) if (auto* pad=PADGetSDLGamepadForIndex(0)) SDL_RumbleGamepad(pad,0,0,0);
        return;
    }
    if (!watchdog) watchdog = SDL_AddTimer(20,expire,nullptr);
    // Do not send an unbounded effect if the independent safety timer failed.
    if (!watchdog) return;
    const auto& vibration = dComIfGp_getVibration();
    const auto& shock = vibration.mMotor.mShock;
    const auto& quake = vibration.mMotor.mQuake;
    const bool allowed = vibration.mRumbleState == dVibration_c::RUMBLE_STATE_RUNNING &&
        dComIfGs_checkOptVibration() == 1 &&
        (JUTGamePad::sRumbleSupported & JUTGamePad::CRumble::mEnabled & 0x80000000u);
    const PcHapticSample sample = {vibration.mFrameIdx,allowed,
        {shock.mPatternIdx,shock.mCurrentFrame,shock.mLength,shock.mRounds,shock.mPattern},
        {quake.mPatternIdx,quake.mCurrentFrame,quake.mLength,quake.mRounds,quake.mPattern}};
    const auto now = SDL_GetTicksNS();
    auto* pad = PADGetSDLGamepadForIndex(0);
    const auto level = shape.update(sample,now,pad == nullptr,strength);
    std::lock_guard lock(outputMutex);
    if (!pad) { stopLocked(); return; }
    const auto id = SDL_GetGamepadID(pad);
    if (sentPad && sentPad != id) stopLocked();
    const auto low = static_cast<Uint16>(std::lround(level.heavy*65535));
    const auto high = static_cast<Uint16>(std::lround(level.light*65535));
    if (low || high) {
        if (SDL_RumbleGamepad(pad,low,high,120)) { sentPad = id; expiry.arm(now); }
        else stopLocked();
    } else stopLocked();
}
#endif
