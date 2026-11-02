#!/usr/bin/env python3
"""Check source ownership and layout invariants without configuring or compiling."""
import json
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[2]


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()


moves = json.loads((ROOT / 'ps5/source-moves.json').read_text())
for move in moves:
    old, new = ROOT / move['from'], ROOT / move['to']
    assert not old.exists(), f'duplicate moved source: {old}'
    assert new.is_file(), f'missing owned source: {new}'
for config, folders in [('game-features.cmake', ['src/features', 'src/game_hooks', 'src/integration', 'src/presentation']),
                        ('aurora-extensions.cmake', ['aurora/gfx'])]:
    text = (ROOT / 'ps5/cmake' / config).read_text()
    source_list = re.search(r'target_sources\([^)]*\)', text).group()
    listed = re.findall(r'\$\{PS5_PORT_DIR\}/ps5/([^"\n]+\.cpp)', source_list)
    actual = [str(p.relative_to(ROOT / 'ps5')) for f in folders for p in (ROOT / 'ps5' / f).glob('*.cpp')]
    assert len(listed) == len(set(listed)) and set(listed) == set(actual), (config, listed, actual)
meta = (ROOT / 'ps5/include/pc/pc_setting_presentation.h').read_text()
keys = re.findall(r'\{"(COS_[^"]+)"', meta)
assert len(keys) == len(set(keys)), 'duplicate menu metadata'
known_source = '\n'.join(p.read_text() for p in (ROOT / 'ps5/src').rglob('*.cpp'))
for feature in ('pc_menu.cpp', 'pc_language.cpp'):
    known_source += (ROOT / 'vendor/switchwaker/native/src/pc/features' / feature).read_text()
assert all('"' + key + '"' in known_source for key in keys), 'unknown presentation setting'
menu = (ROOT / 'ps5/src/presentation/pc_menu.cpp').read_text()
assert (ROOT / 'vendor/switchwaker/native/src/pc/features/pc_menu.cpp').read_text() == git(ROOT / 'vendor/switchwaker', 'show', 'HEAD:native/src/pc/features/pc_menu.cpp') + '\n', 'upstream menu must remain unchanged'
overlay = ROOT / 'vendor/switchwaker/native/src/pc/features/pc_overlay.cpp'
assert overlay.read_text().strip() == git(ROOT / 'vendor/switchwaker', 'show', 'HEAD:native/src/pc/features/pc_overlay.cpp'), 'excluded upstream overlay must remain unchanged'
assert 'pc_gameplay_options_init();' not in menu, 'menu still owns feature lifecycle'
config = (ROOT / 'ps5/cmake/game-features.cmake').read_text()
assert 'list(FILTER ps5_pc_sources EXCLUDE REGEX "/features/pc_(menu|overlay)[.]cpp$")' in config
assert '"${PS5_PORT_DIR}/ps5/src/presentation/pc_menu.cpp"' in config
startup = (ROOT / 'vendor/switchwaker/native/src/pc/runtime/pc_main.cpp').read_text()
assert startup.index('pc_ps5_features_init();') < startup.index('menuInit();'), 'startup registration order'
assert not (ROOT / 'ps5/patches/switchwaker.patch').exists()
assert not (ROOT / 'ps5/patches/aurora.patch').exists()
for name in ('switchwaker', 'aurora'):
    folder = ROOT / 'ps5/patches' / name
    topics = json.loads((folder / 'topics.json').read_text())
    assert (folder / 'series').read_text().splitlines() == [t['name'] for t in topics]
    assert all((folder / t['name']).is_file() for t in topics)
# Use the staged gitlink so an isolated upstream update can be checked before commit.
vendor = ROOT / 'vendor/switchwaker'
assert git(ROOT, 'rev-parse', ':vendor/switchwaker') == git(vendor, 'rev-parse', 'HEAD')
print('Source layout checks passed: ownership, target membership, presentation/lifecycle, series and selected gitlink.')

# Each menu switch has one case per row kind; catches accidental cleanup duplicates.
for body in menu.split('switch (r.kind) {')[1:]:
    body = body.split('\n}\n', 1)[0]
    labels = re.findall(r'case RowKind::(\w+):', body)
    assert len(labels) == len(set(labels)), f'duplicate menu switch cases: {labels}'

# PS5 presentation routing is separate from upstream's persistent setting tabs.
presentation_rows = {}
for match in re.finditer(r'\{"(COS_[^"]+)",\s*\w+,\s*(true|false),\s*(\d+),\s*PcMenuTab::(\w+),\s*PcMenuSection::(\w+)\}', meta):
    key, hidden, order, tab, section = match.groups()
    presentation_rows[key] = (tab, section, int(order))
