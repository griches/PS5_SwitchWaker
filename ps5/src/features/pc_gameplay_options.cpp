#include "pc/pc_gameplay_options.h"

#if defined(__PROSPERO__)
#include "pc/pc_settings.h"
#include <cstring>
#include <cstdlib>

namespace {
bool initialized;
float sprintMultiplier = 1.0f;
PcSailMode sailMode = PC_SAIL_ORIGINAL;
PcSailMode requestedSail = PC_SAIL_ORIGINAL;
bool enabled[PC_GAMEPLAY_OPTION_COUNT] = {};
bool requested[PC_GAMEPLAY_OPTION_COUNT] = {};
bool textEnabled[2] = {};
bool deferred(PcGameplayOption id) {
    return id == PC_GAMEPLAY_FASTER_MOVEMENT || id == PC_GAMEPLAY_FASTER_TRAVERSAL ||
        id == PC_GAMEPLAY_FASTER_BLOCK_PUSH || id == PC_GAMEPLAY_FASTER_GRAPPLE ||
        id == PC_GAMEPLAY_FASTER_BALLAD || id == PC_GAMEPLAY_INSTANT_TEXT ||
        id == PC_GAMEPLAY_SKIP_INTRO || id == PC_GAMEPLAY_QUICK_DOORS;
}
void applySail(const char*, const char* value, void*) {
    requestedSail = std::strcmp(value, "1") == 0 ? PC_SAIL_SWIFT :
        std::strcmp(value, "2") == 0 ? PC_SAIL_BRISK : PC_SAIL_ORIGINAL;
}
const PcSettingChoice onOff[] = {{"0", "Off"}, {"1", "On"}};

void apply(const char* key, const char* value, void* user);
struct Option {
    PcGameplayOption id;
    const char* key;
    const char* title;
    const char* help;
    unsigned flags = 0;
};
const PcSettingChoice sailChoices[] = {
    {"0", "Original"}, {"1", "Swift"}, {"2", "Brisk"}
};
const PcSettingChoice sprintChoices[] = {
    {"0", "Off"}, {"1.1", "1.1x"}, {"1.2", "1.2x"},
    {"1.25", "1.25x"}, {"1.3", "1.3x"}, {"1.4", "1.4x"},
    {"1.5", "1.5x"}, {"1.6", "1.6x"}, {"1.7", "1.7x"},
    {"1.75", "1.75x"}, {"1.8", "1.8x"}, {"1.9", "1.9x"},
    {"2", "2x"}
};
const Option options[] = {
    {PC_GAMEPLAY_FAST_LOADING, "COS_FAST_LOADING", "Faster loading",
     "Speeds up loading while the screen is hidden. Keeps your chosen fade speed."},
    {PC_GAMEPLAY_QUICK_DOORS, "COS_QUICK_DOORS", "Quick doors",
     "Speeds up walking through doors with handles. Does not affect dungeon doors. Applies to the next door."},
    {PC_GAMEPLAY_JUMP_BUTTON, "COS_JUMP_BUTTON", "Jump button (L1)",
     "Press L1 to jump. Unavailable while carrying items, targeting, wearing Iron Boots or during events."},
    {PC_GAMEPLAY_SPRINT, "COS_SPRINT_SPEED", "Sprint speed (L3)",
     "Press L3 to start or stop sprinting. Stops when you release the stick. Jumping does not stop sprinting."},
    {PC_GAMEPLAY_DPAD_MENUS, "COS_DPAD_MENUS", "D-pad inventory navigation",
     "Use the D-pad in Items and Quest Status. The left stick takes priority."},
    {PC_GAMEPLAY_SHORT_FADES, "COS_SHORT_FADES", "Shorter scene fades",
     "Makes scene fades shorter. Applies to the next fade."},
    {PC_GAMEPLAY_INSTANT_TEXT, "COS_BWW_INSTANT_TEXT", "Instant text",
     "Next conversation: instant text, no text waits. Hold Square to advance or cancel where allowed."},
    {PC_GAMEPLAY_FASTER_MOVEMENT, "COS_BWW_FASTER_MOVEMENT", "Faster rolling and crawling",
     "Faster roll starts and double crawl speed. Applies at the next player action."},
    {PC_GAMEPLAY_FASTER_GRAPPLE, "COS_BWW_FASTER_GRAPPLE", "Faster grappling hook",
     "Faster throws, target drop and wrapping. Applies when the grapple returns to its ready state."},
    {PC_GAMEPLAY_FASTER_BLOCK_PUSH, "COS_BWW_FASTER_BLOCK_PUSH", "Faster block pushing",
     "Faster block pushing/pulling. Release the block and start again to apply a change."},
    {PC_GAMEPLAY_FASTER_TRAVERSAL, "COS_BWW_FASTER_TRAVERSAL", "Faster climbing and conversation zoom",
     "Faster climbing, sidling and conversation zoom. Applies at the next player action."},
    {PC_GAMEPLAY_TINGLE_CHESTS, "COS_BWW_TINGLE_CHESTS", "Tingle Chests without the Tingle Tuner",
     "Applies immediately: ordinary bombs reveal chests; the compass shows them. Chest progress is saved."},
    {PC_GAMEPLAY_UNRESTRICTED_BOAT, "COS_BWW_UNRESTRICTED_BOAT", "Unrestricted boat",
     "Lets you sail beyond story limits early. You may reach areas out of order."},
    {PC_GAMEPLAY_NO_SONG_REPLAYS, "COS_BWW_NO_SONG_REPLAYS", "Skip song replays",
     "Skips the replay after you play a song. The song still takes effect."},
    {PC_GAMEPLAY_SWING_TURN, "COS_BWW_SWING_TURN", "Turn while rope swinging",
     "The left stick turns Link while swinging on a rope."},
    {PC_GAMEPLAY_SKIP_INTRO, "COS_BWW_SKIP_INTRO", "Skip opening movie",
     "Skips the opening legend next time it starts. Applies to new games and title replays."},
    {PC_GAMEPLAY_FASTER_BALLAD, "COS_BWW_FASTER_BALLAD", "Faster Ballad of Gales",
     "Skips the ascent wait and warp music on the next Ballad of Gales warp."},
    {PC_GAMEPLAY_REVEAL_SEA_CHART, "COS_BWW_REVEAL_SEA_CHART", "Reveal sea chart (new games)",
     "Starts new games with the sea chart revealed. Does not change existing saves."},
    {PC_GAMEPLAY_SEA_COMPASS, "COS_BWW_SEA_COMPASS", "Correct sea compass",
     "Corrects east and west on the boat compass."},
    {PC_GAMEPLAY_KEEP_WATERED_TREES, "COS_FOREST_WATER_KEEP_TREES", "Keep watered trees",
     "Keeps trees watered after Forest Water expires. Save normally to keep your progress. Turn off to restore normal wilting."},
    {PC_GAMEPLAY_FOREST_WATER_30_MINUTES, "COS_FOREST_WATER_30_MINUTES", "30-minute Forest Water",
     "The next refill lasts 30 minutes instead of 20. Does not change the current timer."},
};
void apply(const char* key, const char* value, void*) {
    for (const Option& option : options) {
        if (std::strcmp(key, option.key) == 0) {
            if (option.id == PC_GAMEPLAY_SPRINT) {
                char* end = nullptr;
                const float factor = std::strtof(value, &end);
                sprintMultiplier = end != value && *end == '\0' && factor >= 1.1f && factor <= 2.0f
                    ? factor : 1.0f;
                requested[option.id] = sprintMultiplier > 1.0f;
            } else requested[option.id] = std::strcmp(value, "1") == 0;
            if (!deferred(option.id)) enabled[option.id] = requested[option.id];
            return;
        }
    }
}
}

