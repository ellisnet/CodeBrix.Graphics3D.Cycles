#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Specialization on/off film identity for the render gates.

A render gate binary that already ran with scene specialization (folder ON) runs
again with specialization off and every film/image it writes must be bit-identical.

"Off" is the superset mode by default (CODEBRIX_CYCLES_SHADER_SPECIALIZATION=superset:G):
every region except the bump/derivative and Gabor regions the scene does not reach,
compiled in size-bounded groups (pipelines.h) because the whole superset exceeds the
Intel Mesa 25.0.7 backend's 16-bit instruction/register range. Group 0 reports the
group count; every group runs and must be bit-identical. Regions that do not fit
next to the scene are listed as "unfit" in the record (other gate scenes cover them).
"unspecialized" (=0) compiles every handler in one module and is a diagnostic only
(native/DEVICE.md, "Linux reference fixes on the updated Mesa")."""
import hashlib
import json
import os
import subprocess
from pathlib import Path

def films(folder):
    return {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(Path(folder).iterdir())
            if p.name.endswith(('.ppm','.film.bin'))}

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
    record={'mode':mode,'binary':str(binary),'on':on_films,'runs':[]}
    for value,folder,log in runs:
        off_films=films(folder);entry={'value':value,'films':off_films,'identical':bool(on_films) and on_films==off_films,
              'maxWgslBytes':max((r['wgslBytes'] for r in log),default=0)}
        if log and 'supersetGroups' in log[0]:
            entry.update({k:log[0][k] for k in ('supersetGroups','supersetSceneBytes','supersetGroupBytes','supersetGroupAdded','supersetUnfit')})
            entry['maxSupersetGroupBytes']=max(r['supersetGroupBytes'] for r in log)
            entry['added']=sorted({f for r in log for f in r['supersetGroupAdded'].split(',') if f})
            entry['unfitAll']=sorted({f for r in log for f in r['supersetUnfit'].split(',') if f})
        record['runs'].append(entry)
    record['identical']=all(r['identical'] for r in record['runs'])
    if mode!='unspecialized':
        record['covered']=sorted({f for r in record['runs'] for f in r['added']})
        record['unfit']=sorted(set(record['runs'][0]['unfitAll'])-set(record['covered']))
    (off/'specialization-identical.json').write_text(json.dumps(record,indent=2)+'\n')
    assert record['identical'],f'{Path(binary).name}: films differ with specialization on/{mode}'
    return record