for key in ('COS_PS5_OUTPUT', 'COS_FB_SCALE', 'COS_FPS60'):
    assert presentation_rows[key][0] == "Graphics"
for key in ('COS_HEAT_HAZE', 'COS_CAMERA_SHAKE'):
    assert presentation_rows[key][0] == "Gameplay"
for key in ('COS_CAMERA_INVERT_X', 'COS_CAMERA_INVERT_Y', 'COS_HAPTICS'):
    assert presentation_rows[key][0] == "Controls"
for key in ('COS_MENU_TEXT_SIZE', 'COS_MENU_HIGH_CONTRAST', 'COS_FPS_OVERLAY'):
    assert presentation_rows[key][0] == "System"
row_kinds = set(re.search(r'enum class RowKind \{([^}]+)', menu).group(1).replace(' ', '').split(','))
for body in menu.split('switch (r.kind) {')[1:]:
    cases = set(re.findall(r'case RowKind::(\w+):', body.split('\n}\n', 1)[0]))
    assert cases == row_kinds, ('incomplete menu action handling', row_kinds - cases)
assert 'pc_menu_setting_tab(d) == tab' in menu
assert 'pc_menu_setting_is_tool(d->key) != m.toolsPage' in menu
assert presentation_rows['COS_HAPTICS'][1] != presentation_rows['COS_DPAD_MENUS'][1]
assert presentation_rows['COS_HD_BLOOM'][1] == presentation_rows['COS_DOF'][1]
for key in ('COS_HD_SSAO', 'COS_COLOUR_GRADE', 'COS_HD_BLOOM'):
    assert presentation_rows[key][1] == presentation_rows['COS_DOF'][1]
    assert presentation_rows[key][2] < presentation_rows['COS_DOF'][2] < presentation_rows['COS_HD_FAR_BLUR'][2]
for key in ('COS_HD_DISTANT_ISLANDS', 'COS_HD_CLOUD_SHADOWS', 'COS_HD_EDGE_LIGHT', 'COS_HD_TORCH_LIGHT'):
    assert presentation_rows[key][1] == 'Enhancements'
    assert presentation_rows[key][2] > presentation_rows['COS_HD_FAR_BLUR'][2]
assert presentation_rows['COS_HD_SSAO'][2] < presentation_rows['COS_COLOUR_GRADE'][2]
for key in ('COS_BWW_SAIL', 'COS_BWW_SEA_COMPASS', 'COS_BWW_UNRESTRICTED_BOAT'):
    assert presentation_rows[key][2] < presentation_rows['COS_BWW_REVEAL_SEA_CHART'][2]
assert presentation_rows['COS_BWW_SEA_COMPASS'][2] < presentation_rows['COS_BWW_UNRESTRICTED_BOAT'][2]
assert '"COS_BWW_REVEAL_SEA_CHART", PC_GAMEPLAY_MENU_SAILING' in meta
assert presentation_rows['COS_PERF_LOG'][2] < presentation_rows['COS_PERF_EVERY'][2]
assert presentation_rows['COS_MENU_HIGH_CONTRAST'][2] < 620 < presentation_rows['COS_FPS_OVERLAY'][2]
assert 'RowKind::DeveloperTools ? 620' in menu
assert 'RowKind::RestoreDefaults || r.kind == RowKind::DeveloperTools) ? int(PcMenuSection::Menu)' in menu
assert 'RowKind::RestoreDefaults ? 615' in menu
assert presentation_rows['COS_MENU_HIGH_CONTRAST'][2] < 615 < 620
assert 'pc_settings_restore_defaults() != 0' in menu
assert 'GameplayCategory' not in menu and 'm.gameplay' not in menu
assert 'ga != gb ? ga < gb : pc_menu_setting_order(a.desc->key, a.desc->order)' in menu
assert 'gameplayGroupName(section-1)' in menu
assert 'bool toolsPage = false;' in menu and 'm.toolsPage = false; m.scrollToRow = true; return;' in menu
# The preset must cover exactly the effects exposed in FX, including future additions.
preset = re.search(r'constexpr FxPresetEntry kFxPreset\[\] = \{(.*?)\n\};', menu, re.S).group(1)
preset_values = dict((key, (original, enhanced)) for key, original, enhanced in
                     re.findall(r'\{"([^"]+)", "([^"]+)", "([^"]+)"\}', preset))