void pc_gameplay_options_init() {
    if (initialized) return;
    initialized = true;
    PcSettingDesc sail = {};
    sail.key = "COS_BWW_SAIL";
    sail.label = "Sail";
    sail.help = "Swift: double speed and following wind. Brisk: stronger braking. Lower and raise the sail to apply.";
    sail.tab = PC_SETTING_TAB_GAMEPLAY;
    sail.apply = applySail;
    sail.choices = sailChoices;
    sail.choiceCount = 3;
    sail.defaultValue = "1";
    pc_settings_register(&sail);
    applySail(sail.key, pc_settings_get(sail.key), nullptr);
    sailMode = requestedSail;
    for (const Option& option : options) {
        PcSettingDesc desc = {};
        desc.key = option.key;
        desc.label = option.title;
        desc.help = option.help;
        desc.tab = PC_SETTING_TAB_GAMEPLAY;
        desc.choices = option.id == PC_GAMEPLAY_SPRINT ? sprintChoices : onOff;
        desc.choiceCount = option.id == PC_GAMEPLAY_SPRINT ?
            int(sizeof(sprintChoices) / sizeof(sprintChoices[0])) : 2;
        desc.defaultValue = option.id == PC_GAMEPLAY_SPRINT ? "1.3" :
            (option.id == PC_GAMEPLAY_SKIP_INTRO || option.id == PC_GAMEPLAY_REVEAL_SEA_CHART ||
             option.id == PC_GAMEPLAY_UNRESTRICTED_BOAT) ? "0" : "1";
        desc.flags = option.flags;
        desc.apply = apply;
        pc_settings_register(&desc);
        apply(option.key, pc_settings_get(option.key), nullptr);
        enabled[option.id] = requested[option.id];
    }
}

float pc_gameplay_sprint_multiplier() { pc_gameplay_options_init(); return sprintMultiplier; }

void pc_gameplay_begin_action(PcGameplayOption option) {
    pc_gameplay_options_init();
    if (option >= 0 && option < PC_GAMEPLAY_OPTION_COUNT) enabled[option] = requested[option];
}
void pc_gameplay_begin_sailing() {
    pc_gameplay_options_init();
    sailMode = requestedSail;
}
void pc_gameplay_begin_text(PcTextSystem system) {
    pc_gameplay_options_init();
    if (system == PC_TEXT_NORMAL || system == PC_TEXT_DEMO)
        textEnabled[system] = requested[PC_GAMEPLAY_INSTANT_TEXT];
}
bool pc_gameplay_text_enabled(PcTextSystem system) {
    return (system == PC_TEXT_NORMAL || system == PC_TEXT_DEMO) && textEnabled[system];
}
bool pc_gameplay_enabled(PcGameplayOption option) {
    pc_gameplay_options_init();
    return option >= 0 && option < PC_GAMEPLAY_OPTION_COUNT && enabled[option];
}
PcSailMode pc_gameplay_sail_mode() {
    pc_gameplay_options_init();
    return sailMode;
}
#else
// Other native targets retain their existing menu and gameplay behavior.
float pc_gameplay_sprint_multiplier() { return 1.0f; }
void pc_gameplay_options_init() {}
void pc_gameplay_begin_action(PcGameplayOption) {}
void pc_gameplay_begin_sailing() {}
void pc_gameplay_begin_text(PcTextSystem) {}
bool pc_gameplay_text_enabled(PcTextSystem) { return false; }
PcSailMode pc_gameplay_sail_mode() { return PC_SAIL_ORIGINAL; }
bool pc_gameplay_enabled(PcGameplayOption) { return false; }
#endif
