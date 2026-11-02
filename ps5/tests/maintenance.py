#!/usr/bin/env python3
"""Packaging/provenance fixtures and source guards; never compile or configure."""
import importlib.util
import os
import shutil
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('provenance', ROOT / 'ps5/tools/source-provenance.py')
provenance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provenance)


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args])


with tempfile.TemporaryDirectory(prefix='ps5-maintenance-') as temporary:
    temp = Path(temporary)
    repo = temp / 'source'
    subprocess.run(['git', 'clone', '--quiet', '--shared', str(ROOT), str(repo)], check=True)
    before = provenance.dependency_state(repo)
    assert not before['worktree_changes']
    source = repo / 'README.md'
    source.write_text(source.read_text() + '\nlocal change\n')
    extra = repo / 'fixture-extra.txt'
    extra.write_text('untracked source\n')
    link = repo / 'fixture-link'
    link.symlink_to('README.md')
    deleted = repo / 'ps5/src/startup.c'
    deleted.unlink()
    state = provenance.dependency_state(repo)
    changes = state['worktree_changes']
    assert changes['README.md']['sha256'] == provenance.digest(source)
    assert changes['fixture-extra.txt']['sha256'] == provenance.digest(extra)
    assert changes['fixture-link'] == {'kind': 'symlink', 'target': 'README.md'}
    assert changes['ps5/src/startup.c'] == {'kind': 'deleted'}
    git(repo, 'add', '--', 'README.md')
    assert provenance.dependency_state(repo) == state, 'staging changed actual-source inventory'
    source.write_text(source.read_text() + 'second change\n')
    assert provenance.dependency_state(repo)['worktree_changes']['README.md'] != changes['README.md']
    # An initialized nested checkout must report its own edits as well as its pin.
    child = repo / 'fixture-nested'
    subprocess.run(['git', 'clone', '--quiet', '--shared', str(ROOT), str(child)], check=True)
    pin = git(child, 'rev-parse', 'HEAD').decode().strip()
    git(repo, 'update-index', '--add', '--cacheinfo', f'160000,{pin},fixture-nested')
    nested_source = child / 'README.md'
    nested_source.write_text('nested local edit\n')
    nested = provenance.dependency_state(repo)['submodules']['fixture-nested']
    assert nested['gitlink'] == pin and nested['initialized']
    assert nested['source']['worktree_changes']['README.md']['sha256'] == provenance.digest(nested_source)

    # Exercise the real resource refresh commands against a staged title.
    app, ps5 = temp / 'title', temp / 'port'
    for relative in ('res/custom/keep.txt', 'res/ps5-glyphs/obsolete.png', 'res/shader-cache/obsolete.db', 'res/controller-ui/res/Msg/menures.arc'):
        path = app / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('previous')
    shutil.copytree(ROOT / 'ps5/assets/controller-ui', ps5 / 'assets/controller-ui')
    recipe = (ROOT / 'ps5/tools/package-title.sh').read_text()
    refresh = recipe[recipe.index('# Controller UI'):recipe.index('# The licences')]
    env = dict(os.environ, app=str(app), ps5=str(ps5), root=str(temp / 'project'))
    subprocess.run(['bash', '-euo', 'pipefail', '-c', refresh], env=env, check=True)
    assert (app / 'res/custom/keep.txt').read_text() == 'previous'
    assert not (app / 'res/ps5-glyphs/obsolete.png').exists()
    assert not (app / 'res/shader-cache').exists()
    assert not (app / 'res/ps5-glyphs').exists()
    assert (app / 'res/resources-us.bin').read_bytes().startswith(b'P5UIRES3')
    assert not (app / 'res/controller-ui').exists()
    assert (app / 'res/menu-prompts.rgba').is_file()
    (ps5 / 'shader-cache').mkdir(parents=True)
    (ps5 / 'shader-cache/dawn_cache.db').write_text('new cache')
    subprocess.run(['bash', '-euo', 'pipefail', '-c', refresh], env=env, check=True)
    assert (app / 'res/shader-cache/dawn_cache.db').read_text() == 'new cache'
    assert (app / 'res/custom/keep.txt').read_text() == 'previous'
    # Both regional bundles belong to the same universal package.
    assert (app / 'res/resources-pal.bin').read_bytes().startswith(b'P5UIRES3')
    subprocess.run(['bash', '-euo', 'pipefail', '-c', refresh], env=env, check=True)
    assert (app / 'res/resources-pal.bin').read_bytes().startswith(b'P5UIRES3')
    assert (app / 'res/resources-us.bin').read_bytes().startswith(b'P5UIRES3')
    (ps5 / 'assets/controller-ui/pal/resources.bin').unlink()
    missing = subprocess.run(['bash', '-euo', 'pipefail', '-c', refresh], env=env, capture_output=True)
    assert missing.returncode != 0 and b'missing bundled controller asset' in missing.stderr

    # Staging must preserve large files by inode and leave the published title
    # unchanged when generated outputs are replaced or staging is abandoned.
    published, staged = temp / 'published', temp / 'staged'
    (published / 'res').mkdir(parents=True)
    for relative in ('game.iso', 'eboot.bin', 'BUILD_INPUTS.json', 'res/div-table.bin', 'SOURCE.txt'):
        (published / relative).write_text('old')
    staged.mkdir()
    staging_recipe = recipe[recipe.index('# Same-filesystem hardlinks'):recipe.index('mkdir -p "$app/sce_sys"')]
    subprocess.run(['bash', '-euo', 'pipefail', '-c', staging_recipe],
        env=dict(os.environ, published=str(published), app=str(staged)), check=True)
    assert (published / 'game.iso').stat().st_ino == (staged / 'game.iso').stat().st_ino
    assert not (staged / 'eboot.bin').exists()
    (staged / 'eboot.bin').write_text('new')
    assert (published / 'eboot.bin').read_text() == 'old'
    replacement = temp / 'new-iso'
    replacement.write_text('replacement')
    subprocess.run(['cp', '--remove-destination', str(replacement), str(staged / 'game.iso')], check=True)
    assert (published / 'game.iso').read_text() == 'old'


