#!/usr/bin/env python3
"""Validate menu translation coverage and format arguments without compiling."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / 'ps5'
quoted = r'"(?:[^"\\]|\\.)*"'
literal = quoted + r'(?:\s*' + quoted + r')*'

def decode(text):
    return ''.join(json.loads(token) for token in re.findall(quoted, text))

def strings(text):
    return [decode(token) for token in re.findall(literal, text)]

catalog = {}
for line in (PORT / 'src/presentation/pc_menu_translations.inc').read_text().splitlines():
    if not line.lstrip().startswith('{'):
        continue
    row = strings(line)
    assert len(row) == 5 and all(row), row
    assert row[0] not in catalog, ('duplicate translation', row[0])
    formats = lambda s: re.findall(r'%(?:[-+0 #]*\d*(?:\.\d+)?[hlz]*[diuoxXfFeEgGcs%])', s)
    assert all(formats(value) == formats(row[0]) for value in row[1:]), row
    catalog[row[0]] = row[1:]

menu = (PORT / 'src/presentation/pc_menu.cpp').read_text()
required = set()
overlay = (PORT / 'src/presentation/pc_overlay.cpp').read_text()
for match in re.finditer(r'\b(?:T|tr)\(\s*(' + literal + r')\s*\)', menu + overlay):
    required.add(decode(match[1]))
assert not re.search(r'\b(?:T|tr)\(\s*' + literal + r'\s*,', menu + overlay), 'Inline translations must live in the catalog'

files = list((PORT / 'src').rglob('*.cpp'))
for path in files:
    source = path.read_text()
    for match in re.finditer(r'\bpc_menu_text\(\s*(' + literal + r')\s*\)', source):
        required.add(decode(match[1]))
    for choices in re.findall(r'const PcSettingChoice\s+\w+\[\]\s*=\s*\{(.*?)\};', source, re.S):
        for choice in re.findall(r'\{([^}]+)\}', choices):
            assert len(strings(choice)) == 2, (path, 'inline choice translation', choice)
    for entry in re.findall(r'\{\s*"COS_[^}]+\}', source):
        values = strings(entry)
        # Owned descriptors store catalog keys in the primary label/help slots.
        if 'PC_SETTING_TAB_' in entry or 'kCameraTab' in entry:
            assert len(values) == 4, (path, 'inline descriptor translations', values)
            required.update(values[1:3])
source = (PORT / 'src/features/pc_gameplay_options.cpp').read_text()
for entry in re.findall(r'\{PC_GAMEPLAY_[^}]+\}', source):
    values = strings(entry)
    assert len(values) == 3, ('inline gameplay translations', values)
    required.update(values[1:3])
required.add(decode(re.search(r'sail.help = (' + literal + ')', source).group(1)))
required.update(('Graphics', 'Gameplay', 'Controls', 'System', 'Movement', 'Camera', 'Sailing',
    'Exploration', 'Display', 'Textures', 'Menu',
    'Game language', 'Off', 'On', 'Compact', 'Expanded', 'Frame graph', 'Profiler',
    'Top left', 'Top right', 'Bottom left', 'Bottom right', 'Small', 'Standard', 'Large',
    'Extra large', 'Original', 'Enhanced', 'Swift', 'Brisk', 'Match output',
    'Automatic (1024)', 'No limit'))
for names in re.findall(r'\b(?:kTabNames\[PC_SETTING_TABS\]|names\[\])\s*=\s*\{([^}]+)\}', menu):
    required.update(name for name in strings(names) if name)
assert not required - catalog.keys(), ('missing translations', sorted(required - catalog.keys()))

resolver = (PORT / 'src/presentation/pc_menu_language.cpp').read_text()
assert '#if VERSION == VERSION_PAL' in resolver and 'dComIfGs_getPalLanguage()' in resolver
assert 'return English;' in resolver and 'return english;' in resolver
assert 'pc_ui_spanish()' not in menu
assert 'CHOICES(kMenuLanguages), "auto", applyMenuLanguage' in menu
assert 'PC_SETTING_TAB_DEBUG, 0, CHOICES(kMenuLanguages)' in menu  # no restart flag
assert menu.index('selectTab((m.tab') < menu.index('if (m.warpPage)', menu.index('void handleInput('))
print(f'Menu language checks passed: {len(catalog)} entries, visible text coverage, format arguments, PAL/US fallback and navigation.')

# Reject abandoned catalog entries, including old menu-category descriptions.
used = set()
for folder in ('ps5/src', 'ps5/include', 'vendor/switchwaker/native/src', 'vendor/switchwaker/native/include'):
    for path in (ROOT / folder).rglob('*'):
        if path.suffix not in ('.cpp', '.c', '.h', '.hpp'):
            continue
        for token in re.findall(literal, path.read_text(errors='replace')):
            try:
                used.add(decode(token))
            except ValueError:
                pass  # C-only escapes in unrelated runtime strings
assert not catalog.keys() - used, ('unused translations', sorted(catalog.keys() - used))
