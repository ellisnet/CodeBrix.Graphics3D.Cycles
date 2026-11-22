#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Multi-backend comparison of oracle films for one scene (the two-gold-standards comparator).

    python3 native/oracle/compare.py DIR_A DIR_B [DIR_C ...] [--json OUT]
    python3 native/oracle/compare.py --scene-root ROOT SCENE_ID [--json OUT]
    python3 native/oracle/compare.py --self-test

Each directory is one backend's output for the same scene (meta.json and film-N.f32 from
codebrix_cycles_oracle, render_oracles.py or the Android app gate's bundle mode), for example
cpu-x64-linux, cpu-arm64-macos, metal-m2pro, wgsl-linux-intel, wgsl-android-<device>.
--scene-root ROOT SCENE_ID takes every backend directory under ROOT/oracle/SCENE_ID (or
ROOT/SCENE_ID), in name order with cpu-* first, then metal-*; a Mac bundle drops in as more
directories.

Two gold standards: the unmodified Cycles CPU kernels (backends named cpu-*) and the unmodified
Cycles Metal backend (metal-*) are both reference renderers. Every other backend (wgsl-*) is a
candidate. Every pair is compared and reported; the role of a pair decides whether it gates:
- gating: a candidate against the CPU gold standard of its own ISA family (the CPU film is
  the reference b): wgsl-linux-intel against cpu-x64-*, an arm64-host candidate against
  cpu-arm64-*; when no same-ISA CPU film is present, against the CPU film that is (the report
  says so). A WGSL film whose meta.json records wgslArithmetic (the CPU oracle arithmetic its
  WGSL adapters reproduced, from the device report) takes the family of that arithmetic:
  arm64-scalar (the Android ARM64 product default) gates against cpu-arm64-* (the Linux ARM64
  CPU oracle), x64-sse4.2 (the reference-oracle arithmetic) against cpu-x64-*. A wgsl-android-*
  film without the field (films made before it was recorded) is taken as x64, and the report
  says so (isaSource per backend);
- reported: every cross-ISA pair, a candidate against a Metal gold standard, the gold standards
  against each other, and two candidates against each other (same numbers, never a failure).
For a pair, the reference b of the practical criterion abs(a - b) <= 5e-5 + 2e-4 * abs(b) is
the gold standard (the CPU one when both are), else the earlier directory; the strict
diagnostic 1e-7 + 1e-7 * abs(b) is counted. Reported per pair: role, practical failures, strict
differences, maximum absolute and relative error, RMS, the worst pixel (largest ratio to the
tolerance) and NaN/Inf counts. Per backend: every repeat film is re-read and compared with
film-0 (bitwise identity and maximum difference), next to the meta.json repeat record. Films
whose bytes differ from their recorded SHA-256, shape or scene mismatches, NaN/Inf, and
practical failures or sample-count mismatches of gating pairs fail. Every repeat is judged:
repeat N of a pair's candidate (film-N and its passes) against repeat N of the reference (its
repeat 0 when the reference has no film-N); a pair's fields and "problems" hold repeat 0, its
"repeatResults" every repeat (repeat 0 first), and a gating failure of a later repeat goes to
"repeatProblems". The verdict is "perfect" (and "passed" true, exit 0) when nothing failed:
every gating pair passes the practical criterion in every repeat ("everyRepeatPassed"). A run
without a CPU gold standard is invalid input ("no gold standard", exit 2).

