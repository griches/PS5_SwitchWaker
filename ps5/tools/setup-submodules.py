#!/usr/bin/env python3
"""Initialize missing source submodules; never update populated working trees.

Only reads Git metadata on the normal setup path. --check never fetches or writes.
Upstream declarations must agree with the parent's staged gitlinks before setup.
"""
import argparse
import ast
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def git(repo, *args):
    result = subprocess.run(['git', '-C', str(repo), *args], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or f'git {args[0]} failed in {repo}')
    return result.stdout.strip()


def gitlink(parent, path):
    entry = git(parent, 'ls-files', '--stage', '--', path).split()
    if len(entry) != 4 or entry[0] != '160000' or entry[2] != '0':
        raise RuntimeError(f'{path}: missing or conflicted submodule gitlink')
    return entry[1]


def ensure(parent, path, expected=None, check=False):
    if Path(path).is_absolute() or '..' in Path(path).parts:
        raise RuntimeError(f'Invalid submodule path: {path}')
    pin = gitlink(parent, path)
    if expected is not None:
        if pin != expected:
            raise RuntimeError(f'{path}: gitlink {pin} differs from upstream pin {expected}; update pins explicitly')
    repo = parent / path
    if not (repo / '.git').exists():
        if check:
            raise RuntimeError(f'{repo}: submodule is not initialized')
        if repo.exists() and any(repo.iterdir()):
            raise RuntimeError(f'{repo}: nonempty directory is not a Git checkout; left untouched')
        print(f'Initializing submodule {path}', file=sys.stderr)
        # No --recursive: Dawn's platform/test-only modules are deliberately not fetched.
        git(parent, 'submodule', 'update', '--init', '--depth', '1', '--', path)
    if git(repo, 'rev-parse', 'HEAD') != pin:
        raise RuntimeError(f'{repo}: HEAD differs from gitlink {pin}; existing tree left untouched')
    return repo


def match(pattern, text):
    found = re.search(pattern, text, re.MULTILINE)
    if not found:
        raise RuntimeError(f'Cannot read upstream dependency pin: {pattern}')
    return found[1]


def source(check):
    vendor = ensure(ROOT, 'vendor/switchwaker', check=check)
    pin = match(r'set\(COS_AURORA_COMMIT "([0-9a-f]{40})"',
                (vendor / 'native/cmake/Aurora.cmake').read_text())
    ensure(ROOT, '.deps/aurora', pin, check)
    pin = match(r'^pin="([0-9a-f]{40})"',
                (vendor / 'native/tools/fetch_recompcore.sh').read_text())
    ensure(ROOT, '.deps/recompcore', pin, check)
    pin = match(r'^commit=([0-9a-f]{40})', (vendor / 'game/UPSTREAM').read_text())
    ensure(ROOT, '.deps/decomp', pin, check)


def dawn_dependencies(parent, paths, check):
    for path in paths:
        # The upstream helper's list may include a child within another dependency.
        if any(path.startswith(other + '/') for other in paths):
            continue
        repo = ensure(parent, path, check=check)
        children = [p[len(path) + 1:] for p in paths if p.startswith(path + '/')]
        if children:
            dawn_dependencies(repo, children, check)


def dawn(check):
    versions = (ROOT / '.deps/aurora/cmake/AuroraDependencyVersions.cmake').read_text()
    pin = match(r'_aurora_dependency_version\(AURORA_DAWN_REF "([0-9a-f]{40})"', versions)
    repo = ensure(ROOT, '.deps/dawn', pin, check)
    # Read the upstream selection without executing DEPS or the fetch script.
    tree = ast.parse((repo / 'tools/fetch_dawn_dependencies.py').read_text())
    selections = [node.value for node in ast.walk(tree) if isinstance(node, ast.Assign)
                  and any(isinstance(t, ast.Name) and t.id == 'required_submodules' for t in node.targets)]
    if len(selections) != 1:
        raise RuntimeError('Dawn fetch helper changed; review its dependency selection')
    paths = ast.literal_eval(selections[0])
    if not isinstance(paths, list) or not all(isinstance(p, str) and p and not Path(p).is_absolute()
                                           and '..' not in Path(p).parts for p in paths):
        raise RuntimeError('Invalid Dawn dependency selection')
    dawn_dependencies(repo, paths, check)


def sdl(check):
    versions = (ROOT / '.deps/aurora/cmake/AuroraDependencyVersions.cmake').read_text()
    ref = match(r'_aurora_dependency_version\(AURORA_SDL3_REF "([^"]+)"', versions)
    repo = ensure(ROOT, '.deps/SDL', check=check)
    try:
        release = git(repo, 'rev-parse', f'{ref}^{{commit}}')
    except RuntimeError:
        if check:
            raise RuntimeError(f'SDL release tag {ref} is absent; run setup-sdl.sh once')
        git(repo, 'fetch', '--depth', '1', 'origin', f'{ref}:{ref}')
        release = git(repo, 'rev-parse', f'{ref}^{{commit}}')
    if release != gitlink(ROOT, '.deps/SDL'):
        raise RuntimeError('SDL gitlink differs from Aurora release pin; update pins explicitly')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--group', choices=('source', 'dawn', 'sdl', 'all'), default='all')
    parser.add_argument('--check', action='store_true', help='metadata validation only; no initialization')
    args = parser.parse_args()
    for name, action in [('source', source), ('dawn', dawn), ('sdl', sdl)]:
        if args.group in (name, 'all'):
            action(args.check)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, SyntaxError) as error:
        raise SystemExit(str(error)) from error
