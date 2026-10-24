#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Multi-backend comparison of oracle films for one scene (the two-oracle comparator).

    python3 native/oracle/compare.py DIR_A DIR_B [DIR_C ...] [--json OUT]
    python3 native/oracle/compare.py --scene-root ROOT SCENE_ID [--json OUT]
    python3 native/oracle/compare.py --self-test

Each directory is one backend's output for the same scene (meta.json and film-N.f32 from
codebrix_cycles_oracle, render_oracles.py or the Android app gate's bundle mode), for example
cpu-x64-linux, cpu-arm64-macos, metal-m2pro, wgsl-linux-intel, wgsl-android-<device>.
--scene-root ROOT SCENE_ID takes every backend directory under ROOT/oracle/SCENE_ID (or
ROOT/SCENE_ID), in name order with cpu-* first; a Mac bundle drops in as more directories.

For every pair the earlier directory is the reference b of the practical criterion
abs(a - b) <= 5e-5 + 2e-4 * abs(b), which must hold for every component; the strict diagnostic
1e-7 + 1e-7 * abs(b) is counted. Reported per pair: practical failures, strict differences,
maximum absolute and relative error, RMS, the worst pixel (largest ratio to the tolerance) and
NaN/Inf counts. Per backend: every repeat film is re-read and compared with film-0 (bitwise
identity and maximum difference), next to the meta.json repeat record. Films whose bytes differ
from their recorded SHA-256, shape or scene mismatches, NaN/Inf and practical failures fail.
The verdict is "perfect" when every pair passes the practical criterion and nothing failed.
"""
import argparse,array,hashlib,itertools,json,math,shutil,sys,tempfile
from pathlib import Path
PRACTICAL=(5e-5,2e-4);STRICT=(1e-7,1e-7)


class Invalid(Exception):pass


def read_film(directory,meta,repeat):
 path=Path(directory)/f'film-{repeat}.f32';data=path.read_bytes()
 hashes=meta.get('filmSha256',[])
 if repeat>=len(hashes) or hashlib.sha256(data).hexdigest()!=hashes[repeat]:raise Invalid(f'{path} does not match its recorded SHA-256')
 count=meta['width']*meta['height']*4
 if len(data)!=count*4:raise Invalid(f'{path} has {len(data)} bytes; expected {count*4}')
 values=array.array('f');values.frombytes(data)
 if sys.byteorder!='little':values.byteswap()
 return values


def load(directory,repeat=0):
 directory=Path(directory);meta=json.loads((directory/'meta.json').read_text())
 return meta,read_film(directory,meta,repeat)


def nonfinite(values):return sum(1 for v in values if not math.isfinite(v))


def compare(reference,candidate,width):
 failures=strict=0;max_abs=max_rel=0.0;square=0.0;worst=None;bitwise=reference.tobytes()==candidate.tobytes()
 for i,(b,a) in enumerate(zip(reference,candidate)):
  if not (math.isfinite(a) and math.isfinite(b)):continue
  difference=abs(a-b);square+=difference*difference
  max_abs=max(max_abs,difference)
  if b!=0:max_rel=max(max_rel,difference/abs(b))
  tolerance=PRACTICAL[0]+PRACTICAL[1]*abs(b)
  if difference>tolerance:failures+=1
  if difference>STRICT[0]+STRICT[1]*abs(b):strict+=1
  ratio=difference/tolerance
  if worst is None or ratio>worst['criterionRatio']:
   pixel,channel=divmod(i,4)
   worst=dict(x=pixel%width,y=pixel//width,channel='RGBA'[channel],reference=b,candidate=a,absoluteDifference=difference,criterionRatio=ratio)
 return dict(bitwiseEqual=bitwise,maxAbsoluteDifference=max_abs,maxRelativeError=max_rel,rms=math.sqrt(square/len(reference)),
  practicalFailures=failures,strictDifferences=strict,worstPixel=worst)


def repeats(directory,meta,first):
 """Re-reads every repeat film and compares it with film-0."""
 rows=[]
 for r in range(1,int(meta.get('repeats',1))):
  if not (Path(directory)/f'film-{r}.f32').exists():break
  film=read_film(directory,meta,r)
  rows.append(dict(repeat=r,bitwiseEqual=film.tobytes()==first.tobytes(),
   maxAbsoluteDifference=max((abs(a-b) for a,b in zip(film,first) if math.isfinite(a) and math.isfinite(b)),default=0.0),nonfinite=nonfinite(film)))
 return rows


def run(directories):
 films=[];problems=[]
 for directory in directories:
  meta,values=load(directory)
  films.append((Path(directory),meta,values))
  if nonfinite(values):problems.append(f'{directory}: {nonfinite(values)} NaN/Inf components')
 first=films[0][1]
 for directory,meta,_ in films[1:]:
  if meta['sceneSha256']!=first['sceneSha256']:raise Invalid(f'{directory} rendered a different scene file')
  if (meta['width'],meta['height'])!=(first['width'],first['height']):raise Invalid(f'{directory} has a different film shape')
 backends=[]
 for d,m,v in films:
  rows=repeats(d,m,v)
  for row in rows:
   if row['nonfinite']:problems.append(f'{d}: repeat {row["repeat"]} has {row["nonfinite"]} NaN/Inf components')
  backends.append(dict(directory=str(d),backend=m['backend'],samples=m['samples'],repeats=m['repeats'],
   repeatBitwiseIdentical=m.get('repeatBitwiseIdentical'),repeatMaxAbsDifference=m.get('repeatMaxAbsDifference'),
   measuredRepeats=rows,measuredRepeatBitwiseIdentical=all(r['bitwiseEqual'] for r in rows),
   measuredRepeatMaxAbsDifference=max([0.0]+[r['maxAbsoluteDifference'] for r in rows]),
   deviceName=m.get('deviceName'),renderSeconds=m.get('renderSeconds'),nonfiniteComponents=nonfinite(v)))
 report=dict(sceneId=first['sceneId'],sceneSha256=first['sceneSha256'],width=first['width'],height=first['height'],
  practicalCriterion='abs(a-b) <= 5e-5 + 2e-4*abs(b)',strictCriterion='abs(a-b) <= 1e-7 + 1e-7*abs(b)',backends=backends,pairs=[])
 for (rd,rm,rv),(cd,cm,cv) in itertools.combinations(films,2):
  if rm['samples']!=cm['samples']:problems.append(f'{cm["backend"]} used {cm["samples"]} samples; {rm["backend"]} used {rm["samples"]}')
  result=compare(rv,cv,first['width']);result.update(reference=rm['backend'],candidate=cm['backend'])
  result['practicalPassed']=result['practicalFailures']==0
  if not result['practicalPassed']:problems.append(f'{cm["backend"]} vs {rm["backend"]}: {result["practicalFailures"]} practical failures')
  report['pairs'].append(result)
 report['problems']=problems;report['passed']=not problems
 report['verdict']='perfect' if report['passed'] and len(films)>=2 else 'not perfect'
 return report


def discover(root,scene):
 root=Path(root)
 for base in (root/'oracle'/scene,root/scene):
  if base.is_dir():
   dirs=[d for d in base.iterdir() if (d/'meta.json').is_file()]
   return sorted(dirs,key=lambda d:(not d.name.startswith('cpu-'),d.name))
 raise Invalid(f'No backend directories for {scene} under {root}')


def table(report):
 lines=[f"{report['sceneId']} ({report['width']}x{report['height']})"]
 for b in report['backends']:
  lines.append(f"  {b['backend']:<26} samples {b['samples']:<5} repeats {b['repeats']} bitwise={b['measuredRepeatBitwiseIdentical']} "
               f"repeatMaxAbs={b['measuredRepeatMaxAbsDifference']:.3g} nonfinite={b['nonfiniteComponents']}")
 lines.append(f"  {'candidate vs reference':<52} {'maxAbs':>10} {'maxRel':>10} {'rms':>10} {'practicalFail':>13} {'strictDiff':>10}")
 for p in report['pairs']:
  lines.append(f"  {p['candidate']+' vs '+p['reference']:<52} {p['maxAbsoluteDifference']:>10.3g} {p['maxRelativeError']:>10.3g} "
               f"{p['rms']:>10.3g} {p['practicalFailures']:>13} {p['strictDifferences']:>10}")
  w=p['worstPixel']
  if w:lines.append(f"      worst x={w['x']} y={w['y']} {w['channel']}: ref {w['reference']:.9g} got {w['candidate']:.9g} ({w['criterionRatio']:.3g}x tolerance)")
 lines.append(f"  verdict: {report['verdict']}"+('' if report['passed'] else '; FAILED: '+'; '.join(report['problems'])))
 return '\n'.join(lines)


def self_test():
 """The comparator must reject NaN/Inf, shape mismatch, corruption and real differences."""
 root=Path(tempfile.mkdtemp(prefix='compare-self-test-'))
 def film(name,values,width=2,height=1,scene='s',repeat_values=None):
  d=root/name;d.mkdir();hashes=[]
  for r,vals in enumerate([values]+([repeat_values] if repeat_values else [])):
   data=array.array('f',vals)
   if sys.byteorder!='little':data.byteswap()
   raw=data.tobytes();(d/f'film-{r}.f32').write_bytes(raw);hashes.append(hashlib.sha256(raw).hexdigest())
  (d/'meta.json').write_text(json.dumps(dict(sceneId='self-test',sceneSha256=scene,backend=name,width=width,height=height,samples=1,repeats=len(hashes),
   repeatBitwiseIdentical=True,repeatMaxAbsDifference=0,filmSha256=hashes)))
  return d
 base=[0.5,0.25,1.0,1.0,0.0,2.0,3.0,1.0]
 try:
  checks={}
  r=run([film('a',base),film('b',base)]);checks['identical']=r['passed'] and r['verdict']=='perfect'
  near=list(base);near[1]+=1e-6
  r=run([film('c',base),film('d',near),film('d2',base)]);checks['threeBackendsWithinPracticalPerfect']=r['verdict']=='perfect' and len(r['pairs'])==3
  far=list(base);far[5]+=1e-2
  r=run([film('e',base),film('f',far)]);checks['beyondPracticalFails']=not r['passed'] and r['verdict']=='not perfect'
  nan=list(base);nan[2]=float('nan');inf=list(base);inf[3]=float('inf')
  checks['nanFails']=not run([film('g',base),film('h',nan)])['passed']
  checks['infFails']=not run([film('i',base),film('j',inf)])['passed']
  def invalid(dirs):
   try:run(dirs);return False
   except Invalid:return True
  checks['shapeMismatchFails']=invalid([film('k',base),film('l',base+[0,0,0,0],width=3)])
  checks['sceneMismatchFails']=invalid([film('m',base),film('n',base,scene='other')])
  corrupt=film('o',base);data=bytearray((corrupt/'film-0.f32').read_bytes());data[0]^=1;(corrupt/'film-0.f32').write_bytes(bytes(data))
  checks['corruptedFilmFails']=invalid([film('p',base),corrupt])
  rep=film('q',base,repeat_values=near);r=run([film('q0',base),rep])
  checks['repeatDifferenceMeasured']=r['backends'][1]['measuredRepeatBitwiseIdentical'] is False and r['backends'][1]['measuredRepeatMaxAbsDifference']>0
  badrep=film('s',base,repeat_values=base);raw=bytearray((badrep/'film-1.f32').read_bytes());raw[4]^=1;(badrep/'film-1.f32').write_bytes(bytes(raw))
  checks['corruptedRepeatFails']=invalid([film('s0',base),badrep])
 finally:shutil.rmtree(root)
 print(json.dumps(checks,indent=2))
 return all(checks.values())


def main():
 parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 parser.add_argument('directories',nargs='*',type=Path);parser.add_argument('--json',type=Path);parser.add_argument('--self-test',action='store_true')
 parser.add_argument('--scene-root',nargs=2,metavar=('ROOT','SCENE_ID'))
 args=parser.parse_args()
 if args.self_test:return 0 if self_test() else 1
 try:
  directories=discover(*args.scene_root) if args.scene_root else args.directories
  if len(directories)<2:parser.error('give at least two backend directories')
  report=run(directories)
 except (Invalid,FileNotFoundError,KeyError,json.JSONDecodeError) as error:
  print('compare.py: invalid input:',error,file=sys.stderr);return 2
 print(table(report))
 if args.json:args.json.write_text(json.dumps(report,indent=2)+'\n')
 return 0 if report['passed'] else 1


if __name__=='__main__':sys.exit(main())
