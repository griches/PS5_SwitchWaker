#!/usr/bin/env python3
"""Check (default) or export PS5 overlays without resetting a working tree or its index.

python3 ps5/tools/manage-patches.py [--write] [--target switchwaker ...]
Needs the existing local pinned source checkouts; never fetches or builds.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('switchwaker', 'aurora', 'sdl', 'dawn', 'dawn-abseil')


def git(repo, *args, env=None, data=None):
    result = subprocess.run(['git', '-C', str(repo), *args], env=env, input=data,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(f'{repo.name}: git {args[0]} failed:\n{result.stderr.decode(errors="replace")}')
    return result.stdout


def match(pattern, text):
    found = re.search(pattern, text)
    if not found:
        raise RuntimeError(f'Cannot resolve pinned dependency: {pattern}')
    return found.group(1)


def target(name):
    vendor = ROOT / 'vendor/switchwaker'
    if name == 'switchwaker':
        return vendor, git(ROOT, 'rev-parse', ':vendor/switchwaker').decode().strip(), []
    aurora = ROOT / '.deps/aurora'
    if name == 'aurora':
        pin = match(r'set\(COS_AURORA_COMMIT "([0-9a-f]{40})"',
                    (vendor / 'native/cmake/Aurora.cmake').read_text())
        return aurora, pin, sorted((vendor / 'native/patches/aurora').glob('*.patch'))
    versions = (aurora / 'cmake/AuroraDependencyVersions.cmake').read_text()
    if name == 'sdl':
        ref = match(r'_aurora_dependency_version\(AURORA_SDL3_REF "([^"]+)"', versions)
        return ROOT / '.deps/SDL', ref, []
    dawn = ROOT / '.deps/dawn'
    pin = match(r'_aurora_dependency_version\(AURORA_DAWN_REF "([0-9a-f]{40})"', versions)
    if name == 'dawn':
        return dawn, pin, []
    abseil_pin = match(r"third_party/abseil-cpp@([0-9a-f]{40})", git(dawn, 'show', f'{pin}:DEPS').decode())
    return dawn / 'third_party/abseil-cpp', abseil_pin, []


def generate(name, repo, pin, upstream, temporary):
    expected = git(repo, 'rev-parse', f'{pin}^{{commit}}').strip()
    if git(repo, 'rev-parse', 'HEAD').strip() != expected:
        raise RuntimeError(f'{name}: checkout HEAD differs from its pin; no files changed')
    env = dict(os.environ, GIT_INDEX_FILE=str(temporary / f'{name}.index'))
    git(repo, 'read-tree', pin, env=env)
    for patch in upstream:
        git(repo, 'apply', '--cached', '--whitespace=nowarn', str(patch), env=env)
    baseline = git(repo, 'write-tree', env=env).decode().strip()
    # Query against the temporary baseline, so Aurora files added by upstream's
    # series are not mistaken for new PS5 files. Ignore setup stamp metadata only.
    paths = git(repo, 'ls-files', '--others', '--exclude-standard', '-z', env=env).split(b'\0')
    new = [os.fsdecode(p) for p in paths if p and not os.fsdecode(p).startswith('.ps5-')]
    if new:
        git(repo, 'add', '-N', '--', *new, env=env)
    options = ['--no-ext-diff', '--no-textconv', '--no-renames', '--diff-algorithm=myers',
               '--ignore-submodules=all']
    git(repo, 'diff', '--check', *options, env=env)
    patch = git(repo, 'diff', '--binary', '--unified=3', '--src-prefix=a/', '--dst-prefix=b/',
                *options, env=env)
    # Forward application uses another temporary index, never a checkout/reset/clean.
    git(repo, 'read-tree', baseline, env=env)
    if patch:
        git(repo, 'apply', '--cached', '--check', env=env, data=patch)
        git(repo, 'apply', '--cached', '--whitespace=nowarn', env=env, data=patch)
        git(repo, 'apply', '--reverse', '--check', data=patch)
    difference = git(repo, 'diff', '--binary', *options, env=env)
    if difference:
        raise RuntimeError(f'{name}: generated patch does not reproduce the source')
    return patch


def overlay_paths(name):
    folder = ROOT / 'ps5/patches' / name
    if not (folder / 'series').exists():
        return [ROOT / 'ps5/patches' / f'{name}.patch']
    names = [line.strip() for line in (folder / 'series').read_text().splitlines()
             if line.strip() and not line.lstrip().startswith('#')]
    if len(names) != len(set(names)) or any(Path(n).name != n or not n.endswith('.patch') for n in names):
        raise RuntimeError(f'{name}: invalid series')
    return [folder / n for n in names]


def split_topics(name, patch):
    folder = ROOT / 'ps5/patches' / name
    if not (folder / 'series').exists():
        return [(overlay_paths(name)[0], patch)]
    topics = json.loads((folder / 'topics.json').read_text())
    paths = overlay_paths(name)
    if [p.name for p in paths] != [t['name'] for t in topics]:
        raise RuntimeError(f'{name}: topics.json and series order differ')
    owners = {}
    for i, topic in enumerate(topics):
        for path in topic['paths']:
            if path in owners:
                raise RuntimeError(f'{name}: duplicate ownership: {path}')
            owners[path] = i
    chunks = [b'' for _ in topics]
    for chunk in patch.split(b'diff --git ')[1:]:
        line = chunk.split(b'\n', 1)[0].decode()
        found = re.fullmatch(r'a/(.*?) b/.*', line)
        if not found or found[1] not in owners:
            raise RuntimeError(f'{name}: assign integration path to topics.json: {line}')
        chunks[owners[found[1]]] += b'diff --git ' + chunk
    return list(zip(paths, chunks))


def verify_series(name, repo, pin, upstream, artifacts, temporary):
    env = dict(os.environ, GIT_INDEX_FILE=str(temporary / f'{name}-series.index'))
    git(repo, 'read-tree', pin, env=env)
    for patch in upstream:
        git(repo, 'apply', '--cached', '--whitespace=nowarn', str(patch), env=env)
    for path, patch in artifacts:
        if patch:
            git(repo, 'apply', '--cached', '--check', env=env, data=patch)
            git(repo, 'apply', '--cached', '--whitespace=nowarn', env=env, data=patch)
    if git(repo, 'diff', '--no-ext-diff', '--no-textconv', '--ignore-submodules=all', env=env):
        raise RuntimeError(f'{name}: ordered series differs from source')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true', help='export reviewed topic overlays')
    parser.add_argument('--target', nargs='+', choices=NAMES, default=list(NAMES))
    args = parser.parse_args()
    artifacts = []
    with tempfile.TemporaryDirectory(prefix='ps5-patches-') as temporary:
        for name in args.target:
            repo, pin, upstream = target(name)
            patch = generate(name, repo, pin, upstream, Path(temporary))
            selected = split_topics(name, patch)
            verify_series(name, repo, pin, upstream, selected, Path(temporary))
            for path, content in selected:
                if not args.write and (not path.exists() or path.read_bytes() != content):
                    raise RuntimeError(f'{path.relative_to(ROOT)} is stale; review then export --write --target {name}')
            artifacts.extend(selected)
        # Validate all targets before writing any artifact. Real Git indexes are untouched.
        if args.write:
            for path, patch in artifacts:
                if not path.exists() or path.read_bytes() != patch:
                    pending = path.with_suffix('.patch.tmp')
                    pending.write_bytes(patch)
                    pending.replace(path)
    print(f'{"Exported and checked" if args.write else "Checked"}: {", ".join(args.target)}')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError) as error:
        raise SystemExit(str(error)) from error