joystick = (ROOT / 'ps5/sdl/SDL_ps5joystick.c').read_text()
update = joystick.split('static void PS5_JoystickUpdate', 1)[1].split('static void PS5_JoystickClose', 1)[0]
assert 'pad = (struct pad){0};' in update and 'return;' not in update
assert update.index('pad = (struct pad){0};') < update.index('SDL_SendJoystickButton')
recording = (ROOT / '.deps/aurora/lib/gfx/recording.cpp').read_text()
finish = recording.split('void finish_world_effects(const ClipRect& cameraScissor, bool image = false)', 1)[1].split('bool push_encoder_task', 1)[0]
assert finish.index('!post_fx::any_enabled()') < finish.index('return;') < finish.index('gx::fifo::drain()')
assert '!g_recorder.postEffects.any()' in finish.split('gx::fifo::drain()')[0]
assert 'worldFinished' not in recording
assert 'gx::map_logical_scissor(cameraScissor)' in finish
assert finish.index('postEffects.worldScissor =') < finish.index('enqueue_pass(frame,')
visuals = (ROOT / 'ps5/src/game_hooks/pc_hd_visuals.cpp').read_text()
assert 'dComIfGp_getCurrentViewport()' in visuals and 'viewport->mScissor' in visuals
link_inputs = (ROOT / 'ps5/cmake/link-inputs.cmake').read_text().split('PROPERTY LINK_DEPENDS', 1)[1]
assert 'vendor/switchwaker/native/data/initial_pipeline_cache.db' in link_inputs
print('Maintenance checks passed: dirty/nested source fingerprints, staged resources and input/render/link source guards.')

# Port extensions and upstream readback commands share one FIFO namespace.
# A duplicate opcode silently sends a command to the wrong decoder.
import re
gx_commands = (ROOT / '.deps/aurora/include/dolphin/gx/GXAurora.h').read_text()
opcodes = {}
for name, value in re.findall(r'^#define (GX_AURORA_\w+) (0x[0-9A-Fa-f]+)\s*$', gx_commands, re.MULTILINE):
    code = int(value, 16)
    assert code not in opcodes, f'FIFO opcode collision: {name} and {opcodes.get(code)}'
    opcodes[code] = name

assert 'ps5/assets/controller-ui/*' in (ROOT / 'ps5/cmake/link-inputs.cmake').read_text()

# CMake tracks a real output published only after packaging succeeds.
assert 'OUTPUT_NAME "eboot" SUFFIX ".elf"' in (ROOT / 'ps5/cmake/link-inputs.cmake').read_text()
package = (ROOT / 'ps5/tools/package-title.sh').read_text()
assert package.index('mv "$app" "$published"') < package.index('mv "$work/eboot.elf.new" "$work/eboot.elf"')

# RADV provenance exports only validated public identifiers.
driver_record = ("fork: /Users/private-person/work/mesa\n"
                 "revision: " + "a" * 40 + "\n"
                 "sdk: " + "b" * 40 + "\nassertions: off\n"
                 "archive sha256: " + "c" * 64 + "\n"
                 "host: private-machine\n")
public = provenance.public_radv_provenance(driver_record)
assert 'fork:' not in public and 'host:' not in public and '/Users/' not in public
assert 'revision: ' + 'a' * 40 in public and 'archive sha256: ' + 'c' * 64 in public
assert provenance.public_radv_provenance('revision: /home/private/work\nsdk: secret') == ''

