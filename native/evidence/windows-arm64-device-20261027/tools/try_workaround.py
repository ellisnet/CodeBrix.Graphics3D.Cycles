"""Diagnostic: render oracle scenes on the GPU with a WGSL edit applied through CODEBRIX_CYCLES_WGSL_LOAD.

python -I -X utf8 try_workaround.py OUT EDITS.json SCENE [SCENE ...]
EDITS.json: [[old, new], ...]; every generated module that contains an `old` gets an override
with all edits applied. Per scene: dump pass (no overrides), override pass (wgsl-override),
CPU pass, then native/oracle/compare.py (CPU film as reference). Never a validation run.
"""
import json,os,subprocess,sys
from pathlib import Path
REPO=Path(r'C:\GitHome\CodeBrix.Graphics3D.Cycles')
RUN=Path(os.environ.get('TRY_RUN_DIR',REPO/'native/work/windows/win-arm64/host/build/bin'))
out=Path(sys.argv[1]).resolve();edits=json.loads(Path(sys.argv[2]).read_text());out.mkdir(parents=True,exist_ok=False)
base={k:v for k,v in os.environ.items() if not k.startswith('CODEBRIX_CYCLES_')};base['CODEBRIX_CYCLES_WEBGPU_BACKEND']='d3d12'
def oracle(scene,backend,label,target,env):
    r=subprocess.run([str(RUN/'codebrix_cycles_oracle.exe'),'--scene',str(scene),'--backend',backend,'--label',label,'--output',str(target)],
        env=env,capture_output=True,text=True);(target.parent/(target.name+'.log')).write_text(r.stdout+r.stderr);return r.returncode
results={}
for scene in map(Path,sys.argv[3:]):
    name=scene.parent.name+'__'+scene.stem;d=out/name;wgsl=d/'wgsl';wgsl.mkdir(parents=True)
    env={**base,'CODEBRIX_CYCLES_WGSL_LOAD':str(wgsl)}
    oracle(scene,'webgpu','wgsl-dump',d/'dump',env)
    edited=[]
    for g in sorted(wgsl.glob('*.generated.wgsl')):
        text=g.read_text();new=text
        for old,rep in edits:new=new.replace(old,rep)
        if new!=text:(wgsl/g.name.replace('.generated.wgsl','.wgsl')).write_text(new,newline='');edited.append(g.name.split('.')[0])
    gpu=oracle(scene,'webgpu','wgsl-override',d/'gpu',env)
    cpu=oracle(scene,'cpu','cpu-arm64-windows',d/'cpu',base)
    report=d/'compare.json';metas=[next((d/k).rglob('meta.json'),None) for k in ('cpu','gpu')]
    if all(metas):subprocess.run([sys.executable,'-I','-X','utf8',str(REPO/'native/oracle/compare.py'),*[str(m.parent) for m in metas],'--json',str(report)],capture_output=True)
    p=json.loads(report.read_text())['pairs'][0] if report.exists() else {}
    results[name]=dict(edited=edited,gpuExit=gpu,cpuExit=cpu,practicalFailures=p.get('practicalFailures'),maxAbs=p.get('maxAbsoluteDifference'))
    print(name,json.dumps(results[name]),flush=True)
(out/'results.json').write_text(json.dumps(results,indent=1))
