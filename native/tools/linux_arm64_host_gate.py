#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Audit Linux ARM64 build inputs and exercise the native hosts.

    python3 native/tools/linux_arm64_host_gate.py --output native/work/linux-arm64-validation-<n> \
        --references <passed Linux x64 host_gate.py output> [--device-gates]

On Linux x64 (the cross-build machine) the executables run under QEMU and prove
emulated CPU paths only. On an ARM64 Linux machine (a Raspberry Pi 5, native
build) they run directly, the binary review uses the system loader and
binutils, and the gate adds the GPU steps of host_gate.py and
windows_host_gate.py: the WebGPU lifecycle, the ABI and example consumers on
CBC_HOST_GPU (GPU films against the same-host CPU references), the driver
coverage scenes and, with --device-gates, every built device gate executable
with its own JSON verdict.
"""
import argparse,json,math,os,platform,re,shutil,struct,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'native/tools'))
from build import PACKAGE
from device_gate import sha,save
from render_gate import inputs
NATIVE=platform.system()=='Linux' and platform.machine()=='aarch64'
BASE=REPO/'native/work/linux/linux-arm64';SDK=Path('/usr/aarch64-linux-gnu');QEMU=Path('/usr/bin/qemu-aarch64')
REVISION=json.loads((REPO/'third_party/cycles-import.json').read_text())['revision']
# webgpu.h WGPUBackendType: the Linux runtime reaches the GPU through Vulkan.
VULKAN=6
# As windows_host_gate.py: one oracle scene per class of scene a GPU driver has
# rendered wrongly or refused, CPU and GPU film compared with the practical criterion.
DRIVER_COVERAGE={'smooth-shading':'native/oracle/scenes/linux-gates/glass-render.json',
 'motion-normal-map':'native/oracle/scenes/linux-gates/deforming-motion-1.json',
 'glass-area-light':'native/oracle/scenes/starter/glass-and-area-light.json',
 'light-tree':'native/oracle/scenes/light-tree/lamps-and-mesh-lights.json'}
DIAGNOSTIC_VARIABLES=['CODEBRIX_CYCLES_WGSL_LOAD','CODEBRIX_CYCLES_WEBGPU_ADAPTER','CODEBRIX_CYCLES_WEBGPU_ALLOW_NONCOMPLIANT',
 'CODEBRIX_CYCLES_DXC_MAX_SHADER_MODEL','CODEBRIX_CYCLES_WEBGPU_INSTANCE_FLAGS','CODEBRIX_CYCLES_WEBGPU_LOG','CODEBRIX_CYCLES_DEBUG_OUTPUT','CODEBRIX_CYCLES_DRIVER_WORKAROUNDS']
# Never set to make a job pass (docs/TESTING-LINUX-ARM64.md, section 4).
# Native build: system headers and libraries may come only from the toolchain's own C/C++
# runtime packages (the cross SDK's counterpart), never from a system copy of a dependency.
SYSTEM_HEADER_PACKAGES={'libc6-dev','linux-libc-dev','libstdc++-14-dev','libgcc-14-dev'}
SYSTEM_LIBRARIES={'ld-linux-aarch64.so.1','libc.so.6','libm.so.6','libdl.so.2','libpthread.so.0','librt.so.1','libstdc++.so.6','libgcc_s.so.1'}
def report_passed(row):
 """A device gate report's verdict: its 'passed' flag, or for the reports without one
 (atomic_film's CPU contract) every '*Passed' flag and nested 'passed' flag it carries."""
 if 'passed' in row:return bool(row['passed'])
 flags=[v for k,v in row.items() if k.endswith('Passed') and isinstance(v,bool)]+[v['passed'] for v in row.values() if isinstance(v,dict) and 'passed' in v]
 return bool(flags) and all(flags)
PINNED_VARIABLES=['OCIO','CODEBRIX_CYCLES_PIPELINE_TIER','CODEBRIX_CYCLES_PIPELINE_BUDGET_MS','CODEBRIX_CYCLES_PIPELINE_BUDGET_ACTION','CODEBRIX_CYCLES_SHADER_SPECIALIZATION']


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 p.add_argument('--output',type=Path,required=True);p.add_argument('--references',type=Path,required=True)
 p.add_argument('--device-gates',action='store_true',help='Native only: also run every built device gate executable and record its own verdict')
 # Cold driver compiles of the Cycles kernels can exceed an hour per GPU step (the Raspberry
 # Pi 5's Mesa V3DV, docs/TESTING-LINUX-ARM64.md, L2); the limit never changes a result.
 p.add_argument('--step-timeout',type=int,default=3600,help='Seconds allowed for each GPU step (default 3600)')
 a=p.parse_args()
 if a.device_gates and not NATIVE:p.error('--device-gates runs only natively on ARM64')
 host=BASE/'host';build=host/'build';prefix=BASE/'dependencies/install';out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 state=dict(complete=False,phase='binary-review',rid='linux-arm64',native=NATIVE,stepTimeoutSeconds=a.step_timeout,emulatedCpu=not NATIVE,actualArm64GpuProven=False);save(out/'status.json',state)
 results={};comparisons=[];commands=[]
 env=dict(os.environ);env.pop('LD_LIBRARY_PATH',None)
 for name in PINNED_VARIABLES:env.pop(name,None)
 runtime=PACKAGE/'runtimes/linux-arm64/native/libcodebrix_wgpu.so'
 if NATIVE:
  # The system loader and binutils of the ARM64 machine; no emulator.
  env['LD_LIBRARY_PATH']=':'.join(map(str,[build/'lib',prefix/'lib',runtime.parent]));runner=[]
  loader_program=Path('/lib/ld-linux-aarch64.so.1');readelf,nm='readelf','nm'
  system=[Path('/usr/lib/aarch64-linux-gnu').resolve(),Path('/lib/aarch64-linux-gnu').resolve()]
 else:
  search=':'.join(map(str,[build/'lib',prefix/'lib',runtime.parent,SDK/'lib']))
  runner=[str(QEMU),'-L',str(SDK),'-E','LD_LIBRARY_PATH='+search]
  loader_program=SDK/'lib/ld-linux-aarch64.so.1';readelf,nm='aarch64-linux-gnu-readelf','aarch64-linux-gnu-nm';system=[SDK]
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
  # Natively, Debian's /usr/include/<triple>/asm resolves into /usr/lib/linux/uapi (linux-libc-dev).
  allowed=[REPO,PACKAGE,*([Path('/usr/include'),Path('/usr/lib/gcc/aarch64-linux-gnu'),Path('/usr/lib/linux/uapi')] if NATIVE else [SDK,Path('/usr/lib/gcc-cross/aarch64-linux-gnu')])]
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
  packages={}
  if NATIVE:
   system_headers=sorted(h for h in headers if not any(Path(h).is_relative_to(root) for root in [REPO,PACKAGE]))
   owners=subprocess.run(['dpkg','-S',*system_headers],capture_output=True,text=True).stdout
   for line in owners.splitlines():
    package,path=line.split(': ',1);packages.setdefault(package.split(':')[0],set()).add(path)
   unowned=[h for h in system_headers if not any(h in paths for paths in packages.values())]
   assert not unowned,'System headers of no package: '+str(unowned[:5])
   assert set(packages)<=SYSTEM_HEADER_PACKAGES,'System headers from a non-toolchain package: '+str(sorted(set(packages)-SYSTEM_HEADER_PACKAGES))
   packages={name:len(paths) for name,paths in packages.items()}
  files=sorted({Path(path).resolve() for path in pins if Path(path).suffix!='.a'})
  elf=[]
  for path in files:
   data=path.read_bytes();assert data[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0]==183,path
   offset=struct.unpack_from('<Q',data,32)[0];size,count=struct.unpack_from('<HH',data,54);loads=[]
   for i in range(count):
    kind,flags,position,address,physical,file_size,memory_size,alignment=struct.unpack_from('<IIQQQQQQ',data,offset+size*i)
    if kind==1:loads.append(dict(alignment=alignment,offset=position,address=address));assert alignment>=4096 and position%alignment==address%alignment
   assert loads
   dynamic=subprocess.check_output([readelf,'-d','-V',str(path)],text=True)
   elf.append(dict(path=str(path),sha256=sha(path),needed=re.findall(r'\(NEEDED\).*?\[(.*?)\]',dynamic),loadSegments=loads,dynamicAndVersions=dynamic))
  library=build/'lib/libcodebrix_cycles_host.so'
  exported=subprocess.check_output([nm,'-D','--defined-only',str(library)],text=True)
  actual={line.split()[-1].split('@')[0] for line in exported.splitlines()}-{'CBC_HOST_EXPERIMENT_1'}
  expected=set(re.findall(r'\b(cbc_host_\w+)\(', (REPO/'native/host/cycles_host.h').read_text()));assert actual==expected and len(actual)==48
  for kind in ['session','session64','image']:
   link=(build/('codebrix_host_'+kind+'_reference.map')).read_text()
   assert 'CMakeFiles/codebrix_host_reference_color.dir/src/util/colorspace.cpp.o' in link
   assert not re.search(r'libcycles_util\.a\([^)]*colorspace',link)
  loader=[]
  for path in [library,build/'bin/codebrix_host_cpu_test',*[build/'bin'/('codebrix_host_'+kind+'_reference') for kind in ['session','session64','image']]]:
   output=call([*runner,loader_program,'--list',path],'loader-'+path.name,120)
   assert 'not found' not in output
   resolved=[]
   for name in re.findall(r'=>\s+(/\S+)',output):
    dependency_path=Path(name).resolve();assert any(dependency_path.is_relative_to(root) for root in [prefix,build,runtime.parent,*system]),dependency_path
    if NATIVE and any(dependency_path.is_relative_to(root) for root in system):assert Path(name).name in SYSTEM_LIBRARIES,'System library outside the C/C++ runtime: '+name
    pins[str(dependency_path)]=sha(dependency_path);resolved.append(dict(path=str(dependency_path),sha256=sha(dependency_path)))
   assert resolved;loader.append(dict(path=str(path),resolved=resolved,output=output))
  save(out/'binary-review.json',dict(passed=True,compiledSources=compiled,compilerHeaders=headers,systemHeaderPackages=packages,staticArchives=archives,linkCommands=links,elf=elf,exports=sorted(actual),loaderClosure=loader,
   scope='Compiler/header/archive inputs, ARM64 ELF and '+('the native system-loader closure. System GCC 14/glibc; no portable release-baseline claim.' if NATIVE else
    'emulated target-loader closure. External installed GCC/Linux SDK; no native ARM64 GPU or portable release-baseline claim.')))
  save(out/'invocation.json',dict(sourceSha256=sources,binarySha256=pins,buildInvocationSha256=sha(host/'invocation.json'),qemuSha256=None if NATIVE else sha(QEMU)))
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
  coverage={}
  if NATIVE:
   cpu,cpu64=out/'cpu',out/'cpu64'
   def folder(name):
    path=out/name;path.mkdir(parents=True,exist_ok=False);return path
   # WebGPU lifecycle through CBC_HOST_GPU, with the comparisons of host_gate.py.
   for mode,count in [('gpu',6),('gpu-exit',5)]:
    target=folder(mode)
    rows=report('codebrix_host_gpu_test',[target/'run',*(['--exit-live'] if mode=='gpu-exit' else [])],mode,a.step_timeout)
    if mode=='gpu':assert rows[0]['explicitWebGpu'] and rows[0]['repeatStrictCriterionPassed'] and rows[0]['repeatChecks']==3,rows
    for i in range(count):
     reference=cpu64/'run.1.rgba.bin' if i==3 else cpu/f'run.{i if i<3 else 1}.rgba.bin'
     # run.3, the 64-sample film: practical criterion required, strict differences recorded (host_gate.py).
     compare(target/f'run.{i}.rgba.bin',reference,f'{mode} vs same-target CPU Session reference',strict=i!=3)
   abi={}
   for mode in ['cpu','gpu']:
    target=folder('abi-'+mode)
    row=report('codebrix_host_abi_test',[target/'run',mode],'abi-'+mode,a.step_timeout)[0]
    assert re.fullmatch(r'1\.0\.\d{8}\.\d+',row['libraryVersion']) and row['cyclesRevision']==REVISION
    binary_caps=json.loads((target/'run.capabilities-binary.json').read_text());host_caps=json.loads((target/'run.capabilities-host.json').read_text())
    assert binary_caps['abiVersion']==1 and binary_caps['cyclesRevision']==REVISION and 'host' not in binary_caps
    assert binary_caps['backends']==dict(compiled=['cpu','webgpu'],gpu='webgpu',gpuFallbackToCpu=False),binary_caps['backends']
    assert binary_caps['imageInput']['formats']==['float','float4','half','half4','byte','byte4','ushort','ushort4']
    assert host_caps['host']['backend']==('cpu' if mode=='cpu' else 'webgpu')
    assert host_caps['host']['deviceReport']['backend']==host_caps['host']['backend']
    assert row['lightTypes']==5 and row['cancelledByCallback'] and row['refusedReentry']==row['progressEvents']+row['statusEvents']
    if mode=='cpu':
     assert all(row[k] for k in ['lightDeletionRestores','graphRebuildRestores','motionRemovalRestores','imageUpdateRestores'])
     assert row['litRenders']==4 and row['projections']==2 and row['errorEvents']==0
     assert all(row[k]==0 for k in ['depthStatus','normalStatus','aovStatus','dataPassRender','lightPassRender','diffuseDirectStatus','backgroundStatus'])
     passes=json.loads((target/'run.passes.json').read_text())['passes']
     assert [(q['name'],q['components'],q['written']) for q in passes]==[('Combined',4,True),('Depth',1,True),('Normal',3,True),('Mask',1,True)]
    else:
     assert row['litRenders']==4 and row['errorEvents']==1 and row['renderAfterDeviceFailure']==0
     state['adapterReport']=host_caps['host']['deviceReport'];adapter=host_caps['host']['deviceReport']['webgpu']
     # A hardware Vulkan adapter: the host never picks llvmpipe, and a layered adapter is not evidence.
     assert adapter['backendType']==VULKAN,adapter
     state['deviceDiagnostics']=adapter.get('diagnostics',[]);state['layeredOn']=adapter.get('layeredOn')
    abi[mode]=target
   for name in ['base','object-motion','deforming']:
    compare(abi['gpu']/f'run.{name}.rgba.bin',abi['cpu']/f'run.{name}.rgba.bin','ABI GPU vs CPU')
   for name in ['light-pointlight','light-spotlight','light-arealight','light-sunlight']:
    compare(abi['gpu']/f'run.{name}.rgba.bin',abi['cpu']/f'run.{name}.rgba.bin','ABI GPU vs CPU light film',strict=False)
   for mode in ['cpu','gpu']:
    row=report('codebrix_host_example_test',[mode],'example-'+mode,a.step_timeout)[0]
    assert row['backend']==(2 if mode=='cpu' else 1) and row['progressEvents']>0
    assert row['depthStatus']==(0 if mode=='cpu' else 1)
   # Driver coverage (windows_host_gate.py): CPU and GPU film of each scene, compare.py with the CPU film as reference.
   oracle=BASE/'host/build/bin/codebrix_cycles_oracle';assert oracle.exists(),'Build the oracle runner: build_cycles_linux_arm64.py --device-set'
   pins[str(oracle)]=sha(oracle)
   for kind,scene in DRIVER_COVERAGE.items():
    target=folder('driver-'+kind)
    for backend,label in [('cpu','cpu-arm64-linux'),('webgpu','wgsl-linux-arm64-vulkan')]:
     try:call([oracle,'--scene',REPO/scene,'--backend',backend,'--label',label,'--output',target/label,'--threads','4'],f'driver-{kind}-{backend}',a.step_timeout)
     except (subprocess.CalledProcessError,subprocess.TimeoutExpired):
      if backend=='cpu':raise
      coverage[kind]=dict(scene=scene,passed=False,error=(out/f'driver-{kind}-{backend}.stderr.txt').read_text().strip()[-400:])
    if kind in coverage:continue
    compared=subprocess.run([sys.executable,'-I',REPO/'native/oracle/compare.py',target/'cpu-arm64-linux',target/'wgsl-linux-arm64-vulkan','--json',target/'compare.json'],
     cwd=REPO,capture_output=True,text=True,timeout=600)
    (out/f'driver-{kind}-compare.stdout.txt').write_text(compared.stdout+compared.stderr)
    assert (target/'compare.json').exists(),'compare.py wrote no report: '+(compared.stdout+compared.stderr)[-400:]
    report_=json.loads((target/'compare.json').read_text());pair=report_['pairs'][0]
    coverage[kind]=dict(scene=scene,passed=bool(report_['passed']),maximumAbsoluteError=pair['maxAbsoluteDifference'],
     practicalFailures=pair['practicalFailures'],strictDifferences=pair['strictDifferences'])
   results['driverCoverage']=coverage;save(out/'results.json',results)
   state['actualArm64GpuProven']=True
  linux=a.references.resolve();assert json.loads((linux/'status.json').read_text())['passed'];old=json.loads((linux/'invocation.json').read_text())
  # The Linux run recorded absolute paths of its own machine; verify them relative to the (possibly copied) directory.
  root=os.path.commonpath(list(old['referenceSha256']))
  assert all(sha(linux/os.path.relpath(path,root))==digest for path,digest in old['referenceSha256'].items()),'The Linux reference films changed'
  for name in ['native/tests/host/session_reference.cpp','native/tests/host/image_reference.cpp','native/tests/host/image_inputs.h']:assert sha(REPO/name)==old['sourceSha256'][name]
  for i in range(3):compare(out/'cpu'/f'run.{i}.rgba.bin',linux/'cpu'/f'run.{i}.rgba.bin','cross-platform CPU lifecycle',strict=True)
  for variant in ['a','b','c']:
   for i in range(260):compare(refs/variant/'output'/f'case.{i}.rgba.bin',linux/'references'/variant/'output'/f'case.{i}.rgba.bin','cross-platform original CPU images')
  if a.device_gates:
   # Each gate's own JSON verdict (GPU against its in-process CPU reference). On the Pi that
   # reference runs glibc with the device's own scene, so it is the judge (docs/TESTING-LINUX-ARM64.md, section 5).
   gates={}
   for executable in sorted(build.glob('bin/codebrix_cycles_*_gate')):
    name=executable.name[len('codebrix_cycles_'):-len('_gate')];target=out/('device-gate-'+name);target.mkdir()
    # atomic_film is a CPU contract test and takes no output prefix.
    arguments=[] if name=='atomic_film' else [target/'run']
    try:
     text=call([executable,*arguments],'device-gate-'+name,a.step_timeout)
     rows=[json.loads(line) for line in text.splitlines() if line.strip().startswith('{')]
     note=next((f.read_text().strip() for f in [target/'run.library.txt',target/'run/library.txt'] if f.exists()),None)
     gates[name]=dict(exit=0,passed=bool(rows) and all(report_passed(r) for r in rows) and all(r.get('cyclesAllocationBytesAfterCleanup',0)==0 for r in rows),
      loadedLibrary=note,loadedPinnedRuntime=None if note is None else str(runtime) in note)
    except (subprocess.CalledProcessError,subprocess.TimeoutExpired) as error:
     gates[name]=dict(exit=getattr(error,'returncode','timeout'),passed=False,stderrTail=(out/f'device-gate-{name}.stderr.txt').read_text()[-800:])
    state['deviceGates']=gates;save(out/'status.json',state)
  assert inputs()==sources and all(sha(Path(path))==digest for path,digest in pins.items())
  failures=[row for row in comparisons if row['practicalFailures'] or (row['strictRequired'] and row['strictDifferences']) or (row['bitwiseRequired'] and not row['bitwiseEqual'])]
  coverage_failures=[kind for kind,row in coverage.items() if not row['passed']]
  diagnostic=sorted(set(state.get('deviceDiagnostics',[]))|{n+'='+env[n] for n in DIAGNOSTIC_VARIABLES if env.get(n)})
  gpu=[row for row in comparisons if not row['bitwiseRequired'] and 'cross-platform' not in row['kind']]
  state.update(driverCoverageFailures=coverage_failures,diagnosticRun=bool(diagnostic),diagnostics=diagnostic,
   gpuPracticalFailures=sum(row['practicalFailures'] for row in gpu),gpuMaximumAbsoluteError=max((row['maximumAbsoluteError'] for row in gpu),default=None),
   deviceGateFailures=sorted(k for k,v in state.get('deviceGates',{}).items() if not v['passed']))
  state.update(complete=True,phase='complete',passed=not failures and not coverage_failures and not state['deviceGateFailures'] and not diagnostic and not state.get('layeredOn'),filmsCompared=len(comparisons),failedFilms=len(failures),
   practicalFailures=sum(row['practicalFailures'] for row in comparisons),requiredStrictDifferences=sum(row['strictDifferences'] for row in comparisons if row['strictRequired']),
   strictImageDiagnostics=sum(row['strictDifferences'] for row in comparisons if not row['strictRequired']),
   sameTargetBitwiseFailures=sum(row['bitwiseRequired'] and not row['bitwiseEqual'] for row in comparisons),inputsUnchanged=True)
  print(json.dumps({k:v for k,v in state.items() if k!='adapterReport'}),flush=True)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(out/'comparisons.json',comparisons);save(out/'status.json',state)
 return 0 if state['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
