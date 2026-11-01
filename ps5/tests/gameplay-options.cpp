// Focused host checks; compile with the PS5 gameplay module and __PROSPERO__.
#include "pc/pc_gameplay_options.h"
#include "pc/pc_gameplay_menu.h"
#include "pc/pc_settings.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <map>
#include <string>

static std::map<std::string, PcSettingDesc> descriptions;
static std::map<std::string, std::string> values;
extern "C" int pc_settings_register(const PcSettingDesc* desc) {
    assert(desc->tab == PC_SETTING_TAB_GAMEPLAY);
    const bool sail = std::string(desc->key) == "COS_BWW_SAIL";
    const bool sprint = std::string(desc->key) == "COS_SPRINT_SPEED";
    assert(desc->choiceCount == (sail ? 3 : sprint ? 13 : 2));
    assert(desc->flags == 0);
    const bool enabledDefault = std::string(desc->key) != "COS_BWW_SKIP_INTRO" &&
        std::string(desc->key) != "COS_BWW_REVEAL_SEA_CHART" &&
        std::string(desc->key) != "COS_BWW_UNRESTRICTED_BOAT";
    assert(std::string(desc->defaultValue) == (sprint ? "1.3" : enabledDefault ? "1" : "0"));
    assert(desc->label && desc->help);
    assert(desc->apply != nullptr);
    descriptions[desc->key] = *desc;
    if (!values.count(desc->key)) values[desc->key] = desc->defaultValue;
    return 0;
}
extern "C" const char* pc_settings_get(const char* key) { return values[key].c_str(); }