fx_keys = {key for key, (tab, _, _) in presentation_rows.items() if tab == 'FX'}
assert set(preset_values) == fx_keys
assert preset_values['COS_COLOUR_GRADE'] == ('original', 'vivid')
assert preset_values['COS_HD_SSAO'] == ('0', '1')
assert preset_values['COS_SHARPENING'] == ('0', '1')
features = menu + (ROOT / 'ps5/src/game_hooks/pc_hd_visuals.cpp').read_text() + (ROOT / 'ps5/src/game_hooks/pc_distant_islands.cpp').read_text()
for key, (original, enhanced) in preset_values.items():
    descriptor = re.search(r'\{\s*"' + key + r'",\s*"[^"\n]+",\s*"[^"\n]+",\s*(PC_SETTING_.*?)\}', features, re.S)
    assert descriptor and re.search(r'"' + original + r'",\s*(?:apply|nullptr)', descriptor.group(1)), key
assert 'applyDof(nullptr, pc_settings_get("COS_DOF"), nullptr);' in menu
assert 'pc_settings_register(&kFxPresetRow)' not in menu
assert 'if (r.desc == &kFxPresetRow) return choiceLabel(r.desc, fxPresetValue());' in menu
print('PS5 menu checks passed: setting routing, inline sections and complete action switches.')

# Styling stays local to the console widgets; both Begin paths balance style state.
assert menu.count('pc_ps5_menu_style_begin(sMenuHighContrast, scale, sMenuOpacity);') == 1
assert menu.count('pc_ps5_menu_style_end();') == 2
assert 'pc_ps5_menu_tab(' in menu and 'pc_ps5_menu_footer(' in menu
widgets = (ROOT / 'ps5/src/presentation/pc_menu_widgets.cpp').read_text()
assert widgets.count('ImGui::PushStyleVar(') == 7 and 'ImGui::PopStyleVar(7);' in widgets
assert widgets.count('ImGui::PushStyleColor(') == 13 and 'ImGui::PopStyleColor(13);' in widgets
assert 'const float height = fontSize + padding * 2;' in widgets
assert 'fit_text(label,' in widgets and 'badgeHeight' not in widgets
assert 'Restart pending' in menu and 'Restart required' in menu
print('Console styling checks passed: vector widgets, balanced style scopes and single-line setting rows.')

# Overlay state must be restored before its first frame, not only on a menu edit.
init = menu.split('void menuInit() {', 1)[1].split('void menuSmokeState(', 1)[0]
for callback, key in [('applyOverlay', 'COS_FPS_OVERLAY'),
                      ('applyOverlayDetail', 'COS_FPS_OVERLAY_DETAIL'),
                      ('applyOverlayPosition', 'COS_FPS_OVERLAY_POSITION'),
                      ('applyOverlaySize', 'COS_FPS_OVERLAY_SIZE')]:
    call = f'{callback}(nullptr, pc_settings_get("{key}"), nullptr);'
    assert init.count(call) == 1, ('overlay setting not adopted at startup', key)
    assert init.index('pc_settings_register(&d);') < init.index(call)
print('Overlay startup checks passed: enabled, layout, position and size use saved settings.')

assert presentation_rows['COS_MENU_TEXT_SIZE'][2] < presentation_rows['COS_MENU_TRANSPARENCY'][2] < presentation_rows['COS_MENU_HIGH_CONTRAST'][2]
assert menu.count('applyMenuTransparency(nullptr, pc_settings_get("COS_MENU_TRANSPARENCY"), nullptr);') == 2
assert 'SetNextWindowBgAlpha(sMenuHighContrast ? 1.0f : sMenuOpacity)' in menu

# PS5 camera input stays local to subject views; the pad's movement axes are not swapped.
camera = (ROOT / 'vendor/switchwaker/game/src/d/d_camera.cpp').read_text()
aim = camera.split('static void dCamera_aimStick(', 1)[1].split('// Issue #25:', 1)[0]
ps5_aim = aim.split('#if defined(__PROSPERO__)', 1)[1].split('#else', 1)[0]
assert '*x = 0.0f;' in ps5_aim and '*y = 0.0f;' in ps5_aim
assert '*x = CPad_GET_SUBSTICK_POS_X(padId);' in ps5_aim
assert '*y = CPad_GET_SUBSTICK_POS_Y(padId);' in ps5_aim
assert 'if (pc_camera_invert_x()) *x = -*x;' in ps5_aim
assert 'if (!pc_camera_invert_y()) *y = -*y;' in ps5_aim
assert 'if (!pc_camera_invert_x())' in camera
assert 'COS_AIM_INVERT_Y' not in presentation_rows
assert 'CHOICES(kOnOff), "0", applyCameraInvertX' in menu
assert 'CHOICES(kOnOff), "0", applyCameraInvertY' in menu
assert 'CPad_GET_SUBSTICK_VALUE(padId) < 0.1f' in ps5_aim
# SubjectLockOn sets 0x1000000 (aiming blocked), but tutorial zoom must
# remain available until the native C-stick/zoom lock (0x800000) is set.
assert 'const f32 zoomStick = chkFlag(0x800000) ? 0.0f : CPad_GET_STICK_POS_Y(mPadId);' in camera
orbit = camera.split('// Any direction enters orbit.', 1)[1].split('#else', 1)[0]
assert '!pc_camera_controls_input().pressed' in orbit and 'mStickCPosYLast' not in orbit
print('Camera source checks passed: right-stick-only aim, left-stick zoom, orbit in every direction and R3 priority.')

