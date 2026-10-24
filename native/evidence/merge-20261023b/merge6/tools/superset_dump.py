#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 6 (copy of merge wave 4's superset_dump.py): one render gate with specialization on (film hash) and the superset on/off identity check
(native/tools/specialization_check.py, every group), dumping every compiled WGSL module for the Intel
compile-envelope check. Usage: superset_dump.py GATE OUT_DIR TIMEOUT_S"""
import hashlib, json, os, subprocess, sys
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO/'native/tools'))
import specialization_check
gate, out, timeout = sys.argv[1], Path(sys.argv[2]).resolve(), int(sys.argv[3])
binary = REPO/'native/work/cycles-build/bin'/('codebrix_cycles_'+gate+'_gate')
on = out/'on'; on.mkdir(parents=True)
(out/'dump/on').mkdir(parents=True); (out/'dump/superset').mkdir(parents=True)  # the dump writer does not create folders
env = dict(os.environ, CODEBRIX_CYCLES_PIPELINE_LOG=str(on/'pipelines.jsonl'), CODEBRIX_CYCLES_WGSL_DUMP=str(out/'dump/on'))
env.pop('CODEBRIX_CYCLES_SHADER_SPECIALIZATION', None)
with (on/'stdout.txt').open('w') as so, (on/'stderr.txt').open('w') as se:
    subprocess.run([str(binary), str(on/'run')], stdout=so, stderr=se, check=True, timeout=timeout, env=env)
report = json.loads((on/'stdout.txt').read_text()); assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0
films = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(on.iterdir()) if p.name.endswith(('.ppm', '.film.bin'))}
os.environ['CODEBRIX_CYCLES_WGSL_DUMP'] = str(out/'dump/superset')
record = specialization_check.check(binary, on, out/'off', 'superset', timeout)
summary = dict(gate=gate, passed=True, onFilms=films, supersetIdentical=record['identical'], groups=len(record['runs']),
               covered=record['covered'], unfit=record['unfit'])
(out/'summary.json').write_text(json.dumps(summary, indent=2)+'\n'); print(json.dumps(summary))
