#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Specialization on/off result identity for the render gates.

A render gate binary that already ran with scene specialization (folder ON) runs
again with specialization off; specialization must never change a result.

"Off" is the superset mode by default (CODEBRIX_CYCLES_SHADER_SPECIALIZATION=superset:G):
every region except the bump/derivative and Gabor regions the scene does not reach,
compiled in size-bounded groups (pipelines.h) because the whole superset exceeds the
Intel Mesa 25.0.7 backend's 16-bit instruction/register range. Group 0 reports the
group count; every group runs. Regions that do not fit next to the scene are listed
as "unfit" in the record (other gate scenes cover them).
"unspecialized" (=0) compiles every handler in one module and is a diagnostic only
(native/DEVICE.md, "Linux reference fixes on the updated Mesa").

What a run must show (every run, ON and each off run): the gate's own report passed;
every WebGPU film (PREFIX.KIND.film.bin beside a CPU film PREFIX.cpu.film.bin: the
render, its repeat, its batched render) within the standard practical criterion
abs(gpu - cpu) <= 5e-5 + 2e-4*abs(cpu) against that CPU film of the same run, with no
NaN or Inf (strict 1e-7 + 1e-7*abs(cpu) reported); every other film or image file (the
CPU films and images included, whose kernels do not change with the device's
specialization) bit-identical to ON's. WebGPU films are not repeat-identical in
the last bits (per-word atomic film adds in undefined order, an accepted property of the
device), so the bit identity of the WebGPU films and images with ON's is reported as
data per run (differing files, differing components, largest difference), not required."""
import argparse
import array
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'oracle'))
import compare as judge  # native/oracle/compare.py: the judge's own film arithmetic (practical and strict criteria)

def films(folder):
    """Every film and image of a run, in its folder and below it (some gates keep them per case), by relative path."""
    return {str(p.relative_to(folder)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(Path(folder).rglob('*'))
            if p.is_file() and p.name.endswith(('.ppm','.film.bin')) and 'shader-cache' not in p.relative_to(folder).parts}

def _f32(path):
    """A film file as float32 values (native/oracle/compare.py reads films the same way)."""
    values=array.array('f');values.frombytes(Path(path).read_bytes())
    if sys.byteorder!='little':values.byteswap()
    return values

def cpu_partner(name):
    """The CPU film of a WebGPU film name (PREFIX.KIND.film.bin with KIND gpu, repeat, batched, ...: PREFIX.cpu.film.bin), else None."""
    parts=name.split('.')
    if len(parts)<4 or parts[-2:]!=['film','bin'] or parts[-3]=='cpu':return None
    return '.'.join(parts[:-3]+['cpu','film','bin'])

def webgpu_names(names):
    """The WebGPU films (a film with a CPU partner in NAMES) and WebGPU images (*.gpu.ppm) of a run: data only for bit identity."""
    names=set(names);return sorted(n for n in names if (cpu_partner(n) and cpu_partner(n) in names) or n.endswith('.gpu.ppm'))

def against_cpu(folder):
    """Every WebGPU film of FOLDER (PREFIX.KIND.film.bin beside PREFIX.cpu.film.bin: the render, its repeats, its batched
    render) against that CPU film with compare.py's compare(): the standard practical criterion (must pass), strict
    (reported), NaN/Inf (fail). Returns {WebGPU film name: row}."""
    rows={}
    for gpu in sorted(Path(folder).glob('*.film.bin')):
        partner=cpu_partner(gpu.name)
        if not partner or not (Path(folder)/partner).exists():continue
        cpu=Path(folder)/partner
        a,e=_f32(gpu),_f32(cpu)
        if len(a)!=len(e) or not len(a):rows[gpu.name]=dict(cpu=cpu.name,passed=False,problem=f'{len(a)} components, CPU film {len(e)}');continue
        r=judge.compare(e,a,1,1);nonfinite=judge.nonfinite(a)
        rows[gpu.name]=dict(cpu=cpu.name,components=len(a),practicalFailures=r['practicalFailures'],strictDifferences=r['strictDifferences'],nonfinite=nonfinite,
                            maxAbs=r['maxAbsoluteDifference'],passed=r['practicalFailures']==0 and nonfinite==0)
    return rows

def against_cpu_tree(folder):
    """against_cpu in every folder of a run that holds films, keyed by the film's path relative to FOLDER."""
    rows={}
    for d in sorted({(Path(folder)/n).parent for n in films(folder)}):
        for name,row in against_cpu(d).items():rows[str((d/name).relative_to(folder))]=row
    return rows

def bit_identity(on_folder,off_folder,names):
    """Data: per WebGPU file of ON, whether OFF's is bit-identical, and for films the differing components and largest difference."""
    rows={}
    for name in names:
        x,y=Path(on_folder)/name,Path(off_folder)/name
        if not y.exists():rows[name]=dict(present=False);continue
        same=x.read_bytes()==y.read_bytes();row=dict(identical=same)
        if not same and name.endswith('.film.bin'):
            a,b=_f32(x),_f32(y)
            if len(a)==len(b):
                bits_a,bits_b=array.array('I',a.tobytes()),array.array('I',b.tobytes())
                row.update(differingComponents=sum(1 for u,v in zip(bits_a,bits_b) if u!=v),maxAbs=max(abs(u-v) for u,v in zip(a,b)))
        rows[name]=row
    return rows

