#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Compile-only smoke step for the unspecialized surface module (warning only).

Scene specialization is the production path and every probe gate compiles
scene-specialized modules; the unspecialized module (every handler, the
CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 desktop diagnostic) is checked for
correctness by the specialization-off render gates (glass_render,
principled_render, nodes_render on/off bit-identity). This step only measures
it: for prepare_surface and shade_surface_light it records the module size, the
largest inlined entry size (drivers inline every WGSL call) and the cold
pipeline-creation time and peak memory, each in a fresh process with a fresh
Mesa shader cache. Sizes above the 2.3 MB Intel Android danger zone and times
above the 60 s pipeline budget are reported as warnings; this step never fails
a gate (its exit code is 0 unless the tool itself cannot run).

Usage: unspecialized_compile_smoke.py --wgsl SURFACE.wgsl --output DIR
  SURFACE.wgsl: the unspecialized surface module, e.g. the surface.wgsl capture
  of a render gate run with CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0
  (nodes_gate.py --unspecialized nodes_render writes it to
  <output>/nodes_render-unspecialized/surface.wgsl).
"""
import argparse, json, os, re, subprocess, sys, tempfile, time
from functools import lru_cache
from pathlib import Path
from build import ROOT, PACKAGE

DANGER_BYTES = 2_300_000
BUDGET_MS = 60_000
ENTRIES = ('prepare_surface', 'shade_surface_light')

def functions(text):
    text = re.sub(r'//[^\n]*', '', text); out = {}
    for m in re.finditer(r'\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(', text):
        b = text.index('{', m.end()); depth = 0; j = b
        while True:
            c = text[j]
            if c == '{': depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0: break
            j += 1
        out[m.group(1)] = text[m.start():j+1]
    return out

def inlined_bytes(text, entry):
    fns = functions(text)
    @lru_cache(maxsize=None)
    def size(name, depth=0):
        body = fns[name]; total = len(body)
        if depth > 64: return total
        for call in re.findall(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', body[body.index('{'):]):
            if call in fns and call != name: total += size(call, depth+1)
        return total
    return size(entry) if entry in fns else None

def build_probe(out):
    exe = out/'compile_probe'
    command = ['clang++', '-std=c++20', '-O2', '-I', str(ROOT/'src'), '-I', str(ROOT/'include'), '-I', str(PACKAGE/'include'),
               str(Path(__file__).with_name('compile_probe.cpp')), str(ROOT/'src/webgpu_device.cpp'),
               '-L', str(PACKAGE/'runtimes/linux-x64/native'), '-lcodebrix_wgpu', '-ldl', '-pthread',
               '-Wl,-rpath,'+str(PACKAGE/'runtimes/linux-x64/native'), '-o', str(exe)]
    subprocess.run(command, check=True)
    return exe

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--wgsl', type=Path, required=True); ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--timeout', type=int, default=900)
    a = ap.parse_args(); out = a.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    text = a.wgsl.read_text(); exe = build_probe(out)
    report = dict(tool='unspecialized_compile_smoke', wgsl=str(a.wgsl.resolve()), moduleBytes=len(text.encode()),
                  dangerBytes=DANGER_BYTES, budgetMs=BUDGET_MS, entries={}, warnings=[], gateFailure=False)
    for entry in ENTRIES:
        row = dict(inlinedBytes=inlined_bytes(text, entry))
        with tempfile.TemporaryDirectory(prefix='mesa-smoke-', dir=str(out)) as cache:
            env = dict(os.environ, MESA_SHADER_CACHE_DIR=cache); started = time.time()
            try:
                r = subprocess.run([str(exe), str(a.wgsl.resolve()), entry], capture_output=True, text=True, timeout=a.timeout, env=env)
                lines = r.stdout.strip().splitlines(); row.update(json.loads(lines[-1]) if lines else dict(passed=False, error=r.stderr[-800:]))
                row['returncode'] = r.returncode
            except subprocess.TimeoutExpired:
                row.update(passed=False, timeout=True)
            row['wallSeconds'] = round(time.time()-started, 1)
        if not row.get('passed'): report['warnings'].append(entry+': compile did not complete ('+str(row.get('error', 'timeout' if row.get('timeout') else 'exit '+str(row.get('returncode'))))+')')
        if row.get('inlinedBytes') and row['inlinedBytes'] > DANGER_BYTES: report['warnings'].append('%s: inlined %d bytes above the %d-byte Intel Android danger zone' % (entry, row['inlinedBytes'], DANGER_BYTES))
        if row.get('pipelineMs', 0) > BUDGET_MS: report['warnings'].append('%s: cold pipeline %.0f ms above the %d ms budget' % (entry, row['pipelineMs'], BUDGET_MS))
        report['entries'][entry] = row
    (out/'compile_probe').unlink()
    (out/'smoke.json').write_text(json.dumps(report, indent=1)+'\n')
    for w in report['warnings']: print('WARNING:', w, file=sys.stderr)
    print(json.dumps({k: report[k] for k in ('moduleBytes', 'warnings', 'gateFailure')}), flush=True)

if __name__ == '__main__': main()
