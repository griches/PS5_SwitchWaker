#!/usr/bin/env python3
"""Record source/dependency/cache inputs at packaging time; no tool execution or fetching.

This records inputs, not proof that unvalidated caches or effects work on hardware.
"""
import argparse
import hashlib
import json
import os
import re
import stat
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def revision(repo):
    return subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()


def dependency_state(repo):
    """Fingerprint actual local changes, including initialized nested submodules.

    Packaging-only audit: ordinary incremental source preparation stays cheap.
    Ignored files (build outputs/setup stamps) are outside this source inventory.
    """
    def git(*args):
        return subprocess.check_output(['git', '-C', str(repo), *args])

    changed = set(git('diff', '--name-only', '--no-renames', '--ignore-submodules=all',
                      '-z', 'HEAD', '--').split(b'\0'))
    changed.update(git('ls-files', '--others', '--exclude-standard', '-z').split(b'\0'))
    files = {}
    for raw in sorted(changed - {b''}):
        name = os.fsdecode(raw)
        if any(part.startswith('.ps5-') for part in Path(name).parts):
            continue
        path = repo / name
        if path.is_symlink():
            files[name] = {'kind': 'symlink', 'target': os.readlink(path)}
        elif path.is_file():
            files[name] = {'kind': 'file', 'sha256': digest(path),
                           'mode': oct(stat.S_IMODE(path.stat().st_mode))}
        elif not path.exists():
            files[name] = {'kind': 'deleted'}
        else:
            files[name] = {'kind': 'directory'}
    nested = {}
    for entry in git('ls-files', '--stage', '-z').split(b'\0'):
        if not entry:
            continue
        metadata, raw = entry.split(b'\t', 1)
        mode, pin, stage = metadata.split()
        if mode != b'160000':
            continue
        name = os.fsdecode(raw)
        child = repo / name
        nested[name] = {'gitlink': pin.decode(), 'initialized': (child / '.git').exists()}
        if nested[name]['initialized']:
            nested[name]['source'] = dependency_state(child)
    return {'revision': revision(repo), 'worktree_changes': files, 'submodules': nested}


def public_radv_provenance(text):
    """Export identifiers, never free-form driver build paths or machine details."""
    fields = {}
    for line in text.splitlines():
        key, separator, value = line.partition(':')
        key, value = key.strip(), value.strip()
        if not separator:
            continue
        if key in ('revision', 'sdk') and re.fullmatch(r'[0-9a-f]{40}', value):
            fields[key] = value
        elif key == 'archive sha256' and re.fullmatch(r'[0-9a-f]{64}', value):
            fields[key] = value
        elif key == 'assertions' and value in ('on', 'off'):
            fields[key] = value
    return '\n'.join(f'{key}: {fields[key]}' for key in
                     ('revision', 'sdk', 'assertions', 'archive sha256') if key in fields)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--full', action='store_true', help='audit dependency worktrees recursively (slower on shared filesystems)')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--vulkan', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    args = parser.parse_args()
    inputs = {}
    for folder in ('licenses', 'ps5/include', 'ps5/src', 'ps5/aurora', 'ps5/cmake', 'ps5/patches', 'ps5/shader-cache', 'ps5/sdl', 'ps5/nod', 'ps5/sce_sys', 'ps5/assets'):
        for path in sorted((ROOT / folder).rglob('*')):
            if path.is_file():
                inputs[str(path.relative_to(ROOT))] = digest(path)
    for name in ('cc-init-array.sh', 'build.sh', 'link-title.sh', 'prepare-link-inputs.sh', 'package-title.sh', 'make-div-table.py', 'source-provenance.py', 'prepare-source.py', 'prepare-asset-metadata.py', 'manage-patches.py', 'setup-sdk.sh', 'gen-region-assets.py', 'setup-submodules.py', 'setup-source.sh', 'setup-dawn.sh', 'setup-sdl.sh'):
        path = ROOT / 'ps5/tools' / name
        inputs[str(path.relative_to(ROOT))] = digest(path)
    inputs['.gitmodules'] = digest(ROOT / '.gitmodules')
    dependencies = {name: dependency_state(ROOT / path) if args.full else {'revision': revision(ROOT / path)} for name, path in {
        'switchwaker': 'vendor/switchwaker', 'aurora': '.deps/aurora', 'sdl': '.deps/SDL',
        'dawn': '.deps/dawn', 'recompcore': '.deps/recompcore', 'decomp': '.deps/decomp'}.items()}
    external = {'vulkan_revision': revision(args.vulkan), 'radv_archive_sha256': digest(args.archive),
                'libc_prx_sha256': digest(args.vulkan / 'runtime/libc.prx'),
                'sdk_revision': (args.sdk / '.ps5-sdk-revision').read_text().strip()}
    provenance = args.archive.parent.parent / 'PROVENANCE.txt'
    if provenance.exists():
        external['radv_provenance'] = public_radv_provenance(provenance.read_text())
    dirty = subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain', '--ignore-submodules=all', '--untracked-files=no'], text=True)
    record = {'schema_version': 4, 'eboot_sha256': digest(args.output.parent / 'eboot.bin'), 'dependency_audit': 'full' if args.full else 'revisions-only', 'game_id': 'universal', 'supported_game_ids': ['GZLE01', 'GZLP01'], 'port_revision': revision(ROOT), 'port_tracked_worktree_dirty': bool(dirty.strip()),
              'dependencies': dependencies, 'external': external,
              'source_and_cache_sha256': inputs,
              'note': 'Packaging input inventory; revisions-only mode does not audit dependency edits or nested checkouts. No runtime/cache validation is implied.'}
    args.output.write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