assert 'menuSurfaceOpacity = highContrast ? 1.f : opacity * opacity;' in widgets
assert widgets.count('menuSurfaceColor(selected)') == 2
assert 'ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(0,0,0,0));' in widgets

assert 'CHOICES(kMenuTransparency), "40", applyMenuTransparency' in menu
restore = menu.split('case RowKind::RestoreDefaults:', 1)[1].split('case RowKind::DeveloperTools:', 1)[0]
assert 'pc_settings_restore_defaults()' in restore
assert 'applyMenuTransparency(' not in restore

settings = (ROOT / 'vendor/switchwaker/native/src/pc/features/pc_settings.cpp').read_text()
reset = settings.split('int pc_settings_restore_defaults(void) {', 1)[1].split('const char* pc_settings_path', 1)[0]
assert reset.index('remove(s.path.c_str())') < reset.index('entry.stored[slot].clear();')
assert 'errno != ENOENT) return -1;' in reset
assert 's.fileSet.clear();' in reset and 's.devLines.clear();' in reset
assert 'notify(s.entries[key], key.c_str(), value);' in reset
assert 'now != before' not in reset and 'bootValue =' not in reset
harness = (ROOT / 'vendor/switchwaker/native/src/pc/runtime/pc_harness.cpp').read_text()
assert 'pc_settings_set_mode_default(name, PC_MODE_DOCKED, value);' in harness
print('Reset source checks passed: deletion failure guard, cleared overrides, retained platform defaults and unconditional callbacks.')

# The video tour selects rows only; it never activates destructive actions or changes values.
demo = menu.split('bool menuDemoFrame(uint32_t keys) {', 1)[1].split('void handleInput(', 1)[0]
assert 'secondsPerTab = 3.75' in demo
assert 'ImGui::GetTime()' in demo and 'pc_frame_count()' not in demo
assert 'activate(' not in demo and 'pc_settings_set' not in demo
assert 'm.waitRelease = true;' in demo
assert 'if (!menuDemoFrame(keys)) handleInput(keys);' in menu

assert 'kDemoIntroSeconds = 2.0;' in menu and 'kDemoEndPauseSeconds = 1.0;' in menu
assert demo.count('setOpen(false);') == 2
assert 'if (!m.open) setOpen(true);' in demo
assert 'if (elapsed >= secondsPerTab * PC_SETTING_TABS) return true;' in demo
assert 'if (m.demo && m.demoReady && !m.open)' in menu

for callback, key in [('applyCameraInvertX', 'COS_CAMERA_INVERT_X'),
                      ('applyCameraInvertY', 'COS_CAMERA_INVERT_Y'),
                      ('applyCameraShake', 'COS_CAMERA_SHAKE'),
                      ('applyHeatHaze', 'COS_HEAT_HAZE')]:
    call = f'{callback}(nullptr, pc_settings_get("{key}"), nullptr);'
    assert init.count(call) == 1, ('camera setting not adopted at startup', key)
    assert init.index('pc_settings_register(&d);') < init.index(call)
print('Camera startup checks passed: both inversion axes, shake and heat haze adopt resolved settings.')

# PS5 removes unavailable desktop features and retains the menu harness separately.
assert '/features/pc_(update|update_core|button_remap)[.]cpp$' in config
assert '/harness/smokes/pc_button_remap[.]cpp$' in config
frame = (vendor / 'native/src/pc/runtime/pc_frame.cpp').read_text()
assert '#if !defined(__PROSPERO__)\n    buttonRemapFrame(frames);\n#endif' in frame
assert 'struct Smoke' not in menu and 'checkCardRoundtrip' not in menu
smoke = (ROOT / 'ps5/src/integration/pc_menu_smoke.cpp').read_text()
assert 'checkCardRoundtrip' in smoke and 'pc_exit(sSmoke.errors' in smoke
assert 'menuSmokeFrame(frame, m.open, m.opened, inPlay());' in menu
assert 'src/integration/pc_menu_smoke.cpp' in config
platform = (ROOT / 'ps5/cmake/ps5-game.cmake').read_text()
assert 'SDL_ASSEMBLY_UNUSED' not in platform
assert 'add_dependencies(cos_pc webgpu_dawn)' not in platform
assert 'target_link_libraries(cos_pc PRIVATE dawn::dawncpp_headers)' in platform
print('PS5 cleanup passed: desktop exclusions, isolated menu checks and header-only Dawn dependency.')