Named passes (meta.json "passes", files pass-K-N.f32 from codebrix_cycles_oracle): every pass of the
reference of a pair is compared with the candidate's pass of the same name and type, with the same
roles. Float passes use the practical and strict criteria above; integer passes (object_id,
material_id) must be bitwise equal (every differing value is a failure); cryptomatte passes are
compared per pixel as the set of (id, weight) entries: the ids must be the same bit patterns and the
weights of each id meet the practical criterion (their slot order, which the pinned kernel leaves to
the accumulation order for equal weights, is reported); render_time holds this run's measured
times and is reported, never judged. A pass that the reference has and a gating candidate lacks, a
pass of a different shape, NaN/Inf in any pass, and hash mismatches fail.
"""
import argparse,array,hashlib,itertools,json,math,shutil,sys,tempfile
from pathlib import Path
PRACTICAL=(5e-5,2e-4);STRICT=(1e-7,1e-7)


class Invalid(Exception):pass


def read_film(directory,meta,repeat):
 path=Path(directory)/f'film-{repeat}.f32';data=path.read_bytes()
 hashes=meta.get('filmSha256',[])
 if repeat>=len(hashes) or hashlib.sha256(data).hexdigest()!=hashes[repeat]:raise Invalid(f'{path} does not match its recorded SHA-256')
 # A bake's film (runner.cpp, "bake" scenes) has the bake's filmComponents per pixel; a render's film is RGBA.
 count=meta['width']*meta['height']*int(meta.get('filmComponents',4))
 if len(data)!=count*4:raise Invalid(f'{path} has {len(data)} bytes; expected {count*4}')
 values=array.array('f');values.frombytes(data)
 if sys.byteorder!='little':values.byteswap()
 return values


def load(directory,repeat=0):
 directory=Path(directory);meta=json.loads((directory/'meta.json').read_text())
 return meta,read_film(directory,meta,repeat)


def nonfinite(values):return sum(1 for v in values if not math.isfinite(v))


# Pass kinds (by the native pass type name): how a pass is judged.
EXACT_PASSES={'object_id','material_id'};TIMING_PASSES={'render_time'};CRYPTOMATTE_PASSES={'cryptomatte'}
def pass_kind(entry):
 t=entry.get('type') or ''
 return 'exact' if t in EXACT_PASSES else 'timing' if t in TIMING_PASSES else 'cryptomatte' if t in CRYPTOMATTE_PASSES else 'float'


def read_pass(directory,meta,entry,repeat):
 path=Path(directory)/f"pass-{entry['index']}-{repeat}.f32";data=path.read_bytes()
 hashes=entry.get('filmSha256',[])
 if repeat>=len(hashes) or hashlib.sha256(data).hexdigest()!=hashes[repeat]:raise Invalid(f'{path} does not match its recorded SHA-256')
 count=meta['width']*meta['height']*int(entry['components'])
 if len(data)!=count*4:raise Invalid(f'{path} has {len(data)} bytes; expected {count*4}')
 values=array.array('f');values.frombytes(data)
 if sys.byteorder!='little':values.byteswap()
 return values


def compare_exact(reference,candidate,width,components):
 differ=0;worst=None
 rb=array.array('I',reference.tobytes());cb=array.array('I',candidate.tobytes())
 for i,(b,a) in enumerate(zip(rb,cb)):
  if a!=b:
   differ+=1
   if worst is None:
    pixel,channel=divmod(i,components)
    worst=dict(x=pixel%width,y=pixel//width,channel=channel,reference=reference[i],candidate=candidate[i])
 return dict(bitwiseEqual=differ==0,practicalFailures=differ,strictDifferences=differ,maxAbsoluteDifference=None,firstDifference=worst)


def compare_cryptomatte(reference,candidate,width,components):
 """Per pixel: the (id, weight) slots as a map from the id's bit pattern to its weight."""
 failures=strict=order=0;max_abs=0.0;worst=None;ids=array.array('I',reference.tobytes());cids=array.array('I',candidate.tobytes())
 pixels=len(reference)//components
 for pixel in range(pixels):
  base=pixel*components
  rmap={};cmap={};rorder=[];corder=[]
  for k in range(0,components,2):
   if reference[base+k+1]!=0 or ids[base+k]!=0:rmap[ids[base+k]]=reference[base+k+1];rorder.append(ids[base+k])
   if candidate[base+k+1]!=0 or cids[base+k]!=0:cmap[cids[base+k]]=candidate[base+k+1];corder.append(cids[base+k])
  if rorder!=corder:order+=1
  if set(rmap)!=set(cmap):
   failures+=1
   if worst is None:worst=dict(x=pixel%width,y=pixel//width,reason='id set differs',reference=sorted(rmap),candidate=sorted(cmap))
   continue
  for key,b in rmap.items():
   a=cmap[key];difference=abs(a-b)
   if not (math.isfinite(a) and math.isfinite(b)):continue
   max_abs=max(max_abs,difference)
   if difference>PRACTICAL[0]+PRACTICAL[1]*abs(b):
    failures+=1
    if worst is None:worst=dict(x=pixel%width,y=pixel//width,reason='weight',id=key,reference=b,candidate=a)
   if difference>STRICT[0]+STRICT[1]*abs(b):strict+=1
 return dict(bitwiseEqual=reference.tobytes()==candidate.tobytes(),practicalFailures=failures,strictDifferences=strict,
  maxAbsoluteDifference=max_abs,slotOrderDifferences=order,firstDifference=worst)


def compare_pass(entry,reference,candidate,width):
 components=int(entry['components']);kind=pass_kind(entry)
 if kind=='exact':return compare_exact(reference,candidate,width,components)
 if kind=='cryptomatte':return compare_cryptomatte(reference,candidate,width,components)
 if kind=='timing':return dict(bitwiseEqual=reference.tobytes()==candidate.tobytes(),notJudged='render time is a measurement of this run, not a rendered value')
 return compare(reference,candidate,width,components)


def compare(reference,candidate,width,components=4):
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
   pixel,channel=divmod(i,components)
   worst=dict(x=pixel%width,y=pixel//width,channel='RGBA'[channel] if components==4 else channel,reference=b,candidate=a,absoluteDifference=difference,criterionRatio=ratio)
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


def role_of(backend):
 """'cpu' and 'metal' are the two gold standards; everything else is a candidate."""
 return 'cpu' if backend.startswith('cpu-') else 'metal' if backend.startswith('metal-') else 'candidate'


LEGACY_ANDROID='no wgslArithmetic in meta.json (legacy wgsl-android film): judged as x64'


def isa_source(backend,meta=None):
 """(ISA family, how it was decided). A WGSL film's recorded wgslArithmetic decides (arm64-* -> arm64, x64-* ->
 x64); a wgsl-android-* film without it is taken as x64 (the legacy rule: validated through the Linux x64 glibc
 flow); every other backend by its name."""
 arithmetic=(meta or {}).get('wgslArithmetic')
 if role_of(backend)=='candidate' and arithmetic:
  if arithmetic.startswith('arm64'):return 'arm64','wgslArithmetic '+arithmetic
  if arithmetic.startswith('x64'):return 'x64','wgslArithmetic '+arithmetic
  raise Invalid(f'{backend}: unknown wgslArithmetic {arithmetic!r}')
 if backend.startswith('wgsl-android-'):return 'x64',LEGACY_ANDROID
 return ('arm64' if 'arm64' in backend else 'x64'),'backend name'


def isa_of(backend,meta=None):
 """Host ISA family of a backend (see isa_source)."""
 return isa_source(backend,meta)[0]


def pair_role(a,b,cpu_isas,isas=None):
 """Gating: a candidate against the CPU gold standard of its own ISA family (or against any CPU film when no
 same-ISA CPU film is present). Everything else, cross-ISA pairs included, is reported. ISAS maps a backend to its
 family (from its meta.json); without it the family comes from the name."""
 roles={role_of(a),role_of(b)}
 if roles!={'candidate','cpu'}:return 'reported'
 cand,cpu=(a,b) if role_of(a)=='candidate' else (b,a)
 family=(isas or {}).get(cand) or isa_of(cand);cpu_family=(isas or {}).get(cpu) or isa_of(cpu)
 return 'gating' if family==cpu_family or family not in cpu_isas else 'reported'


def judged_scene(meta):
 """(scene id, scene sha256) a film is judged as. A gold-standard film rendered from a scene's oracle reference
 (render_oracles.py, a scene's "oracleReference") stands for the scene that names it, recorded in oracleReferenceFor."""
 named=meta.get('oracleReferenceFor')
 return (named['sceneId'],named['sceneSha256']) if named else (meta['sceneId'],meta['sceneSha256'])


def repeat_range(directory,meta):
 """The repeats a backend has films of: 0, then every later repeat whose film-N.f32 exists (as repeats() reads them)."""
 n=1
 while n<int(meta.get('repeats',1)) and (Path(directory)/f'film-{n}.f32').exists():n+=1
 return range(n)


def judge_repeat(rd,rm,rv,cd,cm,cv,rr,cr,first):
 """One repeat of a pair: the candidate's film CV and named passes of repeat CR against the reference's film RV and
 passes of repeat RR. Returns (the film comparison, the pass rows, the problems without the pair's name)."""
 film=compare(rv,cv,first['width'],int(first.get('filmComponents',4)));found=[]
 if film['practicalFailures']:found.append(f'{film["practicalFailures"]} practical failures')
 # Named passes of the reference, judged like Combined with the pair's role.
 passes=[];candidate_passes={e['name']:e for e in cm.get('passes',[])}
 for entry in rm.get('passes',[]):
  other=candidate_passes.get(entry['name']);row=dict(name=entry['name'],type=entry.get('type'),components=entry.get('components'),kind=pass_kind(entry))
  problem=None
  if not entry.get('written'):row['status']='not written by the reference'
  elif other is None or not other.get('written'):row['status']='missing in the candidate';problem='is missing'
  elif (other.get('type'),other.get('components'))!=(entry.get('type'),entry.get('components')):row['status']='different type or shape';problem='has a different type or shape'
  elif cr>=len(other.get('filmSha256',[])):row['status']='repeat not written by the candidate';problem='is missing in this repeat'
  else:
   p=rr if rr<len(entry.get('filmSha256',[])) else 0
   row.update(compare_pass(entry,read_pass(rd,rm,entry,p),read_pass(cd,cm,other,cr),first['width']));row['status']='compared'
   if row.get('practicalFailures'):problem=f"{row['practicalFailures']} {'differing values' if row['kind']=='exact' else 'practical failures'}"
  row['passed']=problem is None;passes.append(row)
  if problem:found.append(f'pass {entry["name"]} {problem}')
 return film,passes,found


def run(directories):
 films=[];problems=[]
 for directory in directories:
  meta,values=load(directory)
  films.append((Path(directory),meta,values))
  if nonfinite(values):problems.append(f'{directory}: {nonfinite(values)} NaN/Inf components')
  for entry in meta.get('passes',[]):
   if not entry.get('written'):continue
   for r in range(len(entry.get('filmSha256',[]))):
    bad=nonfinite(read_pass(directory,meta,entry,r))
    if bad:problems.append(f"{directory}: pass {entry['name']} repeat {r} has {bad} NaN/Inf components")
 first=films[0][1]
 if not any(role_of(m['backend'])=='cpu' for _,m,_ in films):
  raise Invalid('no gold standard: no cpu-* backend among '+', '.join(m['backend'] for _,m,_ in films))
 for directory,meta,_ in films[1:]:
  if judged_scene(meta)!=judged_scene(first):raise Invalid(f'{directory} rendered a different scene file')
  if (meta['width'],meta['height'],meta.get('filmComponents',4))!=(first['width'],first['height'],first.get('filmComponents',4)):raise Invalid(f'{directory} has a different film shape')
 backends=[]
 for d,m,v in films:
  rows=repeats(d,m,v)
  for row in rows:
   if row['nonfinite']:problems.append(f'{d}: repeat {row["repeat"]} has {row["nonfinite"]} NaN/Inf components')
  pass_repeats=[]
  for entry in m.get('passes',[]):
   if not entry.get('written'):continue
   p0=read_pass(d,m,entry,0)
   for r in range(1,len(entry.get('filmSha256',[]))):
    pr=read_pass(d,m,entry,r)
    pass_repeats.append(dict(name=entry['name'],repeat=r,bitwiseEqual=pr.tobytes()==p0.tobytes(),
     maxAbsoluteDifference=max((abs(a-b) for a,b in zip(pr,p0) if math.isfinite(a) and math.isfinite(b)),default=0.0)))
  backends.append(dict(directory=str(d),backend=m['backend'],samples=m['samples'],repeats=m['repeats'],**({'passRepeats':pass_repeats} if pass_repeats else {}),
   repeatBitwiseIdentical=m.get('repeatBitwiseIdentical'),repeatMaxAbsDifference=m.get('repeatMaxAbsDifference'),
   measuredRepeats=rows,measuredRepeatBitwiseIdentical=all(r['bitwiseEqual'] for r in rows),
   measuredRepeatMaxAbsDifference=max([0.0]+[r['maxAbsoluteDifference'] for r in rows]),
   deviceName=m.get('deviceName'),renderSeconds=m.get('renderSeconds'),nonfiniteComponents=nonfinite(v),
   rid=m.get('rid'),wgslArithmetic=m.get('wgslArithmetic'),isaFamily=isa_source(m['backend'],m)[0],isaSource=isa_source(m['backend'],m)[1]))
 report=dict(sceneId=judged_scene(first)[0],sceneSha256=judged_scene(first)[1],width=first['width'],height=first['height'],
  practicalCriterion='abs(a-b) <= 5e-5 + 2e-4*abs(b)',strictCriterion='abs(a-b) <= 1e-7 + 1e-7*abs(b)',backends=backends,
  goldStandards=[m['backend'] for _,m,_ in films if role_of(m['backend'])!='candidate'],
  candidates=[m['backend'] for _,m,_ in films if role_of(m['backend'])=='candidate'],pairs=[])
 rank={'cpu':0,'metal':1,'candidate':2}
 isas={m['backend']:isa_of(m['backend'],m) for _,m,_ in films}
 cpu_isas={isas[m['backend']] for _,m,_ in films if role_of(m['backend'])=='cpu'}
 report['oracleReferences']={m['backend']:dict(sceneId=m['sceneId'],sceneSha256=m['sceneSha256']) for _,m,_ in films if m.get('oracleReferenceFor')}
 report['crossIsaGating']=sorted(m['backend'] for _,m,_ in films if role_of(m['backend'])=='candidate' and isas[m['backend']] not in cpu_isas)
 report['legacyAndroidIsa']=sorted(m['backend'] for _,m,_ in films if isa_source(m['backend'],m)[1]==LEGACY_ANDROID)
 repeat_problems=[]
 for x,y in itertools.combinations(films,2):
  # The gold standard is the reference (the CPU one when both are gold); otherwise the earlier directory.
  (rd,rm,rv),(cd,cm,cv)=(y,x) if rank[role_of(y[1]['backend'])]<rank[role_of(x[1]['backend'])] else (x,y)
  role=pair_role(rm['backend'],cm['backend'],cpu_isas,isas);name=f'{cm["backend"]} vs {rm["backend"]}'
  # Every repeat of the candidate against the reference's film of the same repeat (its film-0 when it has fewer repeat
  # films, whose own repeats are compared with film-0 above). Repeat 0 fills the pair's fields as before.
  rows=[]
  for r in repeat_range(cd,cm):
   rr=r if r in repeat_range(rd,rm) else 0
   film,passes,found=judge_repeat(rd,rm,rv if rr==0 else read_film(rd,rm,rr),cd,cm,cv if r==0 else read_film(cd,cm,r),rr,r,first)
   rows.append(dict(repeat=r,referenceRepeat=rr,**film,practicalPassed=film['practicalFailures']==0,**({'passes':passes} if passes else {}),passed=not found))
   if r==0:
    result=dict(film,reference=rm['backend'],candidate=cm['backend'],role=role)
    result['practicalPassed']=result['practicalFailures']==0;result['samplesMatch']=rm['samples']==cm['samples']
    if role=='gating' and not result['samplesMatch']:problems.append(f'{cm["backend"]} used {cm["samples"]} samples; {rm["backend"]} used {rm["samples"]}')
    if passes:result['passes']=passes
   if role=='gating':(problems if r==0 else repeat_problems).extend(f'{name}: '+('' if r==0 else f'repeat {r}: ')+p for p in found)
  result['repeatResults']=rows;result['everyRepeatPassed']=all(row['passed'] for row in rows)
  report['pairs'].append(result)
 report['gatingPairs']=sum(1 for p in report['pairs'] if p['role']=='gating')
 report['everyRepeatPassed']=all(p['everyRepeatPassed'] for p in report['pairs'] if p['role']=='gating')
 report['problems']=problems;report['repeatProblems']=repeat_problems;report['passed']=not problems and not repeat_problems
 report['verdict']='perfect' if report['passed'] and len(films)>=2 else 'not perfect'
 return report


def discover(root,scene):
 root=Path(root)
 for base in (root/'oracle'/scene,root/scene):
  if base.is_dir():
   dirs=[d for d in base.iterdir() if (d/'meta.json').is_file()]
   return sorted(dirs,key=lambda d:(not d.name.startswith('cpu-'),not d.name.startswith('metal-'),d.name))
 raise Invalid(f'No backend directories for {scene} under {root}')


def table(report):
 lines=[f"{report['sceneId']} ({report['width']}x{report['height']})"]
 for b in report['backends']:
  lines.append(f"  {b['backend']:<26} {role_of(b['backend']):<9} samples {b['samples']:<5} repeats {b['repeats']} bitwise={b['measuredRepeatBitwiseIdentical']} "
               f"repeatMaxAbs={b['measuredRepeatMaxAbsDifference']:.3g} nonfinite={b['nonfiniteComponents']}"
               +(f" isa={b['isaFamily']} ({b['isaSource']})" if role_of(b['backend'])=='candidate' else ''))
 lines.append(f"  {'candidate vs reference':<52} {'maxAbs':>10} {'maxRel':>10} {'rms':>10} {'practicalFail':>13} {'strictDiff':>10}")
 for p in report['pairs']:
  lines.append(f"  {p['candidate']+' vs '+p['reference']+' ['+p['role']+']':<52} {p['maxAbsoluteDifference']:>10.3g} {p['maxRelativeError']:>10.3g} "
               f"{p['rms']:>10.3g} {p['practicalFailures']:>13} {p['strictDifferences']:>10}")
  w=p['worstPixel']
  if w:lines.append(f"      worst x={w['x']} y={w['y']} {w['channel']}: ref {w['reference']:.9g} got {w['candidate']:.9g} ({w['criterionRatio']:.3g}x tolerance)")
  for q in p.get('passes',[]):
   if q['status']!='compared':lines.append(f"      pass {q['name']:<24} {str(q['type']):<22} {q['status']}");continue
   if q['kind']=='timing':lines.append(f"      pass {q['name']:<24} {q['type']:<22} reported only (timing)");continue
   extra=f" slotOrderDiff={q['slotOrderDifferences']}" if 'slotOrderDifferences' in q else ''
   mx='exact' if q['kind']=='exact' else f"{q['maxAbsoluteDifference']:.3g}"
   lines.append(f"      pass {q['name']:<24} {q['type']:<22} maxAbs={mx:>10} practicalFail={q['practicalFailures']} strictDiff={q['strictDifferences']}{extra}")
  for q in p.get('repeatResults',[])[1:]:
   failed=[x['name'] for x in q.get('passes',[]) if not x['passed']]
   lines.append(f"      repeat {q['repeat']} vs reference repeat {q['referenceRepeat']}: maxAbs={q['maxAbsoluteDifference']:.3g} practicalFail={q['practicalFailures']} "
                f"strictDiff={q['strictDifferences']}"+(f" passesFailed={','.join(failed)}" if failed else '')+(' passed' if q['passed'] else ' FAILED' if p['role']=='gating' else ' differs (reported)'))
 if report.get('legacyAndroidIsa'):lines.append('  note: '+', '.join(report['legacyAndroidIsa'])+': '+LEGACY_ANDROID)
 if report.get('crossIsaGating'):lines.append('  note: no same-ISA CPU gold standard for '+', '.join(report['crossIsaGating'])+'; gated against the CPU film present')
 lines.append(f"  verdict: {report['verdict']}"+('' if report['passed'] else '; FAILED: '+'; '.join(report['problems']+report['repeatProblems'])))
 return '\n'.join(lines)


def self_test():
 """The comparator must reject NaN/Inf, shape mismatch, corruption and real differences."""
 root=Path(tempfile.mkdtemp(prefix='compare-self-test-'))
 def film(name,values,width=2,height=1,scene='s',repeat_values=None,passes=None,**extra):
  d=root/name;d.mkdir();hashes=[];entries=[]
  for k,(pname,ptype,components,*pvalues) in enumerate(passes or []):
   # One list of values per repeat (a pass without a second list has repeat 0 only).
   entries.append(dict(index=k,name=pname,type=ptype,components=components,written=True,filmSha256=[]))
   for r,vals in enumerate(pvalues):
    data=array.array('f',vals)
    if sys.byteorder!='little':data.byteswap()
    raw=data.tobytes();(d/f'pass-{k}-{r}.f32').write_bytes(raw);entries[-1]['filmSha256'].append(hashlib.sha256(raw).hexdigest())
  for r,vals in enumerate([values]+([repeat_values] if repeat_values else [])):
   data=array.array('f',vals)
   if sys.byteorder!='little':data.byteswap()
   raw=data.tobytes();(d/f'film-{r}.f32').write_bytes(raw);hashes.append(hashlib.sha256(raw).hexdigest())
  (d/'meta.json').write_text(json.dumps(dict(sceneId='self-test',sceneSha256=scene,backend=name,width=width,height=height,samples=1,repeats=len(hashes),
   repeatBitwiseIdentical=True,repeatMaxAbsDifference=0,filmSha256=hashes,**({'passes':entries} if entries else {}),**extra)))
  return d
 base=[0.5,0.25,1.0,1.0,0.0,2.0,3.0,1.0]
 try:
  checks={}
  r=run([film('cpu-a',base),film('wgsl-b',base)]);checks['identical']=r['passed'] and r['verdict']=='perfect'
  near=list(base);near[1]+=1e-6
  r=run([film('cpu-c',base),film('wgsl-d',near),film('wgsl-d2',base)]);checks['threeBackendsWithinPracticalPerfect']=r['verdict']=='perfect' and len(r['pairs'])==3
  far=list(base);far[5]+=1e-2
  r=run([film('cpu-e',base),film('wgsl-f',far)]);checks['beyondPracticalFails']=not r['passed'] and r['verdict']=='not perfect'
  nan=list(base);nan[2]=float('nan');inf=list(base);inf[3]=float('inf')
  checks['nanFails']=not run([film('cpu-g',base),film('wgsl-h',nan)])['passed']
  checks['infFails']=not run([film('cpu-i',base),film('wgsl-j',inf)])['passed']
  def invalid(dirs):
   try:run(dirs);return False
   except Invalid:return True
  checks['shapeMismatchFails']=invalid([film('cpu-k',base),film('wgsl-l',base+[0,0,0,0],width=3)])
  checks['sceneMismatchFails']=invalid([film('cpu-m',base),film('wgsl-n',base,scene='other')])
  corrupt=film('wgsl-o',base);data=bytearray((corrupt/'film-0.f32').read_bytes());data[0]^=1;(corrupt/'film-0.f32').write_bytes(bytes(data))
  checks['corruptedFilmFails']=invalid([film('cpu-p',base),corrupt])
  rep=film('wgsl-q',base,repeat_values=near);r=run([film('cpu-q0',base),rep])
  checks['repeatDifferenceMeasured']=r['backends'][1]['measuredRepeatBitwiseIdentical'] is False and r['backends'][1]['measuredRepeatMaxAbsDifference']>0
  badrep=film('wgsl-s',base,repeat_values=base);raw=bytearray((badrep/'film-1.f32').read_bytes());raw[4]^=1;(badrep/'film-1.f32').write_bytes(bytes(raw))
  checks['corruptedRepeatFails']=invalid([film('cpu-s0',base),badrep])
  # Two gold standards: a Metal film far from the CPU with the WGSL film close is perfect (Metal pairs reported).
  r=run([film('cpu-t',base),film('metal-t',far),film('wgsl-t',near)])
  checks['metalFarWgslClosePerfect']=r['verdict']=='perfect' and r['gatingPairs']==1 and not r['problems'] and \
   {(p['candidate'],p['reference'],p['role']) for p in r['pairs']}=={('wgsl-t','cpu-t','gating'),('metal-t','cpu-t','reported'),('wgsl-t','metal-t','reported')} and \
   all(p['practicalFailures']>0 for p in r['pairs'] if 'metal-t' in (p['candidate'],p['reference']))
  r=run([film('cpu-u',base),film('metal-u',base),film('wgsl-u',far)])
  checks['wgslFarFromCpuNotPerfect']=r['verdict']=='not perfect' and len(r['problems'])==1 and 'wgsl-u vs cpu-u' in r['problems'][0]
  r=run([film('cpu-v',base),film('metal-v',far)])
  checks['goldOnlyPerfectPairReported']=r['verdict']=='perfect' and r['gatingPairs']==0 and len(r['pairs'])==1 and r['pairs'][0]['role']=='reported' \
   and r['pairs'][0]['practicalFailures']>0 and r['goldStandards']==['cpu-v','metal-v'] and r['candidates']==[]
  r=run([film('wgsl-w',near),film('cpu-w',base)])
  checks['cpuIsTheReference']=r['pairs'][0]['reference']=='cpu-w' and r['pairs'][0]['role']=='gating'
  checks['noGoldStandardInvalid']=invalid([film('wgsl-x',base),film('metal-x',base)])
  # ISA families: an x64 candidate perfect against cpu-x64 but far from cpu-arm64 is perfect (arm64 pairs reported).
  r=run([film('cpu-x64-y',base),film('cpu-arm64-y',far),film('wgsl-linux-y',near)])
  checks['crossIsaReported']=r['verdict']=='perfect' and r['gatingPairs']==1 and \
   {(p['candidate'],p['reference'],p['role']) for p in r['pairs']}=={('wgsl-linux-y','cpu-x64-y','gating'),('cpu-arm64-y','cpu-x64-y','reported'),('wgsl-linux-y','cpu-arm64-y','reported')}
  r=run([film('cpu-arm64-z',base),film('wgsl-linux-arm64-z',far)])
  checks['arm64CandidateGatesOnArm64']=r['verdict']=='not perfect' and r['pairs'][0]['role']=='gating'
  r=run([film('cpu-arm64-w2',base),film('wgsl-linux-w2',far)])
  checks['noSameIsaCpuGatesOnPresentCpu']=r['verdict']=='not perfect' and r['crossIsaGating']==['wgsl-linux-w2']
  # Named passes: float passes by the practical criterion, integer passes exactly, cryptomatte as id sets.
  depth=[('Depth','depth',1,[1.5,2.0])]
  r=run([film('cpu-p1',base,passes=depth),film('wgsl-p1',base,passes=[('Depth','depth',1,[1.5,2.0+1e-6])])])
  checks['passWithinPracticalPerfect']=r['verdict']=='perfect' and r['pairs'][0]['passes'][0]['strictDifferences']==1
  r=run([film('cpu-p2',base,passes=depth),film('wgsl-p2',base,passes=[('Depth','depth',1,[1.5,2.1])])])
  checks['passBeyondPracticalFails']=r['verdict']=='not perfect' and 'pass Depth' in r['problems'][0]
  r=run([film('cpu-p3',base,passes=depth),film('wgsl-p3',base)])
  checks['missingPassFails']=r['verdict']=='not perfect' and 'is missing' in r['problems'][0]
  r=run([film('cpu-p3m',base,passes=depth),film('metal-p3m',base)])
  checks['missingPassInReportedPairPerfect']=r['verdict']=='perfect'
  ids=[('IndexOB','object_id',1,[1.0,2.0])]
  r=run([film('cpu-p4',base,passes=ids),film('wgsl-p4',base,passes=[('IndexOB','object_id',1,[1.0,2.0000002])])])
  checks['integerPassExact']=r['verdict']=='not perfect' and 'differing values' in r['problems'][0]
  crypto=[('CryptoObject00','cryptomatte',4,[3.0,0.6,5.0,0.4,7.0,1.0,0.0,0.0])]
  r=run([film('cpu-p5',base,passes=crypto),film('wgsl-p5',base,passes=[('CryptoObject00','cryptomatte',4,[5.0,0.4,3.0,0.6,7.0,1.0,0.0,0.0])])])
  checks['cryptomatteSlotOrderReported']=r['verdict']=='perfect' and r['pairs'][0]['passes'][0]['slotOrderDifferences']==1
  r=run([film('cpu-p6',base,passes=crypto),film('wgsl-p6',base,passes=[('CryptoObject00','cryptomatte',4,[3.0,0.6,6.0,0.4,7.0,1.0,0.0,0.0])])])
  checks['cryptomatteIdMismatchFails']=r['verdict']=='not perfect'
  r=run([film('cpu-p7',base,passes=depth),film('wgsl-p7',base,passes=[('Depth','depth',1,[1.5,float('nan')])])])
  checks['passNanFails']=r['verdict']=='not perfect'
  r=run([film('cpu-p8',base,passes=[('Time','render_time',1,[0.3,0.2])]),film('wgsl-p8',base,passes=[('Time','render_time',1,[0.0,0.0])])])
  checks['renderTimeReportedOnly']=r['verdict']=='perfect' and 'notJudged' in r['pairs'][0]['passes'][0]
  bad=film('wgsl-p9',base,passes=depth);raw=bytearray((bad/'pass-0-0.f32').read_bytes());raw[0]^=1;(bad/'pass-0-0.f32').write_bytes(bytes(raw))
  checks['corruptedPassFails']=invalid([film('cpu-p9',base,passes=depth),bad])
  # Android films record the arithmetic their WGSL reproduced (wgslArithmetic): the product default arm64-scalar
  # gates against the ARM64 CPU oracle, the reference-oracle arithmetic x64-sse4.2 against x64; legacy films
  # without the field stay x64 and are flagged.
  def roles(r):return {(p['candidate'],p['reference'],p['role']) for p in r['pairs'] if p['candidate'].startswith('wgsl')}
  r=run([film('cpu-x64-a1',base),film('cpu-arm64-a1',far),film('wgsl-android-a1',far,rid='android-arm64',wgslArithmetic='arm64-scalar')])
  checks['androidArm64ScalarGatesOnArm64']=r['verdict']=='perfect' and roles(r)=={('wgsl-android-a1','cpu-arm64-a1','gating'),('wgsl-android-a1','cpu-x64-a1','reported')} \
   and r['backends'][2]['isaFamily']=='arm64' and r['legacyAndroidIsa']==[]
  r=run([film('cpu-x64-a2',base),film('cpu-arm64-a2',near),film('wgsl-android-a2',base,rid='android-arm64',wgslArithmetic='arm64-scalar')])
  checks['androidArm64ScalarNearArm64Perfect']=r['verdict']=='perfect' and roles(r)=={('wgsl-android-a2','cpu-arm64-a2','gating'),('wgsl-android-a2','cpu-x64-a2','reported')}
  r=run([film('cpu-x64-a3',far),film('cpu-arm64-a3',base),film('wgsl-android-a3',far,rid='android-arm64',wgslArithmetic='arm64-scalar')])
  checks['androidArm64ScalarNotPerfect']=r['verdict']=='not perfect' and r['problems']==['wgsl-android-a3 vs cpu-arm64-a3: 1 practical failures']
  r=run([film('cpu-x64-a4',base),film('cpu-arm64-a4',far),film('wgsl-android-a4',near,rid='android-arm64',wgslArithmetic='x64-sse4.2')])
  checks['androidReferenceOracleGatesOnX64']=r['verdict']=='perfect' and roles(r)=={('wgsl-android-a4','cpu-x64-a4','gating'),('wgsl-android-a4','cpu-arm64-a4','reported')} \
   and r['backends'][2]['isaFamily']=='x64'
  r=run([film('cpu-x64-a5',base),film('cpu-arm64-a5',far),film('wgsl-android-a5',near)])
  checks['legacyAndroidFilmIsX64AndFlagged']=r['verdict']=='perfect' and roles(r)=={('wgsl-android-a5','cpu-x64-a5','gating'),('wgsl-android-a5','cpu-arm64-a5','reported')} \
   and r['legacyAndroidIsa']==['wgsl-android-a5'] and r['backends'][2]['isaSource']==LEGACY_ANDROID
  r=run([film('cpu-arm64-a6',base),film('wgsl-linux-a6',far,rid='linux-x64',wgslArithmetic='arm64-scalar')])
  checks['desktopArm64ScalarGatesOnArm64']=r['verdict']=='not perfect' and r['pairs'][0]['role']=='gating' and r['crossIsaGating']==[]
  checks['unknownArithmeticInvalid']=invalid([film('cpu-x64-a7',base),film('wgsl-android-a7',base,wgslArithmetic='mips')])
  # Every repeat gates: repeat N of the candidate against repeat N of the reference (its repeat 0 when it has no film-N).
  # Repeat 0's numbers and problems stay where they were; a later repeat's failures go to repeatProblems.
  def cpu2(name):return film(name,base,repeat_values=base)
  r=run([cpu2('cpu-r1'),film('wgsl-r1',base,repeat_values=far)])
  checks['repeat0PassesRepeat1FailsNotPerfect']=r['verdict']=='not perfect' and not r['passed'] and r['problems']==[] and not r['everyRepeatPassed'] \
   and r['repeatProblems']==['wgsl-r1 vs cpu-r1: repeat 1: 1 practical failures'] and r['pairs'][0]['practicalFailures']==0 \
   and [(q['repeat'],q['referenceRepeat'],q['practicalFailures'],q['passed']) for q in r['pairs'][0]['repeatResults']]==[(0,0,0,True),(1,1,1,False)]
  r=run([cpu2('cpu-r2'),film('wgsl-r2',near,repeat_values=base)])
  checks['bothRepeatsPassPerfect']=r['verdict']=='perfect' and r['everyRepeatPassed'] and r['repeatProblems']==[] and len(r['pairs'][0]['repeatResults'])==2
  r=run([cpu2('cpu-r3'),film('wgsl-r3',far,repeat_values=base)])
  checks['repeat0FailsAsBefore']=r['verdict']=='not perfect' and r['problems']==['wgsl-r3 vs cpu-r3: 1 practical failures'] and r['repeatProblems']==[]
  r=run([cpu2('cpu-r4'),film('wgsl-r4',far,repeat_values=far)])
  checks['bothRepeatsFailCountsNotDoubled']=r['verdict']=='not perfect' and r['pairs'][0]['practicalFailures']==1 \
   and r['problems']==['wgsl-r4 vs cpu-r4: 1 practical failures'] and r['repeatProblems']==['wgsl-r4 vs cpu-r4: repeat 1: 1 practical failures']
  r=run([film('cpu-r5',base),film('wgsl-r5',near)])
  checks['oneRepeatAsBefore']=r['verdict']=='perfect' and [q['repeat'] for q in r['pairs'][0]['repeatResults']]==[0]
  r=run([cpu2('cpu-r6'),film('wgsl-r6',base,repeat_values=nan)])
  checks['nanInRepeat1Fails']=r['verdict']=='not perfect' and any('repeat 1 has 1 NaN/Inf' in x for x in r['problems'])
  r=run([film('cpu-r7',base),film('wgsl-r7',base,repeat_values=far)])
  checks['referenceWithoutRepeatGivesFilm0']=r['verdict']=='not perfect' and r['pairs'][0]['repeatResults'][1]['referenceRepeat']==0 \
   and r['repeatProblems']==['wgsl-r7 vs cpu-r7: repeat 1: 1 practical failures']
  r=run([cpu2('cpu-r8'),film('metal-r8',base,repeat_values=far),film('wgsl-r8',base,repeat_values=near)])
  checks['reportedPairRepeatNeverFails']=r['verdict']=='perfect' and r['everyRepeatPassed'] and \
   any(not p['everyRepeatPassed'] for p in r['pairs'] if p['role']=='reported')
  r=run([film('cpu-r9',base,repeat_values=base,passes=[('Depth','depth',1,[1.5,2.0],[1.5,2.0])]),
         film('wgsl-r9',base,repeat_values=base,passes=[('Depth','depth',1,[1.5,2.0],[1.5,2.1])])])
  checks['passBeyondPracticalInRepeat1Fails']=r['verdict']=='not perfect' and r['problems']==[] and r['repeatProblems']==['wgsl-r9 vs cpu-r9: repeat 1: pass Depth 1 practical failures']
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
