#!/usr/bin/env python3
"""Source-only submodule safety fixtures: no compiler, network or new commits."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('submodules', ROOT / 'ps5/tools/setup-submodules.py')
modules = importlib.util.module_from_spec(spec)
spec.loader.exec_module(modules)


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def rejected(action, message):
    try:
        action()
    except RuntimeError as error:
        assert message in str(error), error
    else:
        raise AssertionError(f'Accepted unsafe operation: {message}')


with tempfile.TemporaryDirectory(prefix='ps5-submodules-test-') as temporary:
    parent = Path(temporary) / 'parent'
    run('git', 'clone', '--quiet', '--shared', '--no-checkout', str(ROOT), str(parent))
    run('git', '-C', str(parent), 'checkout', '--quiet', '--detach', 'HEAD')
    pin = run('git', '-C', str(parent), 'rev-parse', 'HEAD')
    path = 'fixture-dependency'
    run('git', '-C', str(parent), 'update-index', '--add', '--cacheinfo', f'160000,{pin},{path}')
    run('git', '-C', str(parent), 'config', '-f', '.gitmodules', 'submodule.fixture.path', path)
    run('git', '-C', str(parent), 'config', '-f', '.gitmodules', 'submodule.fixture.url', str(ROOT))
    run('git', '-C', str(parent), 'config', 'protocol.file.allow', 'always')
    rejected(lambda: modules.ensure(parent, path, check=True), 'not initialized')
    repo = parent / path
    repo.mkdir()
    local = repo / 'developer.txt'
    local.write_text('preserve\n')
    rejected(lambda: modules.ensure(parent, path), 'nonempty directory')
    assert local.read_text() == 'preserve\n'
    local.unlink()
    # The local fixture source is explicitly allowed; production URL policy is unchanged.
    import os
    previous = {key: os.environ.get(key) for key in ('GIT_CONFIG_COUNT', 'GIT_CONFIG_KEY_0', 'GIT_CONFIG_VALUE_0')}
    os.environ.update(GIT_CONFIG_COUNT='1', GIT_CONFIG_KEY_0='protocol.file.allow', GIT_CONFIG_VALUE_0='always')
    try:
        modules.ensure(parent, path, pin)
    finally:
        for key, value in previous.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
    source = repo / 'README.md'
    source.write_text(source.read_text() + '\nlocal fixture change\n')
    before = source.read_bytes()
    modules.ensure(parent, path, pin, check=True)
    modules.ensure(parent, path, pin)
    assert source.read_bytes() == before
    rejected(lambda: modules.ensure(parent, path, '0' * 40), 'differs from upstream pin')
    run('git', '-C', str(parent), 'update-index', '--cacheinfo', f'160000,{"1" * 40},{path}')
    rejected(lambda: modules.ensure(parent, path), 'HEAD differs from gitlink')
    assert source.read_bytes() == before
    rejected(lambda: modules.gitlink(parent, 'README.md'), 'missing or conflicted')

print('Submodule safety checks passed: missing/adopted checkouts, pin mismatches and local edit preservation.')