# Release metadata describes the binary; release audits require its checkout.
import json
import sys
release = (ROOT / 'ps5/tools/package-release.sh').read_text()
release_metadata = release.split("<<'PYTHON'\n", 1)[1].split('\nPYTHON', 1)[0]
with tempfile.TemporaryDirectory(prefix='ps5-release-metadata-') as temporary:
    app = Path(temporary)
    # Stub the expensive checkout audit here; exercise its guards separately below.
    (app / 'source-provenance.py').write_text('def verify_release_sources(record): pass\n')
    (app / 'eboot.bin').write_bytes(b'fixture executable')
    (app / 'sce_sys').mkdir()
    param = app / 'sce_sys/param.json'
    param.write_text(json.dumps({'contentVersion': '01.000.038'}))
    record = {
        'port_revision': 'recorded-build-revision',
        'port_tracked_worktree_dirty': False,
        'dependency_sources_verified': True,
        'eboot_sha256': provenance.digest(app / 'eboot.bin'),
        'dependencies': {'switchwaker': {'revision': 'recorded-upstream-revision'}},
        'external': {'vulkan_revision': 'recorded-vulkan', 'sdk_revision': 'recorded-sdk',
                     'radv_archive_sha256': 'recorded-radv-hash', 'radv_provenance': 'revision: recorded-mesa'},
    }
    def run_release_metadata():
        (app / 'BUILD_INPUTS.json').write_text(json.dumps(record))
        return subprocess.run([sys.executable, '-c', release_metadata, str(app), str(app)],
                              capture_output=True, text=True)
    result = run_release_metadata()
    assert result.returncode == 0, result.stderr
    record['dependency_sources_verified'] = False
    assert run_release_metadata().returncode == 0
    del record['dependency_sources_verified']
    assert run_release_metadata().returncode == 0
    record['dependency_sources_verified'] = True
    text = (app / 'SOURCE.txt').read_text()
    assert 'ProsperoWake, Build 38' in text
    assert result.stdout.strip() == '38'
    assert 'recorded-build-revision' in text and 'recorded-upstream-revision' in text
    assert 'recorded-mesa' in text
    param.write_text(json.dumps({'contentVersion': '01.001.002'}))
    assert run_release_metadata().stdout.strip() == '1002'
    param.write_text(json.dumps({'contentVersion': 'invalid'}))
    assert run_release_metadata().returncode != 0
    param.write_text(json.dumps({'contentVersion': '01.000.038'}))
    (app / 'eboot.bin').write_bytes(b'different executable')
    assert run_release_metadata().returncode != 0
    (app / 'eboot.bin').write_bytes(b'fixture executable')
    # Even hash-matching executables must not ship local compiler paths.
    for leaked in (b'/Users/private-person/source/file.cc',
                   b'/home/private-person/build/file.cc',
                   b'C:\\Users\\private-person\\source\\file.cc'):
        (app / 'eboot.bin').write_bytes(b'fixture executable\0' + leaked)
        record['eboot_sha256'] = provenance.digest(app / 'eboot.bin')
        result = run_release_metadata()
        assert result.returncode != 0 and 'Local build path in eboot.bin' in result.stderr
    (app / 'eboot.bin').write_bytes(b'fixture executable')
    record['eboot_sha256'] = provenance.digest(app / 'eboot.bin')
    record['external']['radv_provenance'] = 'fork: /Users/private-person/mesa'
    assert run_release_metadata().returncode != 0
    record['external']['radv_provenance'] = 'revision: recorded-mesa'
    assert run_release_metadata().returncode == 0
    record['port_tracked_worktree_dirty'] = True
    assert run_release_metadata().returncode != 0
    record['port_tracked_worktree_dirty'] = False
    del record['eboot_sha256']
    assert run_release_metadata().returncode != 0
assert 'hashlib.file_digest' not in release
assert 'ProsperoWake-build-$build_number.zip' in release
assert "'eboot_sha256': digest(args.output.parent / 'eboot.bin')" in (ROOT / 'ps5/tools/source-provenance.py').read_text()
print('Release metadata passed: recorded revisions, executable identity and uncommitted-build rejection.')

# The release audit must run even when old build metadata says it was verified.
from unittest.mock import patch
release_record = {'port_revision': 'same', 'dependencies': {
    name: {'revision': 'same'} for name in
    ('switchwaker', 'aurora', 'sdl', 'dawn', 'recompcore', 'decomp')}}
with patch.object(provenance, 'revision', return_value='same'), \
     patch.object(provenance, 'verify_dependencies') as audit:
    provenance.verify_release_sources(release_record)
    audit.assert_called_once()
    audit.reset_mock()
    for verified in (False, None):
        failed = {**release_record, 'dependency_audit': 'full'}
        if verified is not None:
            failed['dependency_sources_verified'] = verified
        try:
            provenance.verify_release_sources(failed)
        except RuntimeError as error:
            assert 'failed its dependency audit' in str(error)
        else:
            raise AssertionError('Recorded build-time audit failure cleared by a later clean checkout')
    audit.assert_not_called()
    provenance.verify_release_sources({**release_record, 'dependency_audit': 'full',
                                      'dependency_sources_verified': True})
    audit.assert_called_once()
    audit.reset_mock()
    for bad in ({**release_record, 'port_revision': 'old'},
                {**release_record, 'dependencies': {}}):
        try:
            provenance.verify_release_sources(bad)
        except RuntimeError:
            pass
        else:
            raise AssertionError('Mismatched release sources accepted')
    audit.assert_not_called()
    audit.side_effect = RuntimeError('dependency edits')
    try:
        provenance.verify_release_sources(release_record)
    except RuntimeError:
        pass
    else:
        raise AssertionError('Dependency audit failure ignored')

assert 'PS5_FULL_PROVENANCE' not in package
