"""Judge win-arm64 oracle films against the Linux x64 glibc CPU films with native/oracle/compare.py.

python -I -X utf8 linux_compare.py OUT RUN_DIR [RUN_DIR ...]
Each RUN_DIR is a render_oracles.py output (oracle/<scene>/<backend>/). For every scene with a
Linux film, compare.py gets [linux cpu-x64-linux, our backend] (one pair per backend), so the
Linux film is the reference b of the practical criterion.
"""
import json,subprocess,sys
from pathlib import Path
REPO=Path(r'C:\GitHome\CodeBrix.Graphics3D.Cycles')
LINUX=[REPO/'native/evidence/linux-x64-docker-reference-20261027/oracles'/n/'oracle' for n in ('linux-docker-2','linux-docker-bench-2')]
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
summary={}
for run in map(Path,sys.argv[2:]):
    for meta in sorted((run/'oracle').rglob('meta.json')):
        backend=meta.parent;scene=backend.parent.relative_to(run/'oracle').as_posix()
        ref=next((l/scene/'cpu-x64-linux' for l in LINUX if (l/scene/'cpu-x64-linux'/'meta.json').exists()),None)
        if ref is None:summary.setdefault(scene,{})[backend.name]='no Linux film';continue
        report=out/(scene.replace('/','__')+'__'+backend.name+'.json')
        subprocess.run([sys.executable,'-I','-X','utf8',str(REPO/'native/oracle/compare.py'),str(ref),str(backend),'--json',str(report)],capture_output=True,text=True)
        r=json.loads(report.read_text());p=r['pairs'][0]
        summary.setdefault(scene,{})[backend.name]=dict(practicalFailures=p['practicalFailures'],strictDifferences=p['strictDifferences'],
            maxAbsoluteDifference=p['maxAbsoluteDifference'],role=p.get('role'))
(out/'SUMMARY.json').write_text(json.dumps(summary,indent=1))
for scene,row in sorted(summary.items()):
    print(scene.ljust(44),'  '.join(f"{b}: {v if isinstance(v,str) else ('PASS' if v['practicalFailures']==0 else 'FAIL '+str(v['practicalFailures']))} {'' if isinstance(v,str) else '%.2g'%v['maxAbsoluteDifference']}" for b,v in sorted(row.items())))
