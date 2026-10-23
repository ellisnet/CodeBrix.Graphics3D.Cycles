#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Linux gate check for the pipeline-build size guard (native/cycles/pipelines.h).

Runs one render gate (default glass_render) twice from the same binary:
1. with the default threshold: the process report's "sizeGuard" states the
   threshold (pipeline_size_warning_default_bytes) and the largest inlined entry,
   and no size warning is raised (every gate entry is below the threshold);
2. with CODEBRIX_CYCLES_PIPELINE_SIZE_WARN_BYTES lowered below the gate's entries:
   the size warnings appear in the report's "warnings" and on stderr, the
   "degraded" flag and the tier are those of run 1, and every film is
   bit-identical to run 1 (the guard never changes a result).
Usage: pipeline_size_guard_check.py --output DIR [--gate glass_render] [--low-bytes 200000]
"""
import argparse, hashlib, json, os, subprocess
from pathlib import Path
from build import WORK

DEFAULT_BYTES = 1110000

def run(binary, folder, extra, timeout):
    folder.mkdir(parents=True)
    env = dict(os.environ, MESA_SHADER_CACHE_DIR=str(folder/'shader-cache'), **extra)
    for name in ('CODEBRIX_CYCLES_SHADER_SPECIALIZATION', 'CODEBRIX_CYCLES_PIPELINE_SIZE_WARN_BYTES'):
        if name not in extra: env.pop(name, None)
    with (folder/'stdout.txt').open('w') as out, (folder/'stderr.txt').open('w') as err:
        subprocess.run([str(binary), str(folder/'run')], stdout=out, stderr=err, env=env, check=True, timeout=timeout)
    report = json.loads((folder/'stdout.txt').read_text())
    assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0
    films = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.iterdir()) if p.name.endswith(('.ppm', '.film.bin'))}
    return report['pipelineReport'], films, (folder/'stderr.txt').read_text()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--output', type=Path, required=True); ap.add_argument('--gate', default='glass_render')
    ap.add_argument('--low-bytes', type=int, default=200000); ap.add_argument('--timeout', type=int, default=1800)
    args = ap.parse_args(); out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    binary = WORK/'cycles-build/bin'/('codebrix_cycles_'+args.gate+'_gate')
    base, base_films, base_err = run(binary, out/'default', {}, args.timeout)
    low, low_films, low_err = run(binary, out/'lowered', {'CODEBRIX_CYCLES_PIPELINE_SIZE_WARN_BYTES': str(args.low_bytes)}, args.timeout)
    is_size = lambda w: 'size guard' in w
    guard, lowered = base['sizeGuard'], low['sizeGuard']
    record = dict(gate=args.gate, binarySha256=hashlib.sha256(binary.read_bytes()).hexdigest(), default=guard, lowered=lowered,
                  defaultSizeWarnings=[w for w in base['warnings'] if is_size(w)], loweredSizeWarnings=[w for w in low['warnings'] if is_size(w)],
                  films=base_films, filmsIdentical=bool(base_films) and base_films == low_films,
                  degraded=[base['degraded'], low['degraded']], tier=[base['tier'], low['tier']])
    checks = {
        'default threshold reported': guard['thresholdBytes'] == DEFAULT_BYTES,
        'largest entry measured': 0 < guard['largestEntryBytes'] < DEFAULT_BYTES,
        'no size warning at the default threshold': guard['warnings'] == 0 and not record['defaultSizeWarnings'],
        'lowered threshold reported': lowered['thresholdBytes'] == args.low_bytes,
        'size warnings raised when lowered': lowered['warnings'] >= 1 and len(record['loweredSizeWarnings']) == lowered['warnings'],
        'warnings on stderr': all(w in low_err for w in record['loweredSizeWarnings']) and 'size guard' not in base_err,
        'degraded and tier unchanged': base['degraded'] == low['degraded'] and base['tier'] == low['tier'],
        'films bit-identical': record['filmsIdentical'],
    }
    record['checks'] = checks; record['passed'] = all(checks.values())
    (out/'size-guard.json').write_text(json.dumps(record, indent=2)+'\n'); print(json.dumps({'passed': record['passed'], 'checks': checks}))
    assert record['passed'], checks

if __name__ == '__main__':
    main()
