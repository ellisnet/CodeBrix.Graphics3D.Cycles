#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Review the osx-arm64 host build and exercise its CPU and Metal lifecycles.

    python3 native/tools/macos_host_gate.py --output native/work/macos-validation-v1

Same-host CPU films must match the independent native Session and image
references bit for bit, through the C API and the thin .NET wrapper alike.
This profile reads no image files, so the image reference and the reload
fixtures run their 256 memory-image cases and verify that texture filenames are
rejected; the four file cases apply only to builds that read files. Metal films
are compared with the CPU references using the practical criterion and reported;
disagreements are findings, never adjusted.
"""
import argparse,ctypes,json,math,os,re,struct,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'tools/dependencies'))
from macos_checks import linked,tool
import hashlib
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
BASE=REPO/'native/work/macos/osx-arm64';FLOATS=32*24*4


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 host=BASE/'host';build=host/'build';prefix=BASE/'dependencies/install';out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 state=dict(complete=False,phase='binary-review',rid='osx-arm64');save(out/'status.json',state)
 results={};comparisons=[];commands=[]
 env=dict(os.environ)
 for name in ['OCIO','DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','CYCLES_METALRT','CYCLES_METAL_SPECIALIZATION_LEVEL']:env.pop(name,None)
 def call(command,name,timeout=900,extra=None):
  commands.append(dict(command=list(map(str,command)),environment=extra or {},timeoutSeconds=timeout));save(out/'commands.json',commands)
  state['phase']=name;save(out/'status.json',state)
  with (out/(name+'.stdout.txt')).open('w') as stdout,(out/(name+'.stderr.txt')).open('w') as stderr:
   subprocess.run(list(map(str,command)),stdout=stdout,stderr=stderr,check=True,timeout=timeout,env={**env,**(extra or {})})
  return (out/(name+'.stdout.txt')).read_text()
 def report(binary,arguments,name,timeout=900,extra=None):
  rows=[json.loads(line) for line in call([build/'bin'/binary,*arguments],name,timeout,extra).splitlines() if line.strip()]
  assert rows and all(row['passed'] for row in rows),rows;results[name]=rows;save(out/'results.json',results);return rows
 def compare(actual,ref,kind,bitwise=False,practical=True):
  x,y=actual.read_bytes(),ref.read_bytes();v,w=struct.unpack(f'<{FLOATS}f',x),struct.unpack(f'<{FLOATS}f',y)
  assert all(math.isfinite(f) for f in v+w)
  errors=[abs(i-j) for i,j in zip(v,w)]
  comparisons.append(dict(film=str(actual),reference=str(ref),kind=kind,filmSha256=sha(actual),bitwiseEqual=x==y,bitwiseRequired=bitwise,
   practicalRequired=practical,maximumAbsoluteError=max(errors),practicalFailures=sum(e>5e-5+2e-4*abs(b) for e,b in zip(errors,w)),
   strictDifferences=sum(e>1e-7+1e-7*abs(b) for e,b in zip(errors,w))))
 try:
  status=json.loads((host/'status.json').read_text());assert status['complete'] and status['passed']
  pins={str(build/name):digest for name,digest in status['artifacts'].items()}
  dependency=json.loads((prefix/'codebrix-osx-arm64-build.json').read_text())
  pins.update({str(prefix/name):digest for name,digest in dependency['libraries'].items()})
  assert all(sha(Path(path))==digest for path,digest in pins.items())
  library=build/'lib/libcodebrix_cycles_host.dylib'
  exported={line.split()[-1].lstrip('_') for line in tool('nm','-gU',str(library)).splitlines() if ' T ' in line}
  expected=set(re.findall(r'\b(cbc_host_\w+)\(',(REPO/'native/host/cycles_host.h').read_text()));assert exported==expected and len(exported)==48
  closure={};review=[]
  for path in [library,*[build/'bin'/name for name in ['codebrix_host_cpu_test','codebrix_host_gpu_test','codebrix_host_session_reference']]]:
   names=linked(path)
   for name in names:assert name.startswith(('@rpath/','/usr/lib/','/System/Library/')),(path,name)
   review.append(dict(path=str(path),sha256=sha(path),linked=names,minimumOs=re.findall(r'minos (\S+)',tool('otool','-l',str(path))),
    signature=tool('codesign','-dv',str(path)).splitlines()))
   subprocess.run(['codesign','--verify',str(path)],check=True)
  frameworks=sorted({n for r in review for n in r['linked'] if n.startswith('/System/')})
  assert any('/Metal.framework/' in f for f in frameworks),frameworks
  codecs=[n for n in status['dependencyClosure'] if re.match(r'lib(jpeg|png|tiff|webp|gif|heif|raw|openjp|avif|jxl)',n)];assert not codecs,codecs
  for kind in ['session','session64','image']:
   link=(build/('codebrix_host_'+kind+'_reference.map')).read_text()
   assert 'codebrix_host_reference_color.dir' in link and 'colorspace.cpp.o' in link
  save(out/'binary-review.json',dict(passed=True,exports=sorted(exported),binaries=review,frameworks=frameworks,dependencyClosure=status['dependencyClosure'],
   scope='osx-arm64 exports, Mach-O minimum OS, ad-hoc signatures and load closure; reference executables link the original color implementation'))

  # Contract checks through the public API, without any test executable.
  api=ctypes.CDLL(str(library));config=(ctypes.c_uint32*8)(32,1,1,1,0,0,0,0);handle=ctypes.c_uint64(0)
  webgpu=api.cbc_host_create(config,ctypes.byref(handle))
  config[2]=2;assert api.cbc_host_create(config,ctypes.byref(handle))==0
  node=ctypes.c_uint64(0);shader=ctypes.c_uint64(0);assert api.cbc_host_create_node(handle,b'shader',ctypes.byref(shader))==0
  assert api.cbc_host_shader_node(handle,shader,b'image_texture',ctypes.byref(node))==0
  rejected=api.cbc_host_set_string(handle,node,b'filename',b'/tmp/texture.png');cleared=api.cbc_host_set_string(handle,node,b'filename',b'')
  assert api.cbc_host_destroy(handle)==0
  contract=dict(webgpuCreateStatus=webgpu,filenameSetStatus=rejected,emptyFilenameStatus=cleared)
  assert contract==dict(webgpuCreateStatus=2,filenameSetStatus=2,emptyFilenameStatus=0),contract
  results['contract']=contract;save(out/'results.json',results)

  for name,kind in [('cpu','session'),('cpu64','session64')]:
   folder=out/name;folder.mkdir();rows=report('codebrix_host_'+kind+'_reference',['CPU',folder/'run'],kind+'-reference')
   assert rows[0]['actualSession'] and rows[0]['renders']==3 and rows[0]['device']=='CPU'
  for mode in ['cpu','cpu-exit']:
   folder=out/(mode+'-consumer');folder.mkdir();report('codebrix_host_cpu_test',[folder/'run',*(['--exit-live'] if mode=='cpu-exit' else [])],mode)
   for i in range(3):compare(folder/f'run.{i}.rgba.bin',out/'cpu'/f'run.{i}.rgba.bin','same-host CPU lifecycle',bitwise=True)
  # Thin .NET wrapper: same films as the native Session, bit for bit.
  managed=out/'managed-bin'
  subprocess.run(['dotnet','build',str(REPO/'tools/hosttests/HostLifecycle/HostLifecycle.csproj'),'-c','Release','-o',str(managed)],check=True,capture_output=True)
  folder=out/'managed-run'
  rows=[json.loads(line) for line in call(['dotnet',managed/'HostLifecycle.dll',out/'cpu',folder],'managed',900,
   {'DYLD_LIBRARY_PATH':str(build/'lib')}).splitlines() if line.strip().startswith('{')]
  assert rows and rows[-1]['passed'] and rows[-1]['cpuReferenceBitwiseEqual'],rows;results['managed']=rows;save(out/'results.json',results)
  for i in range(3):compare(folder/f'run.{i}.rgba.bin',out/'cpu'/f'run.{i}.rgba.bin','managed CPU lifecycle',bitwise=True)
  # Metal: shipped settings, then MetalRT forced on (the default path on M3+).
  for mode,extra in [('metal',{}),('metal-metalrt',{'CYCLES_METALRT':'1'})]:
   folder=out/(mode+'-consumer');folder.mkdir()
   rows=report('codebrix_host_gpu_test',[folder/'run'],mode,1800,extra);assert rows[0].get('explicitMetal'),rows
   for i in range(3):compare(folder/f'run.{i}.rgba.bin',out/'cpu'/f'run.{i}.rgba.bin',f'{mode} vs same-host CPU Session reference')
  # Image oracle (memory images only in this profile), one per OCIO configuration.
  refs=out/'references';refs.mkdir();cases=256
  for variant in ['a','b','c']:
   rows=report('codebrix_host_image_reference',[REPO/'native/tests/host/configs'/f'{variant}.ocio',refs/variant/'output'],'image-reference-'+variant,1800)
   assert rows[0]['originalNativeCpuSession'] and rows[0]['cases']==cases and rows[0]['fileAndRebindCases']==0,rows
  reload=out/'reload'
  for i in range(6):(reload/f'round.{i}').mkdir(parents=True)
  rows=report('codebrix_host_reload_test',[refs,reload,'6'],'reload',2400);assert len(rows)==7 and rows[-1]['films']==6*cases,rows
  for i in range(6):
   for j in range(cases):compare(reload/f'round.{i}/case.{j}.rgba.bin',refs/chr(ord('a')+i%3)/'output'/f'case.{j}.rgba.bin','same-host color reload',bitwise=True)
  # The same image modes and colour reloads through the thin .NET wrapper.
  for project in ['ImageModes','ColorReload']:
   subprocess.run(['dotnet','build',str(REPO/'tools/hosttests'/project/(project+'.csproj')),'-c','Release','-o',str(out/'managed-bin'/project),
    '--configfile',str(REPO/'tools/hosttests/NuGet.Config')],check=True,capture_output=True)
  managed_reload=out/'managed-reload'
  for i in range(6):(managed_reload/f'round.{i}').mkdir(parents=True)
  rows=[json.loads(line) for line in call(['dotnet',out/'managed-bin/ColorReload/ColorReload.dll',out/'managed-bin/ImageModes/ImageModes.dll',refs,managed_reload],
   'managed-reload',2400,{'DYLD_LIBRARY_PATH':str(build/'lib')}).splitlines() if line.strip().startswith('{')]
  assert rows and rows[-1]['passed'] and rows[-1]['sameManagedProcess'] and rows[-1]['films']==6*cases,rows;results['managed-reload']=rows;save(out/'results.json',results)
  for i in range(6):
   for j in range(cases):compare(managed_reload/f'round.{i}/case.{j}.rgba.bin',refs/chr(ord('a')+i%3)/'output'/f'case.{j}.rgba.bin','managed color reload',bitwise=True)
  stress_input=out/'stress-input';stress_input.mkdir();stress_output=out/'stress-output';stress_output.mkdir()
  for variant in ['a','b']:(stress_input/f'{variant}.ocio').write_bytes((REPO/'native/tests/host/configs'/f'{variant}.ocio').read_bytes())
  (stress_input/'input.5.bin').write_bytes((refs/'a/output/input.5.bin').read_bytes())
  rows=report('codebrix_host_concurrency_test',[stress_input,stress_output],'concurrency',1200)
  assert all(rows[0][key]==value for key,value in dict(concurrentHostLifetimes=192,destroyDuringRender=12,racingReaders=48,guardedConfigurationRejections=256,hostLiveConfigurationRejections=192).items()),rows
  for name in ['guarded','after-invalid']:compare(stress_output/(name+'.rgba.bin'),refs/'a/output/case.177.rgba.bin','same-host concurrent lifetime',bitwise=True)
  assert all(sha(Path(path))==digest for path,digest in pins.items())
  required=[r for r in comparisons if (r['bitwiseRequired'] and not r['bitwiseEqual'])]
  metal=[r for r in comparisons if r['kind'].startswith('metal')]
  state.update(complete=True,phase='complete',passed=not required,filmsCompared=len(comparisons),sameHostBitwiseFailures=len(required),
   metalPracticalFailures=sum(r['practicalFailures'] for r in metal),metalStrictDifferences=sum(r['strictDifferences'] for r in metal),
   metalMaximumAbsoluteError=max(r['maximumAbsoluteError'] for r in metal),
   notApplicable=dict(fileAndRebindCases='4 per round: this profile reads no image files; texture filenames are verified as rejected instead'),
   inputsUnchanged=True)
  print(json.dumps(state),flush=True)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(out/'comparisons.json',comparisons);save(out/'status.json',state)
 return 0 if state['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
