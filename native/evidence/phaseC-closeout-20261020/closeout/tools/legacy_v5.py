# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase C close-out: the legacy v5 replay step of Phase C1 restored_regressions_phaseC.py, alone.
# The cache (cold scratch dir or warm user cache) is chosen by the caller's environment.
import contextlib,json,sys
from pathlib import Path
root=Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles');sys.path.insert(0,str(root/'native/tools'))
import render_replay
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=False)
legacy=root/'native/work/parallel-integration/all-lanes-validation-v2/transparent_shadow_gate/transparent_lighting/run/case-1'
with (out/'legacy-v5-replay.log').open('w') as log,contextlib.redirect_stdout(log):render_replay.run('linux-x64',legacy,out/'legacy-v5-replay')
r=json.loads((out/'legacy-v5-replay/validation.json').read_text())
print(json.dumps({'passed':r.get('passed'),'readbacks':r['results']['exactSchedulingReadbacks'],'max':r['maximumAbsoluteImageError'],'strict':r['strictImageFailures']}))
