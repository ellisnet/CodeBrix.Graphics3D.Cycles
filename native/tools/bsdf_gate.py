#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Native closure-setup, path-state and independent image gates of the remaining
SVM BSDF families: Metallic (physical conductor and F82 tint), Beckmann,
Ashikhmin-Shirley, Ashikhmin velvet and Toon (native/cycles/bsdf_fixture.h)."""
import argparse
import json
import subprocess
from pathlib import Path
from build import REPO, WORK, PACKAGE
from device_gate import sha, save
from render_gate import inputs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    status = {'complete': False, 'phase': 'prepare'}
    save(out/'status.json', status)
    sources = inputs()
    runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    pins = {str(runtime): sha(runtime)}
    reports = {}
    kinds = ['bsdf_svm', 'bsdf_bounce', 'bsdf_render', 'bsdf2_svm', 'bsdf2_bounce', 'bsdf2_render', 'bsdf3_svm', 'bsdf3_bounce', 'bsdf3_render']
    try:
        if not args.skip_build:
            status['phase'] = 'build'
            save(out/'status.json', status)
            command = ['python3', str(REPO/'native/tools/build_cycles.py'), '--jobs', '2']
            for kind in kinds:
                command += ['--target', 'codebrix_cycles_'+kind+'_gate']
            with (out/'build.log').open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        for kind in kinds:
            binary = WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')
            pins[str(binary)] = sha(binary)
        save(out/'invocation.json', {'productionSourceSha256': sources, 'fileSha256': pins})
        failed = []
        for kind in kinds:
            status['phase'] = kind
            save(out/'status.json', status)
            folder = out/kind
            folder.mkdir()
            with (folder/'stdout.txt').open('w') as stdout, (folder/'stderr.txt').open('w') as stderr:
                result = subprocess.run([str(WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')), str(folder/'run')],
                                        stdout=stdout, stderr=stderr, timeout=900)
            # Every gate runs; a failing one is recorded with its last stderr line and fails the driver at the end.
            if result.returncode != 0:
                lines = (folder/'stderr.txt').read_text().strip().splitlines()
                failed.append({'gate': kind, 'exit': result.returncode, 'reason': lines[-1] if lines else ''})
                save(out/'failed.json', failed)
                continue
            report = json.loads((folder/'stdout.txt').read_text())
            assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0
            assert Path((folder/'run.library.txt').read_text().strip()).resolve() == runtime.resolve()
            if kind.endswith('_svm'):
                # bsdf3 adds the Radial Tiling refusals: two sides with 0 < roundness < 1, four
                # constant-input programs refused natively and one linked program reporting 16.
                radial = kind == 'bsdf3_svm'
                assert report['originalCpuSurfaceSvm'] and report['compiledGraphs'] == 20
                assert report['invalidCasesRejected'] == (12 if radial else 11)
                assert report['nativeUnsupportedFeatureRejections'] == (4 if radial else 0)
            if kind.endswith('_bounce'):
                assert report['originalCpuSurfaceIntegrator'] and report['cases'] == 30 and report['invalidCasesRejected'] == 21
            if kind.endswith('_render'):
                assert report['independentCpuImage'] and report['repeatBitwiseIdentical']
            reports[kind] = report
            save(out/'results.json', reports)
        assert sources == inputs() and all(sha(Path(name)) == digest for name, digest in pins.items())
        assert not failed, 'Failed gates: '+', '.join(f['gate'] for f in failed)
        status.update(complete=True, passed=True, phase='complete', inputsUnchanged=True,
                      scope='Metallic (physical conductor and F82 tint; GGX, multiscatter GGX, Beckmann), Beckmann glossy/refraction/glass, '
                            'Ashikhmin-Shirley, Ashikhmin velvet, diffuse/glossy Toon and the Radial Tiling node (two sides with 0 < roundness < 1 refused): closure setup against the original CPU SVM, '
                            'surface integration against the original CPU integrator, independent original-Cycles images.')
    except Exception as error:
        status.update(complete=True, passed=False, error=repr(error))
        raise
    finally:
        save(out/'status.json', status)
    print(json.dumps(status), flush=True)


if __name__ == '__main__':
    main()
