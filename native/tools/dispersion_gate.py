#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Native gates of dispersion against the original CPU kernels: the closure records of dispersive Principled BSDF graphs
and typed records (dispersion_svm), two sets of graphs through the surface stage (dispersion_bounce, dispersion_bounce2;
native/cycles/dispersion_fixture.h), manifold next-event estimation through dispersive casters (dispersion_mnee;
native/cycles/mnee_fixture.h) and an independent image with lockstep stage checks (dispersion_render)."""
import argparse
import json
import subprocess
from pathlib import Path
from build import REPO, WORK, PACKAGE
from device_gate import sha, save
from render_gate import inputs


KINDS = ['dispersion_svm', 'dispersion_bounce', 'dispersion_bounce2', 'dispersion_mnee', 'dispersion_render']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--kind', action='append', choices=KINDS, help='run only this gate program (repeatable; default: all)')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    status = {'complete': False, 'phase': 'prepare'}
    save(out/'status.json', status)
    sources = inputs()
    runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    pins = {str(runtime): sha(runtime)}
    reports = {}
    kinds = args.kind or KINDS
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
            if not binary.exists():
                raise RuntimeError('missing program '+str(binary)+': run this gate once without --skip-build')
            pins[str(binary)] = sha(binary)
        save(out/'invocation.json', {'productionSourceSha256': sources, 'fileSha256': pins})
        for kind in kinds:
            status['phase'] = kind
            save(out/'status.json', status)
            folder = out/kind
            folder.mkdir()
            with (folder/'stdout.txt').open('w') as stdout, (folder/'stderr.txt').open('w') as stderr:
                subprocess.run([str(WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')), str(folder/'run')],
                               stdout=stdout, stderr=stderr, check=True, timeout=1800)
            report = json.loads((folder/'stdout.txt').read_text())
            assert report['passed']
            assert Path((folder/'run.library.txt').read_text().strip()).resolve() == runtime.resolve()
            if kind == 'dispersion_svm':
                # The compiled graphs' cases (9), the fixture's typed programs (14) and the typed dispersion records (129).
                assert report['originalCpuSurfaceSvm'] and report['compiledGraphs'] == 20 and report['cases'] == 152
                assert report['invalidCasesRejected'] == 16 and report['nativeUnsupportedFeatureRejections'] == 0
                assert report['cyclesAllocationBytesAfterCleanup'] == 0
            if kind in ('dispersion_bounce', 'dispersion_bounce2'):
                # The compiled materials, 32 surface cases (the principled fixture's 26, glossy filtering and the five
                # dispersion cases), the dense dispatch and the zero-work dispatch.
                assert report['originalCpuSurfaceIntegrator'] and report['cases'] == 35 and report['invalidCasesRejected'] == 21
            if kind == 'dispersion_mnee':
                # Cases 0, 1 and 2 of the caustic fixture with dispersive casters.
                assert report['originalCpuIntersectMnee'] and [case['case'] for case in report['cases']] == [0, 1, 2]
                assert report['paths'] > 1000 and report['sampled'] >= 20
            if kind == 'dispersion_render':
                assert report['independentCpuImage'] and report['repeatBitwiseIdentical'] and report['cyclesAllocationBytesAfterCleanup'] == 0
            reports[kind] = report
            save(out/'results.json', reports)
        assert sources == inputs() and all(sha(Path(name)) == digest for name, digest in pins.items())
        status.update(complete=True, passed=True, phase='complete', inputsUnchanged=True,
                      kinds=kinds,
                      scope='Dispersion: the wavelength sample, the dispersive index of refraction and the throughput weight, on dispersive '
                            'Principled BSDF graphs and typed records, in the closure records, the surface stage, manifold next-event '
                            'estimation and an independent image, against the original CPU kernels')
    except Exception as error:
        status.update(complete=True, passed=False, error=repr(error))
        raise
    finally:
        save(out/'status.json', status)
    print(json.dumps(status), flush=True)


if __name__ == '__main__':
    main()
