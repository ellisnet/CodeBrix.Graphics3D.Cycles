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
   "degraded" flag and the tier are those of run 1.
The guard never changes a result, shown without depending on the order of the WebGPU
film adds: both runs build the same pipelines from byte-identical WGSL modules (every
CODEBRIX_CYCLES_PIPELINE_LOG record's module, entry, sizes, specialization, tier, workgroup
size and features; every CODEBRIX_CYCLES_WGSL_DUMP module text), every WebGPU film
(PREFIX.KIND.film.bin beside PREFIX.cpu.film.bin: the render, its repeat, its batched
render) is within the standard practical criterion abs(gpu - cpu) <= 5e-5 + 2e-4*abs(cpu)
of that CPU film of its run with no NaN or Inf (specialization_check.against_cpu; strict
reported), and every other film or image (the CPU films and images included) is
bit-identical between the runs. WebGPU films are not repeat-identical in
the last bits (per-word atomic film adds in undefined order, an accepted property of the
device): their bit identity between the runs is reported as data ("gpuBitIdentity").
Films are looked for in the run's folder and below it, since some gates keep them per case. A run that writes no film fails.
Usage: pipeline_size_guard_check.py --output DIR [--gate glass_render] [--low-bytes 200000]
"""
import argparse, hashlib, json, os, subprocess
from pathlib import Path
from build import WORK
import specialization_check

DEFAULT_BYTES = 940000

# Pipeline-log fields that describe what was built (not how long it took or whether a cache served it).
BUILT = ('module', 'entry', 'specialized', 'specializationMode', 'tier', 'workgroupSize', 'features', 'wgslBytes', 'inlinedBytes')

def run(binary, folder, extra, timeout):
    folder.mkdir(parents=True); (folder/'wgsl').mkdir()
    env = dict(os.environ, MESA_SHADER_CACHE_DIR=str(folder/'shader-cache'), CODEBRIX_CYCLES_PIPELINE_LOG=str(folder/'pipelines.jsonl'),
               CODEBRIX_CYCLES_WGSL_DUMP=str(folder/'wgsl'), **extra)
    for name in ('CODEBRIX_CYCLES_SHADER_SPECIALIZATION', 'CODEBRIX_CYCLES_PIPELINE_SIZE_WARN_BYTES'):
        if name not in extra: env.pop(name, None)
    with (folder/'stdout.txt').open('w') as out, (folder/'stderr.txt').open('w') as err:
        subprocess.run([str(binary), str(folder/'run')], stdout=out, stderr=err, env=env, check=True, timeout=timeout)
    report = json.loads((folder/'stdout.txt').read_text())
    assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0
    cache = folder/'shader-cache'
    films = {str(p.relative_to(folder)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.rglob('*'))
             if p.is_file() and p.name.endswith(('.ppm', '.film.bin')) and cache not in p.parents and folder/'wgsl' not in p.parents}
    log = [json.loads(line) for line in (folder/'pipelines.jsonl').read_text().splitlines() if line.strip()] if (folder/'pipelines.jsonl').exists() else []
    built = sorted(json.dumps({k: r.get(k) for k in BUILT}, sort_keys=True) for r in log)
    modules = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((folder/'wgsl').iterdir())}
    cpu = {}
    for d in sorted({folder/Path(n).parent for n in films}):
        for name, row in specialization_check.against_cpu(d).items(): cpu[str((d/name).relative_to(folder))] = row
    return report['pipelineReport'], films, (folder/'stderr.txt').read_text(), built, modules, cpu

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--output', type=Path, required=True); ap.add_argument('--gate', default='glass_render')
    ap.add_argument('--low-bytes', type=int, default=200000); ap.add_argument('--timeout', type=int, default=1800)
    args = ap.parse_args(); out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    binary = WORK/'cycles-build/bin'/('codebrix_cycles_'+args.gate+'_gate')
    base, base_films, base_err, base_built, base_modules, base_cpu = run(binary, out/'default', {}, args.timeout)
    low, low_films, low_err, low_built, low_modules, low_cpu = run(binary, out/'lowered', {'CODEBRIX_CYCLES_PIPELINE_SIZE_WARN_BYTES': str(args.low_bytes)}, args.timeout)
    gpu = sorted(str(d/m) for d in sorted({Path(n).parent for n in base_films})
                 for m in specialization_check.webgpu_names([Path(x).name for x in base_films if Path(x).parent == d]))
    exact = sorted(n for n in base_films if n not in gpu)
    bits = {}
    for n in gpu:
        row = specialization_check.bit_identity(out/'default'/Path(n).parent, out/'lowered'/Path(n).parent, [Path(n).name])[Path(n).name]; bits[n] = row
    is_size = lambda w: 'size guard' in w
    guard, lowered = base['sizeGuard'], low['sizeGuard']
    record = dict(gate=args.gate, binarySha256=hashlib.sha256(binary.read_bytes()).hexdigest(), default=guard, lowered=lowered,
                  defaultSizeWarnings=[w for w in base['warnings'] if is_size(w)], loweredSizeWarnings=[w for w in low['warnings'] if is_size(w)],
                  films=base_films, filmsIdentical=bool(base_films) and base_films == low_films, gpuBitIdentity=bits,
                  againstCpu=dict(default=base_cpu, lowered=low_cpu), pipelines=len(base_built), modules=base_modules,
                  degraded=[base['degraded'], low['degraded']], tier=[base['tier'], low['tier']])
    checks = {
        'default threshold reported': guard['thresholdBytes'] == DEFAULT_BYTES,
        'largest entry measured': 0 < guard['largestEntryBytes'] < DEFAULT_BYTES,
        'no size warning at the default threshold': guard['warnings'] == 0 and not record['defaultSizeWarnings'],
        'lowered threshold reported': lowered['thresholdBytes'] == args.low_bytes,
        'size warnings raised when lowered': lowered['warnings'] >= 1 and len(record['loweredSizeWarnings']) == lowered['warnings'],
        'warnings on stderr': all(w in low_err for w in record['loweredSizeWarnings']) and 'size guard' not in base_err,
        'degraded and tier unchanged': base['degraded'] == low['degraded'] and base['tier'] == low['tier'],
        'same pipelines built': bool(base_built) and base_built == low_built,
        'byte-identical WGSL modules': bool(base_modules) and base_modules == low_modules,
        'same film and image files': bool(base_films) and sorted(base_films) == sorted(low_films),
        'CPU films and other files bit-identical': all(base_films[n] == low_films.get(n) for n in exact),
        'WebGPU films within practical of their CPU films': bool(base_cpu) and bool(low_cpu) and all(r['passed'] for r in list(base_cpu.values())+list(low_cpu.values())),
    }
    record['checks'] = checks; record['passed'] = all(checks.values())
    (out/'size-guard.json').write_text(json.dumps(record, indent=2)+'\n'); print(json.dumps({'passed': record['passed'], 'checks': checks}))
    assert record['passed'], checks

if __name__ == '__main__':
    main()
