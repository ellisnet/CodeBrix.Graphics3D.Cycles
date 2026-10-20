#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build the reviewed dependency profile for Android API 33 from local sources."""
import argparse
import importlib.util
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from android_toolchain import AndroidToolchain, TARGETS, environment, sha

spec = importlib.util.spec_from_file_location('codebrix_dependency_recipe', ROOT/'tools/dependencies/build.py')
recipe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recipe)


def save(path, data):
    path.write_text(json.dumps(data, indent=2)+'\n')


def workdir(rid):
    return ROOT/'native/work/android'/rid/'dependencies'


def identity(toolchain):
    return {**toolchain.identity(), 'sourceManifestSha256': sha(ROOT/'third_party/dependency-imports.json'),
            'profileRecipeSha256': sha(ROOT/'tools/dependencies/build.py'),
            'androidRecipeSha256': sha(Path(__file__)),
            'toolchainRecipeSha256': sha(ROOT/'tools/android_toolchain.py')}


def prefix(rid):
    """Reuse only a complete, matching source-built target dependency prefix."""
    recipe.verify()
    installed = workdir(rid)/'install'
    marker = installed/'codebrix-android-build.json'
    if not marker.exists():
        raise RuntimeError(f'Build Android dependencies first: python3 tools/dependencies/build_android.py --rid {rid}')
    report = json.loads(marker.read_text())
    if report['identity'] != identity(AndroidToolchain(rid)):
        raise RuntimeError('Android dependency inputs/toolchain changed; rebuild the target prefix')
    for name, digest in report['libraries'].items():
        if sha(installed/name) != digest:
            raise RuntimeError('Changed Android dependency library: '+name)
    return installed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rid', required=True, choices=TARGETS)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    toolchain = AndroidToolchain(args.rid)
    manifest = recipe.verify()
    work = workdir(args.rid)
    work.mkdir(parents=True, exist_ok=True)
    source, installed = work/'sources', work/'install'
    installed.mkdir(exist_ok=True)
    pin = identity(toolchain)
    expected = {str(Path(component['id'])/entry['path']):entry['sha256']
                for component in manifest['components'] for entry in component['files']}
    if not source.exists():
        shutil.copytree(recipe.SOURCE, source)

    def verify_copy():
        for name, digest in expected.items():
            if sha(source/name) != digest:
                raise RuntimeError('Target dependency source changed: '+name)

    verify_copy()
    save(work/'provenance.json', pin)
    env = environment(installed, args.jobs)
    completed = []
    state = dict(complete=False, identity=pin, completedComponents=completed)
    try:
        for name, path, options in recipe.profile(source):
            build = work/name
            build.mkdir(exist_ok=True)
            settings = {**toolchain.options(installed), 'CMAKE_INSTALL_PREFIX':installed,
                        'CMAKE_INSTALL_LIBDIR:STRING':'lib', 'BUILD_SHARED_LIBS':'ON', 'BUILD_TESTING':'OFF',
                        **options}
            if name in ('imath', 'openexr', 'oiio'):
                settings['CLANG_FORMAT_EXE:FILEPATH'] = 'OFF'
            commands = [['cmake', '-S', str(path), '-B', str(build),
                         *['-D'+k+'='+str(v) for k, v in settings.items()]],
                        ['cmake', '--build', str(build), '--parallel', str(args.jobs)],
                        ['cmake', '--install', str(build)]]
            save(build/'commands.json', commands)
            for step, command in zip(['configure', 'build', 'install'], commands):
                state.update(component=name, step=step)
                save(work/'status.json', state)
                with (build/(step+'.log')).open('w') as log:
                    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env, check=True)
                if step == 'configure':
                    for line in (build/'CMakeCache.txt').read_text().splitlines():
                        if line.startswith('CMAKE_INSTALL_LIBDIR:'):
                            directory = Path(line.split('=', 1)[1])
                            assert not directory.is_absolute() or directory.is_relative_to(installed)
                if step == 'install':
                    assert all(Path(line).resolve().is_relative_to(installed)
                               for line in (build/'install_manifest.txt').read_text().splitlines())
            completed.append(name)
            print(args.rid, name, 'built and installed', flush=True)
        recipe.verify()
        verify_copy()
        assert pin == identity(toolchain)
        libraries = {str(p.relative_to(installed)):sha(p) for p in (installed/'lib').rglob('*')
                     if p.is_file() and not p.is_symlink() and ('.so' in p.name or p.suffix == '.a')}
        save(installed/'codebrix-android-build.json', dict(identity=pin, libraries=libraries))
        state.update(complete=True, passed=True, libraries=libraries)
    except Exception as error:
        state.update(complete=True, passed=False, error=str(error))
        raise
    finally:
        save(work/'status.json', state)


if __name__ == '__main__':
    main()
