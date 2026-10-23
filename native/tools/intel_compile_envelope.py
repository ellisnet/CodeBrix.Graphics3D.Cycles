#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Intel Mesa 25.0.7 compile-envelope check (Linux maintainer tool).

Mesa 25.0.7's brw_opt_combine_constants keeps a constant's VGRF number and its
first/last-use instruction IPs in 16-bit fields (fixed upstream in Mesa commit
b7e1ac830984, issue #9489, not in 25.0.7). An entry point whose backend IR has more
than 65,535 instructions or virtual registers when that pass runs is miscompiled
or crashes the compiler (native/DEVICE.md, "Linux reference fixes on the updated
Mesa"). This tool compiles every entry point of the WGSL modules a gate dumped
(CODEBRIX_CYCLES_WGSL_DUMP=DIR) in a fresh process with a cold Mesa cache, under gdb
with a breakpoint on brw_opt_combine_constants, and records the largest
instruction IP and VGRF count per entry. It fails when any value reaches --limit
(default 60,000, about 8% below the 16-bit range).

Prerequisites: gdb and the Mesa debug information for the installed
libvulkan_intel.so (for example a debuginfod cache), exposed through
--debug-file-directory (a directory with .build-id/ and .dwz/ links).
Usage: intel_compile_envelope.py --dump DIR --debug-file-directory DIR --output DIR [--limit N]
"""
import argparse, json, os, re, subprocess, sys, tempfile
from pathlib import Path
import unspecialized_compile_smoke as smoke

GDB_SCRIPT = '''set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
set libthread-db-search-path /nonexistent
set debug-file-directory {debug}
handle SIGPIPE nostop noprint pass
break brw_opt_combine_constants
commands
silent
printf "CCPROBE alloc=%u end_ip=%d simd=%u name=%s\\n", s.alloc.count, s.cfg->blocks[s.cfg->num_blocks-1]->end_ip, s.dispatch_width, ((nir_function*)s.nir->functions.head_sentinel.next)->name
continue
end
run
quit
'''

def probe(exe, script, wgsl, entry, scratch, timeout):
    env = dict(os.environ, MESA_SHADER_CACHE_DIR=tempfile.mkdtemp(dir=scratch))
    proc = subprocess.Popen(['gdb', '-q', '-batch', '-x', str(script), '--args', str(exe), str(wgsl), entry],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env, start_new_session=True)
    try:
        out, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill(); out, _ = proc.communicate()
    rows = [dict(alloc=int(a), endIp=int(b), simd=int(c)) for a, b, c, n in
            re.findall(r'CCPROBE alloc=(\d+) end_ip=(\d+) simd=(\d+) name=(\S+)', out) if n == entry]
    return rows, ('Pipeline created' in out or 'pipelineMs' in out or bool(rows)), out[-2000:]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dump', type=Path, required=True); ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--debug-file-directory', type=Path, required=True)
    ap.add_argument('--limit', type=int, default=60000); ap.add_argument('--timeout', type=int, default=1200)
    ap.add_argument('--modules', default='', help='comma-separated module names to check (default: all dumped modules)')
    ap.add_argument('--entries', default='', help='comma-separated entry points to check (default: all)')
    args = ap.parse_args(); out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    script = out/'probe.gdb'; script.write_text(GDB_SCRIPT.format(debug=args.debug_file_directory.resolve()))
    exe = smoke.build_probe(out); scratch = out/'mesa'; scratch.mkdir()
    results = []; seen = {}
    modules = {m for m in args.modules.split(',') if m}; wanted = {e for e in args.entries.split(',') if e}
    for wgsl in sorted(args.dump.rglob('[0-9]*-*.wgsl')):
        if modules and wgsl.stem.split('-', 1)[1] not in modules: continue
        text = wgsl.read_text()
        for entry in re.findall(r'@compute @workgroup_size\(\d+\) fn (\w+)\(', text):
            if wanted and entry not in wanted: continue
            key = (entry, hash(text))
            if key in seen: continue
            seen[key] = True
            rows, ok, tail = probe(exe, script, wgsl, entry, scratch, args.timeout)
            row = dict(module=str(wgsl.relative_to(args.dump)), entry=entry, inlinedBytes=smoke.inlined_bytes(text, entry),
                       maxEndIp=max((r['endIp'] for r in rows), default=None), maxAlloc=max((r['alloc'] for r in rows), default=None), variants=rows)
            row['withinLimit'] = row['maxEndIp'] is not None and row['maxEndIp'] < args.limit and row['maxAlloc'] < args.limit
            if row['maxEndIp'] is None: row['probeTail'] = tail
            results.append(row); print(json.dumps({k: row[k] for k in ('module', 'entry', 'inlinedBytes', 'maxEndIp', 'maxAlloc', 'withinLimit')}), flush=True)
    passed = bool(results) and all(r['withinLimit'] for r in results)
    (out/'envelope.json').write_text(json.dumps(dict(passed=passed, limit=args.limit, entries=results), indent=2)+'\n')
    for p in sorted(scratch.rglob('*'), reverse=True): p.unlink() if p.is_file() else p.rmdir()
    print(json.dumps(dict(passed=passed, entries=len(results), worstEndIp=max((r['maxEndIp'] or 0) for r in results) if results else None)))
    sys.exit(0 if passed else 1)

if __name__ == '__main__':
    main()
