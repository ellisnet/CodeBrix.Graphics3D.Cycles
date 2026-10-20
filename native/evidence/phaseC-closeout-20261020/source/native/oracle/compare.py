#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Pairwise comparison of oracle films for one scene.

    python3 native/oracle/compare.py REFERENCE_DIR OTHER_DIR [MORE_DIRS...] [--json OUT]

Each directory holds meta.json and film-N.f32 from codebrix_cycles_oracle. For
every pair, the earlier directory is the reference b in the practical criterion
abs(a - b) <= 5e-5 + 2e-4 * abs(b); the strict diagnostic uses 1e-7 + 1e-7 * abs(b).
Practical failures, NaN/Inf, shape or scene mismatches and films whose bytes
differ from their recorded SHA-256 all fail. Strict differences are reported.
"""
import argparse,array,hashlib,itertools,json,math,shutil,sys,tempfile
from pathlib import Path
PRACTICAL=(5e-5,2e-4);STRICT=(1e-7,1e-7)


class Invalid(Exception):pass


def load(directory,repeat=0):
 directory=Path(directory);meta=json.loads((directory/'meta.json').read_text())
 path=directory/f'film-{repeat}.f32';data=path.read_bytes()
 if hashlib.sha256(data).hexdigest()!=meta['filmSha256'][repeat]:raise Invalid(f'{path} does not match its recorded SHA-256')
 count=meta['width']*meta['height']*4
 if len(data)!=count*4:raise Invalid(f'{path} has {len(data)} bytes; expected {count*4}')
 values=array.array('f');values.frombytes(data)
 if sys.byteorder!='little':values.byteswap()
 return meta,values


def nonfinite(values):return sum(1 for v in values if not math.isfinite(v))


def compare(reference,candidate,width):
 failures=strict=0;max_abs=max_rel=0.0;square=0.0;worst=None
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
 return dict(maxAbsoluteDifference=max_abs,maxRelativeError=max_rel,rms=math.sqrt(square/len(reference)),
  practicalFailures=failures,strictDifferences=strict,worstPixel=worst)


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
 report=dict(sceneId=first['sceneId'],sceneSha256=first['sceneSha256'],width=first['width'],height=first['height'],
  practicalCriterion='abs(a-b) <= 5e-5 + 2e-4*abs(b)',strictCriterion='abs(a-b) <= 1e-7 + 1e-7*abs(b)',
  backends=[dict(directory=str(d),backend=m['backend'],samples=m['samples'],repeats=m['repeats'],
   repeatBitwiseIdentical=m['repeatBitwiseIdentical'],repeatMaxAbsDifference=m['repeatMaxAbsDifference'],
   nonfiniteComponents=nonfinite(v)) for d,m,v in films],pairs=[])
 for (rd,rm,rv),(cd,cm,cv) in itertools.combinations(films,2):
  if rm['samples']!=cm['samples']:problems.append(f'{cm["backend"]} used {cm["samples"]} samples; {rm["backend"]} used {rm["samples"]}')
  result=compare(rv,cv,first['width']);result.update(reference=rm['backend'],candidate=cm['backend'])
  result['practicalPassed']=result['practicalFailures']==0
  if not result['practicalPassed']:problems.append(f'{cm["backend"]} vs {rm["backend"]}: {result["practicalFailures"]} practical failures')
  report['pairs'].append(result)
 report['problems']=problems;report['passed']=not problems
 return report


def table(report):
 lines=[f"{report['sceneId']} ({report['width']}x{report['height']})"]
 for b in report['backends']:
  lines.append(f"  {b['backend']:<18} samples {b['samples']:<5} repeats {b['repeats']} bitwise={b['repeatBitwiseIdentical']} "
               f"repeatMaxAbs={b['repeatMaxAbsDifference']:.3g} nonfinite={b['nonfiniteComponents']}")
 lines.append(f"  {'candidate vs reference':<36} {'maxAbs':>10} {'maxRel':>10} {'rms':>10} {'practicalFail':>13} {'strictDiff':>10}")
 for p in report['pairs']:
  lines.append(f"  {p['candidate']+' vs '+p['reference']:<36} {p['maxAbsoluteDifference']:>10.3g} {p['maxRelativeError']:>10.3g} "
               f"{p['rms']:>10.3g} {p['practicalFailures']:>13} {p['strictDifferences']:>10}")
  w=p['worstPixel']
  if w:lines.append(f"      worst x={w['x']} y={w['y']} {w['channel']}: ref {w['reference']:.9g} got {w['candidate']:.9g} ({w['criterionRatio']:.3g}x tolerance)")
 lines.append('  PASSED' if report['passed'] else '  FAILED: '+'; '.join(report['problems']))
 return '\n'.join(lines)


def self_test():
 """The comparator must reject NaN/Inf, shape mismatch, corruption and real differences."""
 root=Path(tempfile.mkdtemp(prefix='compare-self-test-'))
 def film(name,values,width=2,height=1,scene='s'):
  d=root/name;d.mkdir();data=array.array('f',values)
  if sys.byteorder!='little':data.byteswap()
  raw=data.tobytes();(d/'film-0.f32').write_bytes(raw)
  (d/'meta.json').write_text(json.dumps(dict(sceneId='self-test',sceneSha256=scene,backend=name,width=width,height=height,samples=1,repeats=1,
   repeatBitwiseIdentical=True,repeatMaxAbsDifference=0,filmSha256=[hashlib.sha256(raw).hexdigest()])))
  return d
 base=[0.5,0.25,1.0,1.0,0.0,2.0,3.0,1.0]
 try:
  checks={}
  checks['identical']=run([film('a',base),film('b',base)])['passed']
  near=list(base);near[1]+=1e-6
  checks['withinPractical']=run([film('c',base),film('d',near)])['passed']
  far=list(base);far[5]+=1e-2
  checks['beyondPracticalFails']=not run([film('e',base),film('f',far)])['passed']
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
 finally:shutil.rmtree(root)
 print(json.dumps(checks,indent=2))
 return all(checks.values())


def main():
 parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 parser.add_argument('directories',nargs='*',type=Path);parser.add_argument('--json',type=Path);parser.add_argument('--self-test',action='store_true')
 args=parser.parse_args()
 if args.self_test:return 0 if self_test() else 1
 if len(args.directories)<2:parser.error('give at least two backend directories')
 try:report=run(args.directories)
 except (Invalid,FileNotFoundError,KeyError,json.JSONDecodeError) as error:
  print('compare.py: invalid input:',error,file=sys.stderr);return 2
 print(table(report))
 if args.json:args.json.write_text(json.dumps(report,indent=2)+'\n')
 return 0 if report['passed'] else 1


if __name__=='__main__':sys.exit(main())
