#!/usr/bin/env python3
"""Source-only delivery fixtures. No compiler, network or new Git commits."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'ps5/tools' / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


patches = module('patches', 'manage-patches.py')
prepare = module('prepare', 'prepare-source.py')
with tempfile.TemporaryDirectory(prefix='ps5-delivery-test-') as temporary:
    folder = Path(temporary)
    repo = folder / 'fixture'
    # Reuse an existing commit in an isolated local clone; never create commits.
    subprocess.run(['git', 'clone', '--quiet', '--shared', '--no-checkout', str(ROOT), str(repo)], check=True)
    subprocess.run(['git', '-C', str(repo), 'checkout', '--quiet', '--detach', 'HEAD'], check=True)

    def git(*args, **kwargs):
        return patches.git(repo, *args, **kwargs)

    pin = git('rev-parse', 'HEAD').decode().strip()
    source = repo / 'README.md'
    original = source.read_bytes()
    source.write_bytes(original + b'\nfixture upstream\n')
    upstream = folder / 'upstream.patch'
    upstream.write_bytes(git('diff', '--binary'))
    source.write_bytes(original + b'\nfixture upstream\nfixture PS5\n')
    (repo / 'fixture-new.cpp').write_text('owned fixture\n')
    git('add', 'README.md')
    before = (repo / '.git/index').read_bytes()
    before_status = git('status', '--porcelain')
    overlay = patches.generate('fixture', repo, pin, [upstream], folder)
    assert b'fixture-new.cpp' in overlay
    assert (repo / '.git/index').read_bytes() == before
    assert git('status', '--porcelain') == before_status
    for abbreviation in ('7', '12', '40'):
        git('config', 'core.abbrev', abbreviation)
        assert overlay == patches.generate('fixture', repo, pin, [upstream], folder)
    first = folder / 'first.patch'
    second = folder / 'second.patch'
    # Ordered prerequisite and PS5 overlay touch the same file.
    first.write_bytes(upstream.read_bytes())
    second.write_bytes(overlay)
    patches.verify_series('fixture', repo, pin, [], [(first, first.read_bytes()), (second, overlay)], folder)
    # Exercise the release audit against the same materialized dependency.
    # Only root discovery and the separate pin preflight are replaced by fixture data.
    real_root, real_names = patches.ROOT, patches.NAMES
    real_target, real_paths, real_git, real_run = patches.target, patches.overlay_paths, patches.git, subprocess.run
    def audit_git(directory, *args, **kwargs):
        if directory == folder and args == ('ls-files', '--stage', '-z'):
            return f'160000 {pin} 0\tfixture\0'.encode()
        return real_git(directory, *args, **kwargs)
    def audit_run(command, **kwargs):
        if len(command) > 1 and command[1] == str(folder / 'ps5/tools/setup-submodules.py'):
            return subprocess.CompletedProcess(command, 0)
        return real_run(command, **kwargs)
    patches.ROOT, patches.NAMES = folder, ('fixture',)
    patches.target = lambda name: (repo, pin, [upstream])
    patches.overlay_paths = lambda name: [second]
    patches.git, subprocess.run = audit_git, audit_run
    try:
        patches.verify_dependencies()
        saved_source = source.read_bytes()
        source.write_bytes(saved_source + b'unexported release edit\n')
        try:
            patches.verify_dependencies()
            raise AssertionError('release audit accepted an unexported dependency edit')
        except RuntimeError:
            pass
        source.write_bytes(saved_source)
        patches.verify_dependencies()
    finally:
        patches.ROOT, patches.NAMES = real_root, real_names
        patches.target, patches.overlay_paths = real_target, real_paths
        patches.git, subprocess.run = real_git, real_run
    (repo / 'unexported.cpp').write_text('must reject\n')
    try:
        patches.verify_series('fixture', repo, pin, [], [(first, first.read_bytes()), (second, overlay)], folder)
        raise AssertionError('unexported source accepted')
    except RuntimeError:
        pass
    (repo / 'unexported.cpp').unlink()
    try:
        patches.verify_series('fixture', repo, pin, [], [(second, overlay), (first, first.read_bytes())], folder)
        raise AssertionError('reordered dependent series accepted')
    except RuntimeError:
        pass
    git('reset', '--quiet', 'HEAD')  # disposable fixture's index only
    # Exact already-materialized legacy tree is adopted without overwriting files.
    prepare.prepare('fixture', repo, pin, [], [first, second], folder)
    state = (repo / prepare.STATE).read_bytes()
    assert source.read_bytes().endswith(b'fixture PS5\n')
    # Legacy state adoption checks only patch-owned paths, not unrelated trees.
    unrelated = repo / 'unrelated-dependency/local.txt'
    unrelated.parent.mkdir()
    unrelated.write_text('preserve unrelated developer files\n')
    import json
    legacy = json.loads(state)
    legacy.pop('preparation_key')
    (repo / prepare.STATE).write_text(json.dumps(legacy))
    real_git = prepare.git
    scoped_calls = []
    def scoped_git(repo, *args, **kwargs):
        if args[0] in ('diff', 'ls-files'):
            scoped_calls.append(args)
        return real_git(repo, *args, **kwargs)
    prepare.git = scoped_git
    try:
        assert prepare.prepare('fixture', repo, pin, [], [first, second], folder, verify=False) is True
    finally:
        prepare.git = real_git
    assert scoped_calls and all('--' in call for call in scoped_calls), scoped_calls
    assert all('unrelated-dependency/local.txt' not in call for call in scoped_calls)
    assert unrelated.read_text() == 'preserve unrelated developer files\n'
    unrelated.unlink()
    unrelated.parent.rmdir()
    state = (repo / prepare.STATE).read_bytes()
    # Stamp/state equality never permits destroying a later developer edit.
    source.write_bytes(source.read_bytes() + b'local edit\n')
    dirty = source.read_bytes()
    # An ordinary recompile must not walk/re-index a huge unchanged dependency.
    real_git = prepare.git
    calls = []
    def measured_git(repo, *args, **kwargs):
        calls.append(args[0])
        return real_git(repo, *args, **kwargs)
    prepare.git = measured_git
    try:
        assert prepare.prepare('fixture', repo, pin, [], [first, second], folder, verify=False) is False
    finally:
        prepare.git = real_git
    assert calls == ['rev-parse', 'rev-parse'], calls
    assert source.read_bytes() == dirty and (repo / prepare.STATE).read_bytes() == state
    # A changed input still takes the full safety path and refuses local edits.
    renamed = folder / 'renamed-topic.patch'
    renamed.write_bytes(second.read_bytes())
    try:
        prepare.prepare('fixture', repo, pin, [], [first, renamed], folder, verify=False)
        raise AssertionError('changed inputs bypassed local-edit protection')
    except RuntimeError as error:
        assert 'local or unrecorded edits' in str(error)

    try:
        prepare.prepare('fixture', repo, pin, [], [first, second], folder)
        raise AssertionError('edited materialization accepted')
    except RuntimeError as error:
        assert 'local or unrecorded edits' in str(error)
    assert source.read_bytes() == dirty and (repo / prepare.STATE).read_bytes() == state
    source.write_bytes(dirty.removesuffix(b'local edit\n'))
    (repo / 'unknown.txt').write_text('keep this\n')
    try:
        prepare.prepare('fixture', repo, pin, [], [first, second], folder)
        raise AssertionError('unknown file accepted')
    except RuntimeError as error:
        assert 'unknown.txt' in str(error)
    (repo / 'unknown.txt').unlink()
    # Export a patch refresh containing a deletion and a new file.
    (repo / 'fixture-new.cpp').unlink()
    (repo / 'fixture-next.cpp').write_text('next owned fixture\n')
    next_overlay = patches.generate('fixture', repo, pin, [upstream], folder)
    second.write_bytes(next_overlay)
    # Restore the recorded old materialization, then let preparation update it.
    (repo / 'fixture-next.cpp').unlink()
    (repo / 'fixture-new.cpp').write_text('owned fixture\n')
    git('add', 'README.md')
    staged_index = (repo / '.git/index').read_bytes()
    try:
        prepare.prepare('fixture', repo, pin, [], [first, second], folder)
        raise AssertionError('staged refresh accepted')
    except RuntimeError as error:
        assert 'staged changes' in str(error)
    assert (repo / '.git/index').read_bytes() == staged_index
    assert (repo / 'fixture-new.cpp').exists() and not (repo / 'fixture-next.cpp').exists()
    git('reset', '--quiet', 'HEAD')  # disposable fixture only
    prepare.prepare('fixture', repo, pin, [], [first, second], folder)
    assert not (repo / 'fixture-new.cpp').exists()
    assert (repo / 'fixture-next.cpp').read_text() == 'next owned fixture\n'
    # A broken prerequisite must fail before applying any working-tree changes.
    old_source = source.read_bytes()
    first.write_text('broken patch\n')
    try:
        prepare.prepare('fixture', repo, pin, [], [first, second], folder)
        raise AssertionError('broken topic accepted')
    except RuntimeError:
        pass
    assert source.read_bytes() == old_source
    (repo / 'fixture-next.cpp').write_text('bad whitespace  \n')
    try:
        patches.generate('fixture', repo, pin, [upstream], folder)
        raise AssertionError('whitespace accepted')
    except RuntimeError:
        pass
print('Delivery fixtures passed: ordered composition, deterministic export, index preservation, unchanged-stack fast path, dirty-tree refusal on refresh/verification, safe refresh/new files/deletions, failed-topic safety, whitespace rejection.')
