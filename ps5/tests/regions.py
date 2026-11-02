#!/usr/bin/env python3
"""Regional orchestration fixtures. All setup/configure/build tools are inert mocks."""
import json
import itertools
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


with tempfile.TemporaryDirectory(prefix='ps5-regions-') as temporary:
    temp = Path(temporary)
    project = temp / 'project'
    tools = project / 'ps5/tools'
    tools.mkdir(parents=True)
    metadata = project / 'ps5/sce_sys/param.json'
    metadata.parent.mkdir(parents=True)
    metadata.write_text(json.dumps({'contentVersion': '01.000.003'}))
    for name in ('build.sh', 'cc-init-array.sh'):
        shutil.copy2(ROOT / 'ps5/tools' / name, tools / name)
    for name in ('setup-source', 'setup-dawn', 'setup-sdl', 'setup-sdk'):
        (tools / f'{name}.sh').write_text('#!/bin/sh\nprintf "%s\\n" "$FIXTURE_DEPS"\n')
    dependencies = temp / 'dependencies'
    dependencies.mkdir()
    commands = temp / 'commands'
    commands.mkdir()
    # These programs only record arguments and create configuration sentinels.
    mock = '''#!/usr/bin/env python3
import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
with open(os.environ['FIXTURE_LOG'], 'a') as log:
    log.write(json.dumps([name, sys.argv[1:]]) + '\\n')
if name == 'cmake':
    work = pathlib.Path(sys.argv[sys.argv.index('-B') + 1])
    disc = next(a.split('=', 1)[1] for a in sys.argv if a.startswith('-DCOS_GAME_ID='))
    (work / 'CMakeCache.txt').write_text('COS_GAME_ID:STRING=' + disc + '\\n')
    (work / 'build.ninja').write_text('fixture: never executed by real ninja\\n')
'''
    for name in ('cmake', 'ninja'):
        path = commands / name
        path.write_text(mock)
        path.chmod(0o755)
    log = temp / 'commands.jsonl'
    build = temp / 'shared-build'
    env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ['PATH'],
               FIXTURE_DEPS=str(dependencies), FIXTURE_LOG=str(log), PS5_VULKAN_DIR=str(dependencies),
               PS5_BUILD_DIR=str(build), PS5_CLANG='/fixture/compiler-never-run', PS5_CCACHE='0')
    env.pop('PS5_RECONFIGURE', None)
    env.pop('PS5_REGION', None)

    def run(*args):
        return subprocess.run(['bash', str(tools / 'build.sh'), *args], env=env,
                              text=True, capture_output=True, check=True)

    def events():
        return [json.loads(line) for line in log.read_text().splitlines()]

    run()
    # Repeated invocations preserve the existing object tree and configuration.
    sentinel = build / 'dependency-object-sentinel'
    sentinel.write_text('existing dependency object')
    timestamp = sentinel.stat().st_mtime_ns
    run()
    run('-j2')
    run('-j2', 'switchwaker')
    assert json.loads(metadata.read_text())['contentVersion'] == '01.000.003', 'tracked template must stay unchanged'
    generated_metadata = build / 'param.json'
    assert json.loads(generated_metadata.read_text())['contentVersion'] == '01.000.007'
    version_header = build / 'ps5_build_number.h'
    assert '#define PS5_BUILD_NUMBER "7"' in version_header.read_text()
    configurations = [args for name, args in events() if name == 'cmake']
    assert len(configurations) == 1
    args = configurations[0]
    assert '-DCOS_VERSION=3' in args and '-DCOS_GAME_ID=GZLP01' in args
    assert '-DCOS_RUNTIME_REGION=ON' in args
    assert f'-DCOS_ASSETS_DIR={project}/.deps/assets/universal' in args
    assert args[args.index('-B') + 1] == str(build)
    calls = [args for name, args in events() if name == 'ninja']
    assert calls[-1] == calls[-2] == ['-C', str(build), '-j2', 'switchwaker']
    subprocess.run(['bash', str(tools / 'build.sh')], env=dict(env, PS5_REGION='pal'),
                   capture_output=True, check=True)
    assert sum(name == 'cmake' for name, args in events()) == 1
    assert sentinel.read_text() == 'existing dependency object' and sentinel.stat().st_mtime_ns == timestamp
    # An SDK compiler-selection change must invalidate configuration, not objects on disk.
    changed_env = dict(env, LLVM_CONFIG='/fixture/another-llvm-config')
    subprocess.run(['bash', str(tools / 'build.sh')], env=changed_env, capture_output=True, check=True)
    assert sum(name == 'cmake' for name, args in events()) == 2
    assert sentinel.read_text() == 'existing dependency object'

    before = events()
    for obsolete in [('us',), ('pal',), ('auto',), ('--region', 'us'), ('--region=pal',)]:
        bad = subprocess.run(['bash', str(tools / 'build.sh'), *obsolete], env=env, capture_output=True)
        assert bad.returncode != 0 and b'Region build modes were removed' in bad.stderr
    assert events() == before, 'obsolete modes must fail before invoking build tools'
    saved_version = generated_metadata.read_text()
    saved_header = version_header.read_text()
    run('-n')
    assert generated_metadata.read_text() == saved_version, 'dry runs must not consume a build number'
    assert version_header.read_text() == saved_header
    metadata.write_text(json.dumps({'contentVersion': '01.000.999', 'titleId': 'PPSA99309'}))
    run()
    assert json.loads(generated_metadata.read_text()) == {'contentVersion': '01.001.000', 'titleId': 'PPSA99309'}
    assert '#define PS5_BUILD_NUMBER "1000"' in version_header.read_text()

features = (ROOT / 'ps5/cmake/game-features.cmake').read_text()
assert 'CMAKE_CONFIGURE_DEPENDS' not in features
assert 'ps5_menu_metadata' not in features

language = (ROOT / 'vendor/switchwaker/native/src/pc/features/pc_language.cpp').read_text()
assert '#if VERSION == VERSION_PAL' in language and 'ps5_system_language()' in language
assert '"COS_GAME_LANGUAGE"' in language and 'PC_SETTING_RESTART' in language

# Each save slot may recover only from its own valid backup, regardless of
# whether the other two slots have valid primary or backup checksums.
card = (ROOT / 'vendor/switchwaker/game/src/m_Do/m_Do_MemCardRWmng.cpp').read_text()
recovery = re.findall(r'if \(!csum([012])a && csum([012])b\) \{\s*'
                     r'memcpy\(&save->gamedata\[([012])\], &save2->gamedata\[([012])\]', card)
assert len(recovery) == 3
for checks in itertools.product((False, True), repeat=6):
    for primary, backup, destination, source in recovery:
        primary, backup, destination, source = map(int, (primary, backup, destination, source))
        copies = not checks[primary] and checks[3 + backup]
        expected = not checks[destination] and checks[3 + destination]
        assert source == destination and copies == expected, 'save recovery uses another slot checksum'
print('Save recovery source checks passed: all 64 combinations of primary/backup checksums.')
print('Universal build checks passed: one configuration, preserved objects, rejected regional modes and PAL language wiring (mock tools only).')
