#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Run Linux ARM64 device jobs in parallel lanes under one overall time cap.

    MESA_SHADER_CACHE_DIR=<dir> python3 native/tools/linux_arm64_lanes.py \\
        --output native/work/linux-arm64-lanes-<n> --cap-seconds 36000 \\
        [--lane A=coverage:smooth-shading,coverage:light-tree --lane B=gate:glass_render ...]

For the native Linux ARM64 build on the device (docs/TESTING-LINUX-ARM64.md).
Mesa compiles each pipeline on one CPU core and the host creates pipelines one
after another, so cold compiles of the Cycles kernels on a slow driver (the
Raspberry Pi 5's V3DV, finding L2) dominate the wall; lanes run jobs
concurrently, sharing the driver's on-disk cache. Jobs within a lane run in
order. Every job runs in its own directory in the job form of that document
(pipeline log on, OCIO and the pinned tier/budget/specialization variables
removed; wall, exit, stdout and stderr recorded) and gets the time left before
the cap; a job not started before the cap is recorded as not reached. Walls of
concurrent jobs are contended and are not reference walls.

Job kinds:
  gate:<name>[:<argument>]   codebrix_cycles_<name>_gate <job>/run [<argument>]; its own JSON verdict
                             (GPU against its in-process glibc CPU reference) is the result
  gate:<name>@VAR=value,...  the same with environment variables (the doc's volume rows)
  coverage:<kind>            a driver-coverage scene of linux_arm64_host_gate.py, CPU and GPU film
                             from the oracle runner, compared by native/oracle/compare.py
  host-gate[:<arguments>]    native/tools/linux_arm64_host_gate.py --output <job>/gate ...
"""
import argparse,datetime,json,os,subprocess,sys,threading,time
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'native/tools'))
from build import PACKAGE
from linux_arm64_host_gate import DRIVER_COVERAGE,DIAGNOSTIC_VARIABLES,PINNED_VARIABLES,NATIVE,report_passed
BASE=REPO/'native/work/linux/linux-arm64';BIN=BASE/'host/build/bin'
RUNTIME=PACKAGE/'runtimes/linux-arm64/native/libcodebrix_wgpu.so'
REFERENCES=REPO/'native/evidence/linux-x64-reference-20261029/host-gate'
# The 10-hour plan of 2026-10-28: driver coverage first; the host gate's GPU steps and one
# device gate per kernel family; the two largest material modules on their own lane.
DEFAULT_LANES={
 'A':['coverage:smooth-shading','coverage:motion-normal-map','coverage:glass-area-light','coverage:light-tree'],
 'B':['host-gate','gate:triangle_lighting','gate:object_motion_render','gate:glass_render','gate:volume_render@CODEBRIX_VOLUME_G=0','gate:mnee_render'],
 'C':['gate:principled_render','gate:nodes_render']}


def save(path,value):Path(path).write_text(json.dumps(value,indent=2,default=str)+'\n')


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 p.add_argument('--output',type=Path,required=True);p.add_argument('--cap-seconds',type=int,default=36000)
 p.add_argument('--lane',action='append',help='NAME=job,job,... (replaces the default lanes)');p.add_argument('--threads',type=int,default=2,help='CPU threads of the oracle runner')
 a=p.parse_args()
 if not NATIVE:raise SystemExit('linux_arm64_lanes.py runs natively on an ARM64 Linux machine')
 lanes=dict(item.split('=',1) for item in a.lane) if a.lane else None
 lanes={name:jobs.split(',') for name,jobs in lanes.items()} if lanes else DEFAULT_LANES
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 status=json.loads((BASE/'host/status.json').read_text());assert status['complete'] and status['passed'],'Build first: native/tools/build_cycles_linux_arm64.py --device-set'
 env=dict(os.environ)
 for name in PINNED_VARIABLES:env.pop(name,None)
 env['LD_LIBRARY_PATH']=':'.join(map(str,[BASE/'host/build/lib',BASE/'dependencies/install/lib',RUNTIME.parent]))
 diagnostic=sorted(n+'='+env[n] for n in DIAGNOSTIC_VARIABLES if env.get(n))
 deadline=time.time()+a.cap_seconds;lock=threading.Lock()
 record=dict(started=datetime.datetime.now().astimezone().isoformat(),capSeconds=a.cap_seconds,lanes=lanes,diagnosticRun=bool(diagnostic),diagnostics=diagnostic,
  mesaShaderCacheDir=env.get('MESA_SHADER_CACHE_DIR'),buildArtifacts=status['artifacts'],jobs={})
 save(out/'lanes.json',record)

 def run(name,command,extra,folder):
  """One process in the job form; returns (exit or 'stopped', wall seconds)."""
  remaining=deadline-time.time()
  if remaining<=0:return 'notReached',0.0
  started=time.time();since=datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
  with (folder/(name+'.stdout.txt')).open('w') as stdout,(folder/(name+'.stderr.txt')).open('w') as stderr:
   try:code=subprocess.run(list(map(str,command)),cwd=REPO,stdout=stdout,stderr=stderr,timeout=remaining,
    env={**env,'CODEBRIX_CYCLES_PIPELINE_LOG':str(folder/(name+'.pipelines.jsonl')),**extra}).returncode
   except subprocess.TimeoutExpired:code='stopped'
  wall=round(time.time()-started,2)
  (folder/(name+'.exit.txt')).write_text(str(code)+'\n');(folder/(name+'.wall.txt')).write_text(str(wall)+'\n')
  (folder/(name+'.command.json')).write_text(json.dumps(dict(command=list(map(str,command)),environment=extra),indent=2)+'\n')
  if code!=0:
   # The kernel log of the job's window (V3D resets, GPU hangs); the adm group can read it.
   kernel=subprocess.run(['journalctl','-k','--no-pager','--since',since],capture_output=True,text=True)
   (folder/(name+'.kernel.txt')).write_text(kernel.stdout+kernel.stderr)
  return code,wall

 def pipelines(folder):
  rows=[json.loads(line) for f in folder.glob('*.pipelines.jsonl') for line in f.read_text().splitlines() if line.strip()]
  times=[r['pipelineMs'] for r in rows if 'pipelineMs' in r]
  return dict(count=len(rows),compileSeconds=round(sum(times)/1000,1),largest=max(rows,key=lambda r:r.get('pipelineMs',0))['entry'] if rows else None,
   largestSeconds=round(max(times)/1000,1) if times else None,failures=[r for r in rows if r.get('failure')])

 def job(lane,spec):
  kind,_,rest=spec.partition(':');label=spec.replace(':','-').replace('@','-').replace('=','').replace(',','-')
  folder=out/label;folder.mkdir()
  row=dict(lane=lane,spec=spec,started=datetime.datetime.now().astimezone().isoformat())
  if kind=='gate':
   rest,_,variables=rest.partition('@');name,_,argument=rest.partition(':')
   extra=dict(v.split('=',1) for v in variables.split(',')) if variables else {}
   arguments=[] if name=='atomic_film' else [folder/'run']
   code,wall=run('gate',[BIN/f'codebrix_cycles_{name}_gate',*arguments,*([argument] if argument else [])],extra,folder)
   reports=[json.loads(line) for line in (folder/'gate.stdout.txt').read_text().splitlines() if line.strip().startswith('{')]
   library=next((f.read_text().strip() for f in [folder/'run.library.txt',folder/'run/library.txt'] if f.exists()),None)
   row.update(exit=code,wallSeconds=wall,loadedLibrary=library,
    passed=code==0 and bool(reports) and all(report_passed(r) for r in reports) and all(r.get('cyclesAllocationBytesAfterCleanup',0)==0 for r in reports)
     # atomic_film is a CPU contract test and loads no WebGPU runtime.
     and (name=='atomic_film' or library is not None and str(RUNTIME) in library))
  elif kind=='coverage':
   scene=REPO/DRIVER_COVERAGE[rest];row['scene']=str(scene.relative_to(REPO));films={}
   for backend,film in [('cpu','cpu-arm64-linux'),('webgpu','wgsl-linux-arm64-vulkan')]:
    films[backend]=run(backend,[BIN/'codebrix_cycles_oracle','--scene',scene,'--backend',backend,'--label',film,'--output',folder/film,'--threads',a.threads],{},folder)
   row.update(exit={k:v[0] for k,v in films.items()},wallSeconds={k:v[1] for k,v in films.items()},passed=False)
   if all(v[0]==0 for v in films.values()):
    subprocess.run([sys.executable,'-I',REPO/'native/oracle/compare.py',folder/'cpu-arm64-linux',folder/'wgsl-linux-arm64-vulkan','--json',folder/'compare.json'],
     cwd=REPO,stdout=(folder/'compare.stdout.txt').open('w'),stderr=subprocess.STDOUT,timeout=600)
    if (folder/'compare.json').exists():
     report=json.loads((folder/'compare.json').read_text());pair=report['pairs'][0]
     row.update(passed=bool(report['passed']),maximumAbsoluteError=pair['maxAbsoluteDifference'],practicalFailures=pair['practicalFailures'],strictDifferences=pair['strictDifferences'])
  elif kind=='host-gate':
   remaining=max(int(deadline-time.time()),1)
   code,wall=run('host-gate',[sys.executable,REPO/'native/tools/linux_arm64_host_gate.py','--output',folder/'gate','--references',REFERENCES,
    '--step-timeout',remaining,*(rest.split() if rest else [])],{},folder)
   gate=json.loads((folder/'gate/status.json').read_text()) if (folder/'gate/status.json').exists() else {}
   row.update(exit=code,wallSeconds=wall,passed=code==0 and bool(gate.get('passed')),
    gate={k:gate.get(k) for k in ['phase','passed','error','filmsCompared','failedFilms','gpuPracticalFailures','gpuMaximumAbsoluteError','driverCoverageFailures','actualArm64GpuProven']})
  else:raise SystemExit('Unknown job kind: '+spec)
  row['pipelines']=pipelines(folder) if kind!='host-gate' else None
  codes=row['exit'].values() if isinstance(row.get('exit'),dict) else [row.get('exit')]
  if 'notReached' in codes:row['notReached']=True
  with lock:record['jobs'][label]=row;save(out/'lanes.json',record)
  print(json.dumps({k:row[k] for k in ['lane','spec','passed','exit','wallSeconds'] if k in row}),flush=True)

 def lane(name,jobs):
  for spec in jobs:job(name,spec)
 threads=[threading.Thread(target=lane,args=item) for item in lanes.items()]
 for t in threads:t.start()
 for t in threads:t.join()
 jobs=record['jobs'].values()
 record.update(finished=datetime.datetime.now().astimezone().isoformat(),
  passed=[r['spec'] for r in jobs if r['passed']],failed=[r['spec'] for r in jobs if not r['passed'] and not r.get('notReached') and 'stopped' not in str(r.get('exit'))],
  stopped=[r['spec'] for r in jobs if 'stopped' in str(r.get('exit'))],notReached=[r['spec'] for r in jobs if r.get('notReached')])
 save(out/'lanes.json',record)
 print(json.dumps({k:record[k] for k in ['passed','failed','stopped','notReached']}),flush=True)
 return 0 if not record['failed'] and not record['stopped'] and not record['notReached'] and not diagnostic else 1


if __name__=='__main__':raise SystemExit(main())
