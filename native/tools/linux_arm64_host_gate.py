#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Audit Linux ARM64 build inputs and exercise native CPU hosts under QEMU."""
import argparse,json,math,os,re,shutil,struct,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'native/tools'))
from build import PACKAGE
from device_gate import sha,save
from render_gate import inputs
BASE=REPO/'native/work/linux/linux-arm64';SDK=Path('/usr/aarch64-linux-gnu');QEMU=Path('/usr/bin/qemu-aarch64')


def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--references',type=Path,required=True);a=p.parse_args()
 host=BASE/'host';build=host/'build';prefix=BASE/'dependencies/install';out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 state=dict(complete=False,phase='binary-review',rid='linux-arm64',emulatedCpu=True,actualArm64GpuProven=False);save(out/'status.json',state)
 results={};comparisons=[];commands=[]
 env=dict(os.environ);env.pop('OCIO',None);env.pop('LD_LIBRARY_PATH',None)
 runtime=PACKAGE/'runtimes/linux-arm64/native/libcodebrix_wgpu.so';search=':'.join(map(str,[build/'lib',prefix/'lib',runtime.parent,SDK/'lib']))
 runner=[str(QEMU),'-L',str(SDK),'-E','LD_LIBRARY_PATH='+search]
 sources=inputs();pins={}
 def call(command,name,timeout=900):
  commands.append(dict(command=list(map(str,command)),timeoutSeconds=timeout));save(out/'commands.json',commands)
  state['phase']=name;save(out/'status.json',state)
  with (out/(name+'.stdout.txt')).open('w') as stdout,(out/(name+'.stderr.txt')).open('w') as stderr:
   subprocess.run(command,stdout=stdout,stderr=stderr,check=True,timeout=timeout,env=env)
  return (out/(name+'.stdout.txt')).read_text()
 def report(binary,arguments,name,timeout=900):
  rows=[json.loads(line) for line in call([*runner,build/'bin'/binary,*arguments],name,timeout).splitlines() if line.strip()]
  assert rows and all(row['passed'] for row in rows);results[name]=rows;save(out/'results.json',results);return rows
 def compare(actual,ref,kind,bitwise=False,strict=False):
  x,y=actual.read_bytes(),ref.read_bytes();v,w=struct.unpack('<3072f',x),struct.unpack('<3072f',y)
  assert all(math.isfinite(f) for f in v+w)
  errors=[abs(i-j) for i,j in zip(v,w)]
  comparisons.append(dict(film=str(actual),reference=str(ref),kind=kind,filmSha256=sha(actual),bitwiseEqual=x==y,bitwiseRequired=bitwise,strictRequired=strict,
    maximumAbsoluteError=max(errors),practicalFailures=sum(e>5e-5+2e-4*abs(b) for e,b in zip(errors,w)),strictDifferences=sum(e>1e-7+1e-7*abs(b) for e,b in zip(errors,w))))
 try:
  status=json.loads((host/'status.json').read_text());inv=json.loads((host/'invocation.json').read_text());assert status['complete'] and status['passed']
  assert inv['sourceSha256']==sources
  pins={str(build/name):digest for name,digest in status['artifacts'].items()};pins[str(runtime)]=inv['runtimeSha256']
  marker=prefix/'codebrix-linux-arm64-build.json';dependency=json.loads(marker.read_text());assert sha(marker)==inv['dependencyMarkerSha256']
  pins.update({str(prefix/name):digest for name,digest in dependency['libraries'].items()})
  assert all(sha(Path(path))==digest for path,digest in pins.items())
  compiled=[];headers={};archives={};links={}
  directories=[build,*[BASE/'dependencies'/name for name in json.loads((BASE/'dependencies/status.json').read_text())['completedComponents']]]
  allowed=[REPO,SDK,Path('/usr/lib/gcc-cross/aarch64-linux-gnu'),PACKAGE]
  for directory in directories:
   database=directory/'compile_commands.json'
   if database.exists():
    for row in json.loads(database.read_text()):
     path=Path(row['file']).resolve();assert path.is_relative_to(REPO),'External implementation: '+str(path)
     assert '/third_party/atomic/' not in str(path)
     compiled.append(dict(path=str(path),sha256=sha(path),command=row.get('command',row.get('arguments'))))
   for file in directory.rglob('*.o.d'):
    for name in file.read_text().replace('\\\n',' ').split(':',1)[-1].split():
     path=Path(name);path=path if path.is_absolute() else directory/path
     if not path.is_file():continue
     path=path.resolve();assert any(path.is_relative_to(root) for root in allowed),'Unexpected compiler header: '+str(path)
     assert '/third_party/atomic/' not in str(path);headers[str(path)]=sha(path)
   archives.update({str(path):sha(path) for path in directory.rglob('*.a')})
   links.update({str(path):path.read_text() for path in directory.rglob('link.txt')})
  files=sorted({Path(path).resolve() for path in pins if Path(path).suffix!='.a'})
  elf=[]
  for path in files:
   data=path.read_bytes();assert data[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0]==183,path
   offset=struct.unpack_from('<Q',data,32)[0];size,count=struct.unpack_from('<HH',data,54);loads=[]
   for i in range(count):
    kind,flags,position,address,physical,file_size,memory_size,alignment=struct.unpack_from('<IIQQQQQQ',data,offset+size*i)
    if kind==1:loads.append(dict(alignment=alignment,offset=position,address=address));assert alignment>=4096 and position%alignment==address%alignment
   assert loads
   dynamic=subprocess.check_output(['aarch64-linux-gnu-readelf','-d','-V',str(path)],text=True)
   elf.append(dict(path=str(path),sha256=sha(path),needed=re.findall(r'\(NEEDED\).*?\[(.*?)\]',dynamic),loadSegments=loads,dynamicAndVersions=dynamic))
  library=build/'lib/libcodebrix_cycles_host.so'
  exported=subprocess.check_output(['aarch64-linux-gnu-nm','-D','--defined-only',str(library)],text=True)
  actual={line.split()[-1].split('@')[0] for line in exported.splitlines()}-{'CBC_HOST_EXPERIMENT_1'}
  expected=set(re.findall(r'\b(cbc_host_\w+)\(', (REPO/'native/host/cycles_host.h').read_text()));assert actual==expected and len(actual)==38
  for kind in ['session','session64','image']:
   link=(build/('codebrix_host_'+kind+'_reference.map')).read_text()
   assert 'CMakeFiles/codebrix_host_reference_color.dir/src/util/colorspace.cpp.o' in link
   assert not re.search(r'libcycles_util\.a\([^)]*colorspace',link)
  loader=[]
  for path in [library,build/'bin/codebrix_host_cpu_test',*[build/'bin'/('codebrix_host_'+kind+'_reference') for kind in ['session','session64','image']]]:
   output=call([*runner,SDK/'lib/ld-linux-aarch64.so.1','--list',path],'loader-'+path.name,120)
   assert 'not found' not in output
   resolved=[]
   for name in re.findall(r'=>\s+(/\S+)',output):
    dependency_path=Path(name).resolve();assert any(dependency_path.is_relative_to(root) for root in [prefix,build,SDK,runtime.parent]),dependency_path
    pins[str(dependency_path)]=sha(dependency_path);resolved.append(dict(path=str(dependency_path),sha256=sha(dependency_path)))
   assert resolved;loader.append(dict(path=str(path),resolved=resolved,output=output))
  save(out/'binary-review.json',dict(passed=True,compiledSources=compiled,compilerHeaders=headers,staticArchives=archives,linkCommands=links,elf=elf,exports=sorted(actual),loaderClosure=loader,
   scope='Compiler/header/archive inputs, ARM64 ELF and emulated target-loader closure. External installed GCC/Linux SDK; no native ARM64 GPU or portable release-baseline claim.'))
  save(out/'invocation.json',dict(sourceSha256=sources,binarySha256=pins,buildInvocationSha256=sha(host/'invocation.json'),qemuSha256=sha(QEMU)))
  for name,kind in [('cpu','session'),('cpu64','session64')]:
   folder=out/name;folder.mkdir();rows=report('codebrix_host_'+kind+'_reference',['CPU',folder/'run'],kind+'-reference')
   assert rows[0]['actualSession'] and rows[0]['renders']==3 and rows[0]['device']=='CPU'
  for mode in ['cpu','cpu-exit']:
   folder=out/(mode+'-consumer');folder.mkdir();report('codebrix_host_cpu_test',[folder/'run',*(['--exit-live'] if mode=='cpu-exit' else [])],mode)
   for i in range(3):compare(folder/f'run.{i}.rgba.bin',out/'cpu'/f'run.{i}.rgba.bin','same-target CPU lifecycle',bitwise=True)
  refs=out/'references';refs.mkdir()
  for variant in ['a','b','c']:
   folder=refs/variant/'output';rows=report('codebrix_host_image_reference',[REPO/'native/tests/host/configs'/f'{variant}.ocio',folder],'image-reference-'+variant,1800)
   assert rows[0]['originalNativeCpuSession'] and rows[0]['cases']==260
  reload=out/'reload'
  for i in range(6):(reload/f'round.{i}').mkdir(parents=True)
  rows=report('codebrix_host_reload_test',[refs,reload,'6'],'reload',2400);assert len(rows)==7 and rows[-1]['films']==1560
  for i in range(6):
   for j in range(260):compare(reload/f'round.{i}/case.{j}.rgba.bin',refs/chr(ord('a')+i%3)/'output'/f'case.{j}.rgba.bin','same-target color reload',bitwise=True)
  stress_input=out/'stress-input';stress_input.mkdir();stress_output=out/'stress-output';stress_output.mkdir()
  for variant in ['a','b']:shutil.copy2(REPO/'native/tests/host/configs'/f'{variant}.ocio',stress_input)
  shutil.copy2(refs/'a/output/input.5.bin',stress_input)
  rows=report('codebrix_host_concurrency_test',[stress_input,stress_output],'concurrency',1200)
  assert all(rows[0][key]==value for key,value in dict(concurrentHostLifetimes=192,destroyDuringRender=12,racingReaders=48,guardedConfigurationRejections=256,hostLiveConfigurationRejections=192).items())
  for name in ['guarded','after-invalid']:compare(stress_output/(name+'.rgba.bin'),refs/'a/output/case.177.rgba.bin','same-target concurrent lifetime',bitwise=True)
  linux=a.references.resolve();assert json.loads((linux/'status.json').read_text())['passed'];old=json.loads((linux/'invocation.json').read_text())
  assert all(sha(Path(path))==digest for path,digest in old['referenceSha256'].items())
  for name in ['native/tests/host/session_reference.cpp','native/tests/host/image_reference.cpp','native/tests/host/image_inputs.h']:assert sha(REPO/name)==old['sourceSha256'][name]
  for i in range(3):compare(out/'cpu'/f'run.{i}.rgba.bin',linux/'cpu'/f'run.{i}.rgba.bin','cross-platform CPU lifecycle',strict=True)
  for variant in ['a','b','c']:
   for i in range(260):compare(refs/variant/'output'/f'case.{i}.rgba.bin',linux/'references'/variant/'output'/f'case.{i}.rgba.bin','cross-platform original CPU images')
  assert inputs()==sources and all(sha(Path(path))==digest for path,digest in pins.items())
  failures=[row for row in comparisons if row['practicalFailures'] or (row['strictRequired'] and row['strictDifferences']) or (row['bitwiseRequired'] and not row['bitwiseEqual'])]
  state.update(complete=True,phase='complete',passed=not failures,filmsCompared=len(comparisons),failedFilms=len(failures),
   practicalFailures=sum(row['practicalFailures'] for row in comparisons),requiredStrictDifferences=sum(row['strictDifferences'] for row in comparisons if row['strictRequired']),
   strictImageDiagnostics=sum(row['strictDifferences'] for row in comparisons if not row['strictRequired']),
   sameTargetBitwiseFailures=sum(row['bitwiseRequired'] and not row['bitwiseEqual'] for row in comparisons),inputsUnchanged=True)
  print(json.dumps(state),flush=True)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(out/'comparisons.json',comparisons);save(out/'status.json',state)
 return 0 if state['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
