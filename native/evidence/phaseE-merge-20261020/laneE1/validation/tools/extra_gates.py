#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Run the SVM/attribute/image/normal/world/render gate binaries that the
all-lanes coordinator builds but does not execute (Linux only, no evidence copy).
Each binary's own assertions decide pass/fail; outputs are kept for comparison."""
import argparse, json, subprocess, time
from pathlib import Path
GATES = [('attribute', 'attribute', []), ('render-attributes', 'render', ['--attributes']),
         ('image', 'image', []), ('render-images', 'render', ['--images']),
         ('normal', 'normal', []), ('render-normals', 'render', ['--normals']),
         ('image_file', 'image_file', []), ('svm_world', 'svm_world', []), ('background', 'background', []),
         ('render', 'render', []), ('surface', 'surface', []), ('closest', 'closest', []),
         ('svm_surface', 'svm_surface', []), ('shader_data', 'shader_data', []), ('camera', 'camera', [])]
ap = argparse.ArgumentParser(); ap.add_argument('--bin', type=Path, required=True); ap.add_argument('--output', type=Path, required=True)
ap.add_argument('--only', action='append')
a = ap.parse_args(); a.output.mkdir(parents=True, exist_ok=False)
status = {}
for name, binary, args in GATES:
    if a.only and name not in a.only: continue
    folder = a.output/name; folder.mkdir()
    command = [str(a.bin/('codebrix_cycles_'+binary+'_gate')), str(folder/'run'), *args]
    t = time.time()
    with (folder/'stdout.txt').open('w') as out, (folder/'stderr.txt').open('w') as err:
        rc = subprocess.run(command, stdout=out, stderr=err, timeout=1800).returncode
    status[name] = {'command': command, 'returncode': rc, 'seconds': round(time.time()-t, 1)}
    try: status[name]['passed'] = json.loads((folder/'stdout.txt').read_text()).get('passed')
    except Exception: status[name]['passed'] = False
    (a.output/'status.json').write_text(json.dumps(status, indent=1))
    print(name, rc, status[name]['passed'], flush=True)