def _run(binary,folder,value,timeout,extra_args):
    folder.mkdir(parents=True)
    env=dict(os.environ,CODEBRIX_CYCLES_SHADER_SPECIALIZATION=value,
             CODEBRIX_CYCLES_PIPELINE_LOG=str(folder/'pipelines.jsonl'),MESA_SHADER_CACHE_DIR=str(folder/'shader-cache'))
    with (folder/'stdout.txt').open('w') as stdout,(folder/'stderr.txt').open('w') as stderr:
        subprocess.run([str(binary),str(folder/'run'),*extra_args],stdout=stdout,stderr=stderr,check=True,timeout=timeout,env=env)
    for path in sorted((folder/'shader-cache').rglob('*'),reverse=True):
        path.unlink() if path.is_file() else path.rmdir()
    if (folder/'shader-cache').exists():(folder/'shader-cache').rmdir()
    report=json.loads((folder/'stdout.txt').read_text())
    assert report['passed'] and report['cyclesAllocationBytesAfterCleanup']==0
    log=[json.loads(line) for line in (folder/'pipelines.jsonl').read_text().splitlines()] if (folder/'pipelines.jsonl').exists() else []
    return report,log

def check(binary,on_folder,off_folder,mode='superset',timeout=1800,extra_args=()):
    """Runs BINARY with specialization off into OFF_FOLDER (one sub-folder per
    superset group) and compares its films with ON_FOLDER. Returns the record;
    raises AssertionError on any difference."""
    off=Path(off_folder);off.mkdir(parents=True);on_films=films(on_folder);runs=[]
    if mode=='unspecialized':
        _,log=_run(binary,off/'unspecialized','0',timeout,extra_args);runs.append(('unspecialized',off/'unspecialized',log))
    else:
        group=0;count=1
        while group<count:
            folder=off/f'group-{group}';_,log=_run(binary,folder,f'superset:{group}',timeout,extra_args)
            assert log and all(r.get('specializationMode')=='superset' and r.get('supersetGroup')==group for r in log),folder
            # Scenes in one process can have different group counts; run up to the largest.
            count=max(r['supersetGroups'] for r in log);runs.append((f'superset:{group}',folder,log));group+=1
    # Exact: every file that is not a WebGPU film with a CPU partner or a WebGPU image (the CPU films and images included).
    gpu_names=webgpu_names(on_films);exact=sorted(n for n in on_films if n not in gpu_names)
    on_cpu=against_cpu_tree(on_folder)
    record={'mode':mode,'binary':str(binary),'on':on_films,'onAgainstCpu':on_cpu,'runs':[]}
    for value,folder,log in runs:
        off_films=films(folder);cpu=against_cpu_tree(folder)
        # Must hold: the same film and image files; CPU files and files without a CPU partner bit-identical; every WebGPU
        # film within practical of its CPU film (above). Data: the WebGPU files' bit identity with ON's.
        entry={'value':value,'films':off_films,'sameFiles':sorted(off_films)==sorted(on_films),
               'exactIdentical':all(off_films.get(n)==on_films[n] for n in exact),'exactFiles':exact,
               'againstCpu':cpu,'practicalPassed':all(r['passed'] for r in cpu.values()),
               'bitIdenticalToOn':bool(on_films) and on_films==off_films,'gpuBitIdentity':bit_identity(on_folder,folder,gpu_names),
               'maxWgslBytes':max((r['wgslBytes'] for r in log),default=0)}
        entry['identical']=entry['sameFiles'] and entry['exactIdentical'] and entry['practicalPassed']
        if log and 'supersetGroups' in log[0]:
            entry.update({k:log[0][k] for k in ('supersetGroups','supersetSceneBytes','supersetGroupBytes','supersetGroupAdded','supersetUnfit')})
            entry['maxSupersetGroupBytes']=max(r['supersetGroupBytes'] for r in log)
            entry['added']=sorted({f for r in log for f in r['supersetGroupAdded'].split(',') if f})
            entry['unfitAll']=sorted({f for r in log for f in r['supersetUnfit'].split(',') if f})
        record['runs'].append(entry)
    record['onPracticalPassed']=all(r['passed'] for r in on_cpu.values())
    record['identical']=record['onPracticalPassed'] and all(r['identical'] for r in record['runs'])
    record['bitIdenticalRuns']=sum(1 for r in record['runs'] if r['bitIdenticalToOn'])
    if mode!='unspecialized':
        record['covered']=sorted({f for r in record['runs'] for f in r['added']})
        record['unfit']=sorted(set(record['runs'][0]['unfitAll'])-set(record['covered']))
    (off/'specialization-identical.json').write_text(json.dumps(record,indent=2)+'\n')
    assert record['identical'],f'{Path(binary).name}: results differ with specialization on/{mode} (CPU files, files without a CPU partner, or a WebGPU film outside practical of its CPU film)'
    return record

def main():
    parser=argparse.ArgumentParser(description='Run a render gate binary with specialization off and compare its films with a finished run of the same binary. Exit 0: identical; 1: they differ.')
    parser.add_argument('binary',type=Path);parser.add_argument('on_folder',type=Path,help='Folder of the run with specialization on (its films are read)')
    parser.add_argument('off_folder',type=Path,help='New folder for the off runs');parser.add_argument('--mode',choices=['superset','unspecialized'],default='superset')
    parser.add_argument('--timeout',type=int,default=1800);parser.add_argument('extra_args',nargs='*')
    args=parser.parse_args()
    if not films(args.on_folder):sys.exit(f'{args.on_folder}: no films; a comparison with nothing proves nothing')
    try:record=check(args.binary,args.on_folder,args.off_folder,args.mode,args.timeout,args.extra_args)
    except AssertionError as error:print(error);sys.exit(1)
    print(json.dumps({k:record.get(k) for k in ('mode','identical','onPracticalPassed','bitIdenticalRuns','covered','unfit')}))

if __name__=='__main__':
    main()
