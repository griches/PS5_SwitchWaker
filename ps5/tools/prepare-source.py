#!/usr/bin/env python3
"""Materialize a pinned overlay without resetting, cleaning or overwriting local edits.

Acquisition is separate (setup-*.sh). This command never fetches or builds.
Unchanged stacks use a cheap preparation key; --verify checks complete source contents.
Patch refreshes check affected files only and never overwrite edits to them.
Unrelated files are left alone. No cached fast path modifies source files.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('patches', Path(__file__).with_name('manage-patches.py'))
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
git = patches.git
STATE = '.ps5-source-state.json'
METADATA = {STATE, '.ps5-aurora', '.ps5-sdl', '.ps5-dawn'}


def differences(repo, tree, env, paths=None):
    if paths == []:
        return []
    scope = ['--', *paths] if paths is not None else []
    git(repo, 'read-tree', tree, env=env)
    changed = git(repo, 'diff', '--name-only', '--no-ext-diff', '--no-textconv',
                  '--ignore-submodules=all', *scope, env=env).decode().splitlines()
    others = git(repo, 'ls-files', '--others', '--exclude-standard', '-z', *scope, env=env).split(b'\0')
    return sorted(set(changed + [os.fsdecode(p) for p in others if p and os.fsdecode(p) not in METADATA]))


def preparation_key(pin, paths):
    fingerprint = hashlib.sha256(pin.encode())
    for path in paths:
        # Order, names and contents matter; timestamps alone are insufficient.
        fingerprint.update(path.name.encode() + b'\0')
        fingerprint.update(path.read_bytes())
        fingerprint.update(b'\0')
    return fingerprint.hexdigest()


def prepare(name, repo, pin, upstream, overlays, temporary, verify=True):
    expected = git(repo, 'rev-parse', f'{pin}^{{commit}}').decode().strip()
    if git(repo, 'rev-parse', 'HEAD').decode().strip() != expected:
        raise RuntimeError(f'{name}: HEAD differs from pin. Prepare a separate checkout; existing tree left untouched.')
    state_path = repo / STATE
    state = json.loads(state_path.read_text()) if state_path.exists() else None
    if state and state.get('pin') != expected:
        raise RuntimeError(f'{name}: recorded pin changed; existing tree left untouched')
    key = preparation_key(expected, [*upstream, *overlays])
    if not verify and state and state.get('preparation_key') == key:
        # No checkout, patch application, tree walk or index reconstruction here.
        # Local development edits are compiled as-is; explicit verification/export
        # remains available. A changed stack must pass affected-file safety checks below.
        return False
    print(f'Preparing {name}: ' + ('full source verification' if verify else 'checking patch files only'), file=sys.stderr)
    env = dict(os.environ, GIT_INDEX_FILE=str(temporary / f'{name}-prepare.index'))
    git(repo, 'read-tree', pin, env=env)
    for path in [*upstream, *overlays]:
        if path.stat().st_size:
            git(repo, 'apply', '--cached', '--check', str(path), env=env)
            git(repo, 'apply', '--cached', '--whitespace=nowarn', str(path), env=env)
    desired = git(repo, 'write-tree', env=env).decode().strip()
    previous = state['tree'] if state else git(repo, 'rev-parse', f'{pin}^{{tree}}').decode().strip()
    scope = None
    if not verify:
        # Compare only files supplied by either the old or new patch stack.
        # Walking Dawn's third_party checkout can take minutes over Lima mounts;
        # those unrelated files are never modified by this operation.
        affected = set()
        for tree in (previous, desired):
            affected.update(os.fsdecode(p) for p in git(repo, 'diff-tree', '-r', '--no-renames',
                '--name-only', '-z', pin, tree).split(b'\0') if p)
        scope = sorted(affected)
    needs_update = differences(repo, desired, env, scope)
    if needs_update:
        dirty = differences(repo, previous, env, scope)
        if dirty:
            raise RuntimeError(f'{name}: local or unrecorded edits; no files changed:\n  ' + '\n  '.join(dirty) +
                               '\nReview/export edits, or prepare an isolated checkout. Never reset this tree automatically.')
        # Do not alter an independently staged developer index during patch refresh.
        if git(repo, 'diff', '--cached', '--name-only', '--ignore-submodules=all', *(['--', *scope] if scope is not None else [])):
            raise RuntimeError(f'{name}: staged changes; no files changed')
        delta = git(repo, 'diff', '--binary', '--no-ext-diff', '--no-textconv', previous, desired)
        if delta:
            git(repo, 'apply', '--check', data=delta)
            git(repo, 'apply', '--whitespace=nowarn', data=delta)
    if needs_update and differences(repo, desired, env, scope):
        raise RuntimeError(f'{name}: materialized source differs from overlay')
    # A tree object may be pruned by explicit Git maintenance. If unavailable on a
    # later refresh, read-tree fails safely; never infer permission to erase edits.
    pending = state_path.with_suffix('.tmp')
    pending.write_text(json.dumps({'pin': expected, 'tree': desired, 'preparation_key': key}, indent=2) + '\n')
    pending.replace(state_path)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True, choices=patches.NAMES)
    parser.add_argument('--verify', action='store_true', help='check full source contents even when pins/patches are unchanged')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ps5-prepare-') as temporary:
        checked = prepare(args.target, *patches.target(args.target), patches.overlay_paths(args.target), Path(temporary), verify=args.verify)
    if checked:
        print(f'Prepared and verified: {args.target}', file=sys.stderr)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        raise SystemExit(str(error)) from error