int main(int argc, char** argv) {
    // A second process checks settings restored before menu registration.
    const bool restored = argc > 1;
    if (restored) values["COS_BWW_SEA_COMPASS"] = "1";
    if (restored) values["COS_BWW_SAIL"] = argv[1];
    const bool movementBoot = restored && std::string(argv[1]) == "movement";
    const bool grappleBoot = restored && std::string(argv[1]) == "grapple";
    const bool loadingBoot = restored && std::string(argv[1]) == "loading";
    if (loadingBoot) values["COS_FAST_LOADING"] = "1";
    const bool doorsBoot = restored && std::string(argv[1]) == "doors";
    if (doorsBoot) values["COS_QUICK_DOORS"] = "1";
    const bool jumpBoot = restored && std::string(argv[1]) == "jump";
    const bool sprintBoot = restored && std::string(argv[1]) == "sprint";
    if (jumpBoot) values["COS_JUMP_BUTTON"] = "0"; // Saved Off overrides the new default.
    if (sprintBoot) values["COS_SPRINT_SPEED"] = "1.5";
    const bool waterBoot = restored && std::string(argv[1]) == "water";
    if (waterBoot) {
        values["COS_FOREST_WATER_KEEP_TREES"] = "1";
        values["COS_FOREST_WATER_30_MINUTES"] = "1";
    }
    const bool textBoot = restored && std::string(argv[1]) == "text";
    if (textBoot) values["COS_BWW_INSTANT_TEXT"] = "1";
    const bool introBoot = restored && std::string(argv[1]) == "intro";
    if (introBoot) values["COS_BWW_SKIP_INTRO"] = "1";
    const bool balladBoot = restored && std::string(argv[1]) == "ballad";
    if (balladBoot) values["COS_BWW_FASTER_BALLAD"] = "1";
    const bool tingleBoot = restored && std::string(argv[1]) == "tingle";
    if (tingleBoot) values["COS_BWW_TINGLE_CHESTS"] = "1";
    const bool traversalBoot = restored && std::string(argv[1]) == "traversal";
    if (traversalBoot) values["COS_BWW_FASTER_TRAVERSAL"] = "1";
    if (movementBoot) values["COS_BWW_FASTER_MOVEMENT"] = "1";
    if (grappleBoot) values["COS_BWW_FASTER_GRAPPLE"] = "1";
    pc_gameplay_options_init();
    pc_gameplay_options_init();
    const PcSailMode expectedSail = (!restored || std::string(argv[1]) == "1") ? PC_SAIL_SWIFT :
        restored && std::string(argv[1]) == "2" ? PC_SAIL_BRISK : PC_SAIL_ORIGINAL;
    assert(pc_gameplay_sail_mode() == expectedSail);
    values["COS_BWW_SAIL"] = "2";
    // Reinitializing does not change an active boat; only its action boundary does.
    pc_gameplay_options_init();
    assert(pc_gameplay_sail_mode() == expectedSail);
    assert(descriptions.size() == PC_GAMEPLAY_OPTION_COUNT + 1);
    int groupCounts[PC_GAMEPLAY_MENU_GROUP_COUNT] = {};
    for (const auto& entry : descriptions) {
        const int group = pc_gameplay_menu_group(entry.first.c_str());
        assert(group >= 0 && group < PC_GAMEPLAY_MENU_GROUP_COUNT);
        ++groupCounts[group];
    }
    // Camera rows expose inversion controls; R3 is always available.
    for (const char* key : {"COS_CAMERA_INVERT_X", "COS_CAMERA_INVERT_Y"}) {
        assert(pc_gameplay_menu_group(key) == PC_GAMEPLAY_MENU_CAMERA);
        ++groupCounts[PC_GAMEPLAY_MENU_CAMERA];
    }
    assert(groupCounts[PC_GAMEPLAY_MENU_CAMERA] == 2);
    for (const char* key : {"COS_CAMERA_SHAKE", "COS_HEAT_HAZE"})
        assert(pc_gameplay_menu_group(key) == PC_GAMEPLAY_MENU_COMFORT);
    assert(groupCounts[PC_GAMEPLAY_MENU_MOVEMENT] == 8);
    assert(groupCounts[PC_GAMEPLAY_MENU_SAILING] == 4);
    assert(groupCounts[PC_GAMEPLAY_MENU_PACING] == 3);
    assert(groupCounts[PC_GAMEPLAY_MENU_TRANSITIONS] == 4);
    assert(groupCounts[PC_GAMEPLAY_MENU_EXPLORATION] == 3);
    assert(pc_gameplay_menu_group("FUTURE_SETTING") == PC_GAMEPLAY_MENU_MOVEMENT);
    assert(pc_gameplay_menu_group(nullptr) == PC_GAMEPLAY_MENU_MOVEMENT);
    assert(pc_gameplay_sprint_multiplier() == (sprintBoot ? 1.5f : 1.3f));
    for (int i = 0; i < PC_GAMEPLAY_OPTION_COUNT; ++i) {
        const bool expected = i == PC_GAMEPLAY_SKIP_INTRO ? introBoot :
            i == PC_GAMEPLAY_REVEAL_SEA_CHART || i == PC_GAMEPLAY_UNRESTRICTED_BOAT ? false :
            i == PC_GAMEPLAY_JUMP_BUTTON ? !jumpBoot : true;
        assert(pc_gameplay_enabled(static_cast<PcGameplayOption>(i)) == expected);
    }
    const auto& loading = descriptions.at("COS_FAST_LOADING");
    loading.apply(loading.key, "1", nullptr);
    assert(pc_gameplay_enabled(PC_GAMEPLAY_FAST_LOADING));
    loading.apply(loading.key, "0", nullptr);
    assert(!pc_gameplay_enabled(PC_GAMEPLAY_FAST_LOADING));
    assert(!pc_gameplay_enabled(static_cast<PcGameplayOption>(-1)));
    assert(!pc_gameplay_enabled(PC_GAMEPLAY_OPTION_COUNT));
    for (const auto& pair : descriptions) {
        const auto& d = pair.second;
        if (!d.apply) continue;
        d.apply(d.key, "1", d.user);
        d.apply(d.key, "0", d.user);
    }
    values["COS_BWW_FASTER_MOVEMENT"] = "0";
    values["COS_BWW_FASTER_GRAPPLE"] = "0";
    values["COS_BWW_FASTER_TRAVERSAL"] = "0";
    values["COS_BWW_TINGLE_CHESTS"] = "0";
    values["COS_BWW_FASTER_BALLAD"] = "0";
    values["COS_BWW_SKIP_INTRO"] = "0";
    values["COS_BWW_INSTANT_TEXT"] = "0";
    pc_gameplay_options_init();
    for (int i = 0; i < PC_GAMEPLAY_OPTION_COUNT; ++i)
        assert(pc_gameplay_enabled(static_cast<PcGameplayOption>(i)) ==
            ((movementBoot && i == PC_GAMEPLAY_FASTER_MOVEMENT) ||
             (grappleBoot && i == PC_GAMEPLAY_FASTER_GRAPPLE) ||
                (traversalBoot && i == PC_GAMEPLAY_FASTER_TRAVERSAL) ||
                (balladBoot && i == PC_GAMEPLAY_FASTER_BALLAD) ||
                (introBoot && i == PC_GAMEPLAY_SKIP_INTRO) ||
                (doorsBoot && i == PC_GAMEPLAY_QUICK_DOORS) ||
                (textBoot && i == PC_GAMEPLAY_INSTANT_TEXT)));
    // Deferred requests remain stable during an action and adopt both directions
    // at the explicit boundary. Other action groups are not committed with them.
    const std::pair<const char*, PcGameplayOption> deferred[] = {
        {"COS_BWW_FASTER_MOVEMENT", PC_GAMEPLAY_FASTER_MOVEMENT},
        {"COS_BWW_FASTER_TRAVERSAL", PC_GAMEPLAY_FASTER_TRAVERSAL},
        {"COS_BWW_FASTER_GRAPPLE", PC_GAMEPLAY_FASTER_GRAPPLE},
        {"COS_BWW_FASTER_BLOCK_PUSH", PC_GAMEPLAY_FASTER_BLOCK_PUSH},
        {"COS_BWW_FASTER_BALLAD", PC_GAMEPLAY_FASTER_BALLAD},
        {"COS_BWW_SKIP_INTRO", PC_GAMEPLAY_SKIP_INTRO},
        {"COS_BWW_INSTANT_TEXT", PC_GAMEPLAY_INSTANT_TEXT},
        {"COS_QUICK_DOORS", PC_GAMEPLAY_QUICK_DOORS},
    };
    for (const auto& item : deferred) {
        const auto& desc = descriptions.at(item.first);
        desc.apply(desc.key, "0", nullptr);
        pc_gameplay_begin_action(item.second);
        desc.apply(desc.key, "1", nullptr);
        assert(!pc_gameplay_enabled(item.second));
        pc_gameplay_begin_action(PC_GAMEPLAY_SEA_COMPASS);
        assert(!pc_gameplay_enabled(item.second));
        pc_gameplay_begin_action(item.second);
        assert(pc_gameplay_enabled(item.second));
        desc.apply(desc.key, "0", nullptr);
        assert(pc_gameplay_enabled(item.second));
        pc_gameplay_begin_action(item.second);
        assert(!pc_gameplay_enabled(item.second));
    }
    pc_gameplay_begin_action(static_cast<PcGameplayOption>(-1));
    pc_gameplay_begin_action(PC_GAMEPLAY_OPTION_COUNT);
    const auto& sailSetting = descriptions.at("COS_BWW_SAIL");
    sailSetting.apply(sailSetting.key, "1", nullptr);
    assert(pc_gameplay_sail_mode() == expectedSail);
    pc_gameplay_begin_sailing();
    assert(pc_gameplay_sail_mode() == PC_SAIL_SWIFT);
    sailSetting.apply(sailSetting.key, "2", nullptr);
    assert(pc_gameplay_sail_mode() == PC_SAIL_SWIFT);
    pc_gameplay_begin_sailing();
    assert(pc_gameplay_sail_mode() == PC_SAIL_BRISK);
    sailSetting.apply(sailSetting.key, "invalid", nullptr);
    pc_gameplay_begin_sailing();
    assert(pc_gameplay_sail_mode() == PC_SAIL_ORIGINAL);

    const auto& textSetting = descriptions.at("COS_BWW_INSTANT_TEXT");
    textSetting.apply(textSetting.key, "1", nullptr);
    pc_gameplay_begin_text(PC_TEXT_NORMAL);
    assert(pc_gameplay_text_enabled(PC_TEXT_NORMAL));
    textSetting.apply(textSetting.key, "0", nullptr);
    pc_gameplay_begin_text(PC_TEXT_DEMO);
    assert(pc_gameplay_text_enabled(PC_TEXT_NORMAL));
    assert(!pc_gameplay_text_enabled(PC_TEXT_DEMO));
    pc_gameplay_begin_text(PC_TEXT_NORMAL);
    assert(!pc_gameplay_text_enabled(PC_TEXT_NORMAL));
    textSetting.apply(textSetting.key, "1", nullptr);
    pc_gameplay_begin_text(PC_TEXT_DEMO);
    textSetting.apply(textSetting.key, "0", nullptr);
    pc_gameplay_begin_text(PC_TEXT_NORMAL);
    assert(pc_gameplay_text_enabled(PC_TEXT_DEMO));
    assert(!pc_gameplay_text_enabled(PC_TEXT_NORMAL));
    pc_gameplay_begin_text(PC_TEXT_DEMO);

    const auto& keep = descriptions.at("COS_FOREST_WATER_KEEP_TREES");
    const auto& longer = descriptions.at("COS_FOREST_WATER_30_MINUTES");
    keep.apply(keep.key, "1", nullptr);
    assert(pc_gameplay_enabled(PC_GAMEPLAY_KEEP_WATERED_TREES));
    assert(!pc_gameplay_enabled(PC_GAMEPLAY_FOREST_WATER_30_MINUTES));
    longer.apply(longer.key, "1", nullptr);
    assert(pc_gameplay_enabled(PC_GAMEPLAY_FOREST_WATER_30_MINUTES));
    keep.apply(keep.key, "0", nullptr);
    assert(!pc_gameplay_enabled(PC_GAMEPLAY_KEEP_WATERED_TREES));
    assert(pc_gameplay_enabled(PC_GAMEPLAY_FOREST_WATER_30_MINUTES));
    longer.apply(longer.key, "0", nullptr);
    // Inventory-only mapping, analog priority, diagonals and opposing directions.
    float x = 0, y = 0;
    assert(!pc_gameplay_menu_direction(false, false, true, false, false, x, y));
    assert(x == 0 && y == 0);
    assert(pc_gameplay_menu_direction(true, false, true, false, true, x, y));
    assert(std::abs(x*x + y*y - 1.0f) < 0.00001f && x > 0 && y > 0);
    x = -0.7f; y = 0.4f;
    assert(pc_gameplay_menu_direction(true, false, true, false, false, x, y));
    assert(x == -0.7f && y == 0.4f);
    x = y = 0;
    assert(pc_gameplay_menu_direction(true, true, true, true, true, x, y));
    assert(x == 0 && y == 0);
    assert(pc_gameplay_menu_direction(true, true, false, false, false, x, y));
    assert(x == -1 && y == 0);
    const auto& sprintDesc = descriptions.at("COS_SPRINT_SPEED");
    for (auto value : {"1.1", "1.2", "1.25", "1.3", "1.37", "1.4", "1.5", "1.6", "1.7", "1.75", "1.8", "1.9", "2"}) {
        sprintDesc.apply(sprintDesc.key, value, sprintDesc.user);
        assert(pc_gameplay_enabled(PC_GAMEPLAY_SPRINT));
        assert(pc_gameplay_sprint_multiplier() == std::stof(value));
    }
    for (auto value : {"0", "invalid", "nan", "inf", "100", "1", "1.09", "2.01", "1.5oops"}) {
        sprintDesc.apply(sprintDesc.key, value, sprintDesc.user);
        assert(!pc_gameplay_enabled(PC_GAMEPLAY_SPRINT));
        assert(pc_gameplay_sprint_multiplier() == 1.0f);
    }
    const auto& jumpDesc = descriptions.at("COS_JUMP_BUTTON");
    jumpDesc.apply(jumpDesc.key, "1", jumpDesc.user);
    assert(pc_gameplay_enabled(PC_GAMEPLAY_JUMP_BUTTON));
    jumpDesc.apply(jumpDesc.key, "0", jumpDesc.user);
    assert(!pc_gameplay_enabled(PC_GAMEPLAY_JUMP_BUTTON));
    assert(pc_gameplay_fade_frames(false) == 26 && pc_gameplay_fade_frames(true) == 6);
    for (auto item : {std::make_pair("COS_DPAD_MENUS", PC_GAMEPLAY_DPAD_MENUS),
                      std::make_pair("COS_SHORT_FADES", PC_GAMEPLAY_SHORT_FADES)}) {
        const auto& desc = descriptions.at(item.first);
        desc.apply(desc.key, "1", desc.user);
        assert(pc_gameplay_enabled(item.second));
        desc.apply(desc.key, "0", desc.user);
        assert(!pc_gameplay_enabled(item.second));
    }
    assert(pc_gameplay_forest_water_frames(36000, false) == 36000);
    assert(pc_gameplay_forest_water_frames(100, false) == 100);
    assert(pc_gameplay_forest_water_frames(36000, true) == 30 * 60 * 30);

    const auto& compass = descriptions.at("COS_BWW_SEA_COMPASS");
    compass.apply(compass.key, "1", nullptr);
    assert(pc_gameplay_enabled(PC_GAMEPLAY_SEA_COMPASS));
    assert(!pc_gameplay_enabled(PC_GAMEPLAY_SWING_TURN));

    assert(pc_gameplay_swing_heading(0, 0.249f) == 0);
    assert(pc_gameplay_swing_heading(0, -0.249f) == 0);
    assert(pc_gameplay_swing_heading(0, 0.25f) == -128);
    assert(pc_gameplay_swing_heading(0, -0.25f) == 128);
    assert(pc_gameplay_swing_heading(0, 1.0f) == -512);
    assert(pc_gameplay_swing_heading(32767, -1.0f) == -32257);
    assert(pc_gameplay_swing_heading(-32768, 1.0f) == 32256);
    assert(pc_gameplay_swing_heading(123, std::numeric_limits<float>::quiet_NaN()) == 123);
    assert(pc_gameplay_swing_heading(123, 2.0f) == 123);
    for (int angle : {0, 16384, -16384, -32768}) {
        assert(pc_gameplay_compass_degrees(angle, false) == -angle * 180.0f / 32768);
        assert(pc_gameplay_compass_degrees(angle, true) == angle * 180.0f / 32768);
    }
    const auto original = pc_gameplay_sail_tuning(PC_SAIL_ORIGINAL);
    const auto swift = pc_gameplay_sail_tuning(PC_SAIL_SWIFT);
    const auto brisk = pc_gameplay_sail_tuning(PC_SAIL_BRISK);
    assert(original.topSpeed == 55.0f && original.initialSpeed == 80.0f);
    assert(swift.topSpeed == original.topSpeed * 2 && swift.initialSpeed == original.initialSpeed * 2);
    assert(brisk.topSpeed == swift.topSpeed && brisk.initialSpeed == swift.initialSpeed);
    assert(swift.acceleration == original.acceleration * 2);
    assert(swift.accelerationStep == original.accelerationStep * 2);
    assert(swift.firstBrakeMax == original.firstBrakeMax * 2);
    assert(swift.firstBrakeMin == original.firstBrakeMin * 2);
    assert(swift.cruiseSpeed == 30.0f);
    assert(swift.stopMax == 2.0f && swift.stopMin == 0.2f);
    assert(brisk.stopMax == 3.0f && brisk.stopMin == 0.2f);
    assert(swift.idleMax == 2.0f && swift.idleMin == 0.1f);
    assert(brisk.idleMax == 4.0f && brisk.idleMin == 0.2f);
    assert(original.stopMax == 1.0f && original.stopMin == 0.1f);
    assert(original.idleMax == 1.0f && original.idleMin == 0.05f);
    for (int step = -160; step <= 160; ++step) {
        const float speed = step * 0.5f;
        const float originalTurn = 4.0f - std::fabs(speed) / 55.0f * 3.0f;
        assert(pc_gameplay_sail_turn_rate(speed, PC_SAIL_ORIGINAL) == originalTurn);
        assert(pc_gameplay_sail_turn_rate(speed * 2, PC_SAIL_SWIFT) == originalTurn);
        assert(pc_gameplay_sail_turn_rate(speed * 2, PC_SAIL_BRISK) == originalTurn);
    }
    assert(pc_gameplay_sail_turn_rate(110, PC_SAIL_SWIFT) == 1.0f);
    for (int heading = -32768; heading <= 32767; ++heading) {
        const unsigned expected = (-heading + 0x5000) & 0xe000u;
        assert(static_cast<uint16_t>(pc_gameplay_sail_wind(heading)) == expected);
    }
    for (int path = -1; path <= 5; ++path) {
        assert(!pc_gameplay_skip_boat_path(false, path, true));
        assert(!pc_gameplay_skip_boat_path(true, path, false));
        assert(pc_gameplay_skip_boat_path(true, path, true) == (path >= 0 && path < 3));
    }

    // No action-duration change when disabled; enabled duration and animation
    // are sampled together at action initialization, not during movement.
    assert(pc_gameplay_block_frames(20, false) == 20);
    assert(pc_gameplay_block_frames(24, false) == 24);
    assert(pc_gameplay_block_frames(20, true) == 12);
    assert(pc_gameplay_block_animation(1.0f, false) == 1.0f);
    assert(pc_gameplay_block_animation(0.8f, false) == 0.8f);
    assert(pc_gameplay_block_animation(1.0f, true) == 1.4f);

    const auto oldRoll = pc_gameplay_roll_tuning(1.5f, 0.5f, false);
    const auto fastRoll = pc_gameplay_roll_tuning(1.5f, 0.5f, true);
    assert(oldRoll.multiplier == 1.5f && oldRoll.base == 0.5f);
    assert(fastRoll.base == 20.0f);
    // Better starts from a standstill, with the original maximum retained.
    assert(std::fabs((17 * oldRoll.multiplier + oldRoll.base) -
                     (17 * fastRoll.multiplier + fastRoll.base)) < 0.00001f);
    for (int speed = 0; speed < 17; ++speed)
        assert(speed * fastRoll.multiplier + fastRoll.base > speed * oldRoll.multiplier + oldRoll.base);
    assert(pc_gameplay_crawl_speed(3.0f, false) == 3.0f);
    assert(pc_gameplay_crawl_speed(2.5f, false) == 2.5f);
    assert(pc_gameplay_crawl_speed(3.0f, true) == 6.0f);

    for (auto action : {PC_TRAVERSAL_LEDGE_START, PC_TRAVERSAL_LADDER_START,
                        PC_TRAVERSAL_LADDER_MOVE, PC_TRAVERSAL_LADDER_END,
                        PC_TRAVERSAL_SIDLE}) {
        for (float originalRate : {0.8f, 0.9f, 1.0f, 1.2f, 1.6f, 0.75f})
            assert(pc_gameplay_traversal_rate(originalRate, action, false) == originalRate);
        const float expected = action == PC_TRAVERSAL_LADDER_END ? 1.4f :
            action == PC_TRAVERSAL_SIDLE ? 2.0f : 1.6f;
        assert(pc_gameplay_traversal_rate(0.75f, action, true) == expected);
    }
    assert(pc_gameplay_traversal_rate(0.75f, static_cast<PcTraversalAction>(-1), true) == 0.75f);
    assert(pc_gameplay_talk_zoom_frames(20, false) == 20);
    assert(pc_gameplay_talk_zoom_frames(24, false) == 24);
    assert(pc_gameplay_talk_zoom_frames(20, true) == 10);

    const auto oldGrapple = pc_gameplay_grapple_tuning(false);
    const auto fastGrapple = pc_gameplay_grapple_tuning(true);
    // Half-duration throws travel the same distance in either aiming mode.
    assert(oldGrapple.firstPersonFrames * oldGrapple.throwSpeed ==
           fastGrapple.firstPersonFrames * fastGrapple.throwSpeed);
    assert(oldGrapple.thirdPersonFrames * oldGrapple.throwSpeed ==
           fastGrapple.thirdPersonFrames * fastGrapple.throwSpeed);
    assert(fastGrapple.dropSpeed == 2 * oldGrapple.dropSpeed);
    auto wrapFrames = [](PcGrappleTuning tune, float scale) {
        float denominator = 20.0f - (scale - 1.0f) * tune.wrapScale;
        if (denominator < 0.5f) denominator = 0.5f;
        int wrapped = 0, elapsed = 0;
        while (wrapped < 400 && elapsed < 1000) {
            wrapped += static_cast<int>(elapsed / denominator) + tune.wrapCounterStep;
            ++elapsed;
        }
        assert(wrapped >= 400 && elapsed < 1000);
        return elapsed;
    };
    for (float scale : {0.5f, 1.0f, 1.5f, 2.0f, 3.0f})
        assert(wrapFrames(fastGrapple, scale) < wrapFrames(oldGrapple, scale));

}
