# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
import contextlib,json,subprocess,sys
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'native/tools'))
from build import WORK,PACKAGE
from device_gate import save,sha
from render_gate import inputs
import render_replay
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=False)
kinds={'broad_lighting':12,'transparent_lighting':8,'object_motion_render':8,'deforming_motion_render':8}
binaries={k:WORK/'cycles-build/bin'/('codebrix_cycles_'+k+'_gate') for k in [*kinds,'camera','render']}
runtime=PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
render_replay.build('linux-x64')
source=inputs();pins={str(p):sha(p) for p in [*binaries.values(),runtime,render_replay.BASE/'linux-x64/render_replay']}
save(out/'source-pins.json',source);save(out/'binary-runtime-pins.json',pins)
state={'complete':False};reports={}
def phase(n):state['phase']=n;save(out/'status.json',state);print(n,flush=True)
try:
 for k,cases in kinds.items():
  phase(k);folder=out/k;folder.mkdir();command=[str(binaries[k]),str(folder/'run')];save(folder/'invocation.json',{'command':command})
  with (folder/'stdout.txt').open('w') as stdout,(folder/'stderr.txt').open('w') as stderr:subprocess.run(command,stdout=stdout,stderr=stderr,check=True,timeout=900)
  report=json.loads((folder/'stdout.txt').read_text());assert report['passed'] and report['cases']==cases and report['cyclesAllocationBytesAfterCleanup']==0
  assert Path((folder/'run/library.txt').read_text().strip()).resolve()==runtime.resolve()
  reports[k]=report;save(out/'results.json',reports)
  phase(k+'-replay')
  with (out/(k+'-replay.log')).open('w') as log,contextlib.redirect_stdout(log):render_replay.run('linux-x64',folder/'run/case-1',out/(k+'-replay'))
  reports[k+'-replay']=json.loads((out/(k+'-replay')/'validation.json').read_text());save(out/'results.json',reports)
 phase('camera-motion')
 with (out/'camera.log').open('w') as log:subprocess.run([sys.executable,str(root/'native/tools/camera_motion_gate.py'),'--skip-build','--output',str(out/'camera')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=900)
 reports['camera']=json.loads((out/'camera/validation.json').read_text());save(out/'results.json',reports)
 phase('legacy-v5-replay')
 legacy=root/'native/work/parallel-integration/all-lanes-validation-v2/transparent_shadow_gate/transparent_lighting/run/case-1'
 with (out/'legacy-v5-replay.log').open('w') as log,contextlib.redirect_stdout(log):render_replay.run('linux-x64',legacy,out/'legacy-v5-replay')
 reports['legacy-v5-replay']=json.loads((out/'legacy-v5-replay/validation.json').read_text());save(out/'results.json',reports)
 state.update(complete=True,passed=True,phase='complete',imageScenes=45,newReplays=4,legacyReplays=1)
except BaseException as e:state.update(complete=True,passed=False,error=repr(e));raise
finally:
 state['inputsUnchanged']=source==inputs() and all(sha(Path(p))==h for p,h in pins.items())
 if not state['inputsUnchanged']:state['passed']=False
 save(out/'status.json',state)
assert state['passed']
