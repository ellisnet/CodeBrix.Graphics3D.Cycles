#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Review the osx-x64 host build and run the Linux host-gate list on an Intel Mac.

    python3 native/tools/macos_x64_host_gate.py --output native/work/macos-x64-validation-<n> \\
        [--references <passed Linux host_gate.py output>] [--device-gates]

The gate list is host_gate.py's: independent native Session references, C
lifecycle and process exit, concurrent lifetimes, the thin .NET wrapper, the
WebGPU lifecycle (CBC_HOST_GPU), the ABI additions on CPU and GPU, and the
example consumer, with the same comparisons and tolerances. Same-host CPU films
must be bitwise identical; GPU films must pass the practical criterion
5e-5 + 2e-4*abs(ref) and, where the Linux gate requires it, the strict criterion
1e-7 + 1e-7*abs(ref). Disagreements are findings, never adjusted.

The two OIIO file tests (image reference, color reload with PNG files) do not
apply: this product reads no image files, as on osx-arm64. With --references,
the CPU films are also compared with a passed Linux x64 host gate, with the
cross-platform rules of linux_arm64_host_gate.py. --device-gates also runs the
built WGSL device gate executables and records each one's own pass flag; the
Linux drivers' extra coverage assertions are not reproduced. As in
windows_host_gate.py, the driver-coverage step renders one oracle scene per
known GPU-driver failure class on the CPU and the GPU (practical criterion
required), and a run with an opt-in diagnostic set never passes.
"""
import argparse,ctypes,hashlib,json,math,os,platform,re,struct,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'tools/dependencies'))
from macos_checks import linked,tool
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
BASE=REPO/'native/work/macos/osx-x64';FLOATS=32*24*4
REVISION=json.loads((REPO/'third_party/cycles-import.json').read_text())['revision']
RUNTIME_NAME='libcodebrix_wgpu.dylib'
RUNTIME_SHA256='840a106d54fe479c255ecb98d51cee192a9044c3d0411fc1535f9ff0b8abbb82'
DEPLOYMENT_TARGET='15.0'
CBC_UNSUPPORTED=2
# webgpu.h WGPUBackendType values, for the adapter report.
BACKEND_TYPES={0:'Undefined',1:'Null',2:'WebGPU',3:'D3D11',4:'D3D12',5:'Metal',6:'Vulkan',7:'OpenGL',8:'OpenGLES'}
# Device gate executables that take only an output prefix (as the Linux drivers call them).
DEVICE_GATES=['device','camera','closest','background','svm_world','shader_data','svm_surface','attribute','normal','image',
 'surface','microfacet_svm','microfacet_bounce','microfacet_render','render','atomic_film',
 'ao_render','ao_state','light_tree','mnee','mnee_render','shadow_catcher']
# One oracle scene per failure class found on a GPU driver (windows_host_gate.py;
# docs/TESTING-WINDOWS-ARM64.md, F1, F3 and F4; docs/TESTING-WINDOWS-X64.md, W1).
DRIVER_COVERAGE={'smooth-shading':'native/oracle/scenes/linux-gates/glass-render.json',
 'motion-normal-map':'native/oracle/scenes/linux-gates/deforming-motion-1.json',
 'glass-area-light':'native/oracle/scenes/starter/glass-and-area-light.json',
 'light-tree':'native/oracle/scenes/light-tree/lamps-and-mesh-lights.json'}
# Opt-in diagnostics (native/src/webgpu_device.h): a run with any of them set
# is diagnostic and never passes.
DIAGNOSTIC_VARIABLES=['CODEBRIX_CYCLES_WGSL_LOAD','CODEBRIX_CYCLES_WEBGPU_ADAPTER','CODEBRIX_CYCLES_WEBGPU_ALLOW_NONCOMPLIANT',
 'CODEBRIX_CYCLES_DXC_MAX_SHADER_MODEL','CODEBRIX_CYCLES_WEBGPU_INSTANCE_FLAGS','CODEBRIX_CYCLES_WEBGPU_LOG','CODEBRIX_CYCLES_DEBUG_OUTPUT','CODEBRIX_CYCLES_DRIVER_WORKAROUNDS']


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 p.add_argument('--output',type=Path,required=True);p.add_argument('--references',type=Path,help='A passed Linux x64 host_gate.py output directory')
 p.add_argument('--device-gates',action='store_true',help='Also run the WGSL device gate executables (diagnostic)')
 a=p.parse_args()
 if platform.system()!='Darwin' or platform.machine()!='x86_64':
  raise SystemExit(f'osx-x64: this gate runs only on an Intel Mac; this host is {platform.system()} {platform.machine()}')
 host=BASE/'host';build=host/'build';prefix=BASE/'dependencies/install';out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 binary=build/'bin';library=build/'lib/libcodebrix_cycles_host.dylib';runtime=build/'lib'/RUNTIME_NAME
 oracle=BASE/'oracle-build/codebrix_cycles_oracle'
 state=dict(complete=False,phase='binary-review',rid='osx-x64');save(out/'status.json',state)
 results={};comparisons=[];commands=[]
 env=dict(os.environ)
 for name in ['OCIO','DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','DYLD_INSERT_LIBRARIES','CYCLES_CPU_NO_AVX2',
  'CODEBRIX_CYCLES_SHADER_SPECIALIZATION','CODEBRIX_CYCLES_TEST_CRASH_PIPELINE']:env.pop(name,None)
 def call(command,name,timeout=900,extra=None):
  commands.append(dict(command=list(map(str,command)),environment=extra or {},timeoutSeconds=timeout));save(out/'commands.json',commands)
  state['phase']=name;save(out/'status.json',state)
  with (out/(name+'.stdout.txt')).open('w') as stdout,(out/(name+'.stderr.txt')).open('w') as stderr:
   subprocess.run(list(map(str,command)),cwd=REPO,stdout=stdout,stderr=stderr,check=True,timeout=timeout,env={**env,**(extra or {})})
  return (out/(name+'.stdout.txt')).read_text()
 def report(command,name,timeout=900,extra=None):
  rows=[json.loads(line) for line in call(command,name,timeout,extra).splitlines() if line.strip()]
  assert rows and all(row['passed'] for row in rows),rows;results[name]=rows;save(out/'results.json',results);return rows
 def compare(actual,ref,kind,bitwise=False,strict=True):
  x,y=actual.read_bytes(),ref.read_bytes();v,w=struct.unpack(f'<{FLOATS}f',x),struct.unpack(f'<{FLOATS}f',y)
  assert all(math.isfinite(f) for f in v+w),actual
  errors=[abs(i-j) for i,j in zip(v,w)]
  row=dict(film=str(actual),reference=str(ref),kind=kind,filmSha256=sha(actual),bitwiseEqual=x==y,bitwiseRequired=bitwise,strictRequired=strict,
   maximumAbsoluteError=max(errors),practicalFailures=sum(e>5e-5+2e-4*abs(b) for e,b in zip(errors,w)),
   strictDifferences=sum(e>1e-7+1e-7*abs(b) for e,b in zip(errors,w)))
  comparisons.append(row);save(out/'comparisons.json',comparisons)
 def folder(name):
  path=out/name;path.mkdir(parents=True,exist_ok=False);return path
 try:
  status=json.loads((host/'status.json').read_text());assert status['complete'] and status['passed'],'Build the host first: python3 tools/build_macos_x64.py'
  invocation=json.loads((host/'invocation.json').read_text())
  pins={str(build/name):digest for name,digest in status['artifacts'].items()}
  dependency=json.loads((prefix/'codebrix-osx-x64-build.json').read_text())
  pins.update({str(prefix/name):digest for name,digest in dependency['libraries'].items()})
  assert all(sha(Path(path))==digest for path,digest in pins.items()),'Build artifacts changed since the build recorded them'
  assert sha(runtime)==RUNTIME_SHA256==invocation['runtimeSha256'],'The runtime beside the host is not the shipped osx-x64 binary'
  assert oracle.is_file(),'Build the oracle runner first: python3 tools/build_macos_x64.py';pins[str(oracle)]=sha(oracle)

  # Binary review: exports, architecture, minimum OS, load closure, signatures.
  exported={line.split()[-1].lstrip('_') for line in tool('nm','-gU',str(library)).splitlines() if ' T ' in line}
  expected=set(re.findall(r'\b(cbc_host_\w+)\(',(REPO/'native/host/cycles_host.h').read_text()));assert exported==expected and len(exported)==48,sorted(exported^expected)
  review=[]
  reviewed=[library,runtime,*[binary/n for n in ['codebrix_host_cpu_test','codebrix_host_gpu_test','codebrix_host_abi_test','codebrix_host_session_reference']]]
  for path in reviewed:
   names=linked(path)
   for name in names:assert name.startswith(('@rpath/','/usr/lib/','/System/Library/')),(path,name)
   archs=tool('lipo','-archs',str(path)).split();assert archs==['x86_64'],(path,archs)
   minos=re.findall(r'minos (\S+)',tool('otool','-l',str(path)))
   if path!=runtime:assert minos and all(v==DEPLOYMENT_TARGET for v in minos),(path,minos)
   signature=subprocess.run(['codesign','-dv',str(path)],capture_output=True,text=True).stderr.splitlines()
   subprocess.run(['codesign','--verify',str(path)],check=True)
   review.append(dict(path=str(path),sha256=sha(path),architectures=archs,linked=names,minimumOs=minos,signature=signature,
    rpaths=re.findall(r'cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (\S+)',tool('otool','-l',str(path)))))
  assert '@rpath/'+RUNTIME_NAME in linked(library),'The host does not load the WebGPU runtime'
  host_frameworks=sorted(n for n in linked(library) if n.startswith('/System/'))
  assert not any('/Metal.framework/' in f for f in host_frameworks),'The host links Metal directly'
  runtime_frameworks=sorted(n for n in linked(runtime) if n.startswith('/System/'))
  codecs=[n for n in status['dependencyClosure'] if re.match(r'lib(jpeg|png|tiff|webp|gif|heif|raw|openjp|avif|jxl)',n)];assert not codecs,codecs
  for kind in ['session','session64']:
   link=(build/('codebrix_host_'+kind+'_reference.map')).read_text()
   assert 'codebrix_host_reference_color.dir' in link and 'colorspace.cpp.o' in link
  save(out/'binary-review.json',dict(passed=True,exports=sorted(exported),binaries=review,hostFrameworks=host_frameworks,runtimeFrameworks=runtime_frameworks,
   dependencyClosure=status['dependencyClosure'],overlayComparison=invocation.get('overlayComparison'),
   scope='osx-x64 exports, x86_64 Mach-O, minimum OS, load closure (system libraries, the reviewed prefix and the shipped runtime), signatures'))

  # Contract checks through the public API, without any test executable.
  api=ctypes.CDLL(str(library));config=(ctypes.c_uint32*8)(32,1,3,1,0,0,0,0);handle=ctypes.c_uint64(0)
  metal=api.cbc_host_create(config,ctypes.byref(handle))
  config[2]=2;assert api.cbc_host_create(config,ctypes.byref(handle))==0
  node=ctypes.c_uint64(0);shader=ctypes.c_uint64(0);assert api.cbc_host_create_node(handle,b'shader',ctypes.byref(shader))==0
  assert api.cbc_host_shader_node(handle,shader,b'image_texture',ctypes.byref(node))==0
  rejected=api.cbc_host_set_string(handle,node,b'filename',b'/tmp/texture.png');cleared=api.cbc_host_set_string(handle,node,b'filename',b'')
  assert api.cbc_host_destroy(handle)==0
  contract=dict(metalCreateStatus=metal,filenameSetStatus=rejected,emptyFilenameStatus=cleared)
  assert contract==dict(metalCreateStatus=CBC_UNSUPPORTED,filenameSetStatus=CBC_UNSUPPORTED,emptyFilenameStatus=0),contract
  results['contract']=contract;save(out/'results.json',results)

  cpu=folder('cpu');cpu64=folder('cpu64')
  for target,kind in [(cpu,'session'),(cpu64,'session64')]:
   rows=report([binary/('codebrix_host_'+kind+'_reference'),'CPU',target/'run'],kind+'-reference')
   assert rows[0]['actualSession'] and rows[0]['renders']==3 and rows[0]['device']=='CPU'
  reference_hashes={str(f):sha(f) for d in [cpu,cpu64] for f in d.rglob('*') if f.is_file()}
  for mode in ['cpu','cpu-exit']:
   target=folder(mode+'-consumer');report([binary/'codebrix_host_cpu_test',target/'run',*(['--exit-live'] if mode=='cpu-exit' else [])],mode)
   for i in range(3):compare(target/f'run.{i}.rgba.bin',cpu/f'run.{i}.rgba.bin','same-host CPU lifecycle',bitwise=True)

  stress_input=folder('stress-input');stress_output=folder('stress-output')
  for variant in ['a','b']:(stress_input/f'{variant}.ocio').write_bytes((REPO/'native/tests/host/configs'/f'{variant}.ocio').read_bytes())
  linux=a.references.resolve() if a.references else None
  if linux:
   # The Linux gate's decoded texture, so the films can also be compared with its references.
   (stress_input/'input.5.bin').write_bytes((linux/'references/a/output/input.5.bin').read_bytes())
  else:
   # Deterministic 7x5 byte4 texture, as on osx-arm64; Linux decodes a PNG here.
   (stress_input/'input.5.bin').write_bytes(bytes((37*i+11*(i//4))%256 for i in range(7*5*4)))
  rows=report([binary/'codebrix_host_concurrency_test',stress_input,stress_output],'concurrency',1200)
  assert all(rows[0][key]==value for key,value in dict(concurrentHostLifetimes=192,destroyDuringRender=12,racingReaders=48,
   guardedConfigurationRejections=256,hostLiveConfigurationRejections=192).items()),rows
  assert rows[0]['cancelledOutputUntouched']
  compare(stress_output/'after-invalid.rgba.bin',stress_output/'guarded.rgba.bin','same-host concurrent lifetime',bitwise=True)

  # Thin .NET wrapper: same films as the native Session, bit for bit.
  managed=out/'managed-bin'
  call(['dotnet','build',REPO/'tools/hosttests/HostLifecycle/HostLifecycle.csproj','-c','Release','--configfile',
   REPO/'tools/hosttests/NuGet.Config','-o',managed],'managed-build',900)
  pins[str(managed/'HostLifecycle.dll')]=sha(managed/'HostLifecycle.dll');pins[str(managed/'CodeBrix.Graphics3D.Cycles.dll')]=sha(managed/'CodeBrix.Graphics3D.Cycles.dll')
  target=out/'managed-lifecycle'
  rows=[json.loads(line) for line in call(['dotnet',managed/'HostLifecycle.dll',cpu,target],'managed-lifecycle',900,
   {'DYLD_LIBRARY_PATH':str(build/'lib')}).splitlines() if line.strip().startswith('{')]
  assert rows and rows[-1]['passed'] and rows[-1]['cpuReferenceBitwiseEqual'],rows;results['managed-lifecycle']=rows;save(out/'results.json',results)
  for i in range(3):compare(target/f'run.{i}.rgba.bin',cpu/f'run.{i}.rgba.bin','managed CPU lifecycle',bitwise=True)

  # WebGPU lifecycle through CBC_HOST_GPU (the shipped runtime's Metal route).
  for mode,count in [('gpu',6),('gpu-exit',5)]:
   target=folder(mode)
   rows=report([binary/'codebrix_host_gpu_test',target/'run',*(['--exit-live'] if mode=='gpu-exit' else [])],mode,1800)
   if mode=='gpu':assert rows[0]['explicitWebGpu'] and rows[0]['repeatStrictCriterionPassed'] and rows[0]['repeatChecks']==3,rows
   for i in range(count):
    reference=cpu64/'run.1.rgba.bin' if i==3 else cpu/f'run.{i if i<3 else 1}.rgba.bin'
    # run.3 (the 64-sample film against the session64 reference), as in host_gate.py: the
    # practical criterion is required and strict differences are recorded. Its atomic
    # accumulation order varies between runs, and one component (1742) sits one float step
    # from the strict bound (native/DEVICE.md, "Merge wave 7").
    compare(target/f'run.{i}.rgba.bin',reference,f'{mode} vs same-host CPU Session reference',strict=i!=3)

  # ABI additions on the CPU reference and through CBC_HOST_GPU.
  abi={}
  for mode in ['cpu','gpu']:
   target=folder('abi-'+mode)
   row=report([binary/'codebrix_host_abi_test',target/'run',mode],'abi-'+mode,1800)[0]
   assert re.fullmatch(r'1\.0\.\d{8}\.\d+',row['libraryVersion']) and row['cyclesRevision']==REVISION
   binary_caps=json.loads((target/'run.capabilities-binary.json').read_text());host_caps=json.loads((target/'run.capabilities-host.json').read_text())
   assert binary_caps['abiVersion']==1 and binary_caps['cyclesRevision']==REVISION and 'host' not in binary_caps
   assert binary_caps['backends']==dict(compiled=['cpu','webgpu'],gpu='webgpu',gpuFallbackToCpu=False),binary_caps['backends']
   assert binary_caps['imageInput']['formats']==['float','float4','half','half4','byte','byte4','ushort','ushort4']
   assert binary_caps['imageInput']['fileReading'] is False
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
    state['adapterReport']=host_caps['host']['deviceReport']
    adapter=host_caps['host']['deviceReport']['webgpu'];backend_type=adapter['backendType']
    state['adapterBackend']=BACKEND_TYPES.get(backend_type,backend_type)
    # The shipped runtime's macOS build has only wgpu's Metal backend.
    assert backend_type==5 and adapter['backendName']=='Metal',adapter
    # IEEE floating point requested from the runtime (docs/TESTING-MACOS-X64.md, M1).
    assert adapter.get('metalPreciseMath') is True,adapter
    state['metalPreciseMath']=True
    state['deviceDiagnostics']=adapter.get('diagnostics',[])
   abi[mode]=target
  for name in ['base','object-motion','deforming']:
   compare(abi['gpu']/f'run.{name}.rgba.bin',abi['cpu']/f'run.{name}.rgba.bin','ABI GPU vs CPU')
  # As on Linux: the four light films require the practical criterion; strict differences are recorded.
  for name in ['light-pointlight','light-spotlight','light-arealight','light-sunlight']:
   compare(abi['gpu']/f'run.{name}.rgba.bin',abi['cpu']/f'run.{name}.rgba.bin','ABI GPU vs CPU light film',strict=False)
  for mode in ['cpu','gpu']:
   row=report([binary/'codebrix_host_example_test',mode],'example-'+mode,1800)[0]
   assert row['backend']==(2 if mode=='cpu' else 1) and row['progressEvents']>0
   assert row['depthStatus']==(0 if mode=='cpu' else 1)

  # Driver coverage, as in windows_host_gate.py: one oracle scene per class of scene a
  # GPU driver has rendered wrongly or refused, rendered on the CPU and the GPU by the
  # oracle runner and compared with the practical criterion, which is required.
  coverage={}
  for kind,scene in DRIVER_COVERAGE.items():
   target=folder('driver-'+kind)
   # compare.py takes the CPU film as the reference by its label (cpu-*).
   for backend,label in [('cpu','cpu-macos'),('webgpu','wgsl-macos')]:
    try:call([oracle,'--scene',REPO/scene,'--backend',backend,'--label',label,'--output',target/label,'--threads','4'],
     f'driver-{kind}-{backend}',3600)
    except subprocess.CalledProcessError:
     # A GPU render that fails (a pipeline the driver rejects) is a coverage
     # failure of this class; the remaining classes still run.
     if backend=='cpu':raise
     coverage[kind]=dict(scene=scene,passed=False,error=(out/f'driver-{kind}-{backend}.stderr.txt').read_text().strip()[-400:])
   if kind in coverage:continue
   compared=subprocess.run([sys.executable,'-I',REPO/'native/oracle/compare.py',target/'cpu-macos',target/'wgsl-macos','--json',target/'compare.json'],
    cwd=REPO,capture_output=True,text=True,timeout=600)
   (out/f'driver-{kind}-compare.stdout.txt').write_text(compared.stdout+compared.stderr)
   assert (target/'compare.json').exists(),'compare.py wrote no report: '+(compared.stdout+compared.stderr)[-400:]
   report_=json.loads((target/'compare.json').read_text())
   pair=report_['pairs'][0]
   coverage[kind]=dict(scene=scene,passed=bool(report_['passed']),maximumAbsoluteError=pair['maxAbsoluteDifference'],
    practicalFailures=pair['practicalFailures'],strictDifferences=pair['strictDifferences'])
  results['driverCoverage']=coverage;save(out/'results.json',results)

  if linux:
   linux_status=json.loads((linux/'status.json').read_text());assert linux_status['passed'];old=json.loads((linux/'invocation.json').read_text())
   # A CPU-only Linux run (host_gate.py --cpu-only, e.g. in a container) writes the same CPU reference set.
   state['crossPlatformReference']=dict(directory=str(linux),cpuOnly=bool(linux_status.get('cpuOnly')))
   # The Linux run recorded absolute paths of its own machine; verify them relative to the copied directory.
   base=os.path.commonpath(list(old['referenceSha256']))
   assert all(sha(linux/os.path.relpath(path,base))==digest for path,digest in old['referenceSha256'].items()),'The Linux reference films changed'
   for name in ['native/tests/host/session_reference.cpp','native/tests/host/consumer.c','native/tests/host/concurrency_consumer.c']:
    if name in old['sourceSha256']:assert sha(REPO/name)==old['sourceSha256'][name],name
   # Cross-platform rules of linux_arm64_host_gate.py: lifecycle films strict, others practical.
   for i in range(3):compare(cpu/f'run.{i}.rgba.bin',linux/'cpu'/f'run.{i}.rgba.bin','cross-platform CPU lifecycle (Linux x64)',strict=True)
   for i in range(3):compare(cpu64/f'run.{i}.rgba.bin',linux/'cpu64'/f'run.{i}.rgba.bin','cross-platform CPU 64-sample lifecycle (Linux x64)',strict=False)
   for name in ['guarded','after-invalid']:
    compare(stress_output/(name+'.rgba.bin'),linux/'references/a/output/case.177.rgba.bin','cross-platform concurrent lifetime (Linux x64)',strict=False)

  if a.device_gates:
   gates={}
   for name in DEVICE_GATES:
    executable=binary/f'codebrix_cycles_{name}_gate';target=folder('device-gate-'+name)
    try:
     text=call([executable,target/'run'],'device-gate-'+name,1800)
     rows=[json.loads(line) for line in text.splitlines() if line.strip().startswith('{')]
     library_note=(target/'run.library.txt').read_text().strip() if (target/'run.library.txt').exists() else None
     gates[name]=dict(exit=0,passed=bool(rows) and all(r.get('passed') for r in rows),loadedLibrary=library_note)
    except (subprocess.CalledProcessError,subprocess.TimeoutExpired) as error:
     gates[name]=dict(exit=getattr(error,'returncode','timeout'),passed=False,stderrTail=(out/f'device-gate-{name}.stderr.txt').read_text()[-800:])
    state['deviceGates']=gates;save(out/'status.json',state)

  assert all(sha(Path(path))==digest for path,digest in {**pins,**reference_hashes}.items()),'Inputs changed during the gate'
  failures=[r for r in comparisons if r['practicalFailures'] or (r['strictRequired'] and r['strictDifferences']) or (r['bitwiseRequired'] and not r['bitwiseEqual'])]
  gpu=[r for r in comparisons if not r['bitwiseRequired'] and 'cross-platform' not in r['kind']]
  save(out/'invocation.json',dict(binarySha256=pins,referenceSha256=reference_hashes,buildInvocationSha256=sha(host/'invocation.json'),commands=commands))
  coverage_failures=[kind for kind,row in coverage.items() if not row['passed']]
  # A diagnostic run (any opt-in diagnostic in the environment or reported by the
  # device) is recorded but never passes.
  diagnostic=sorted(set(state.get('deviceDiagnostics',[]))|{n+'='+env[n] for n in DIAGNOSTIC_VARIABLES if env.get(n)})
  state.update(complete=True,phase='complete',passed=not failures and not coverage_failures and not diagnostic,
   driverCoverageFailures=coverage_failures,diagnosticRun=bool(diagnostic),diagnostics=diagnostic,filmsCompared=len(comparisons),failedFilms=len(failures),
   sameHostBitwiseFailures=sum(r['bitwiseRequired'] and not r['bitwiseEqual'] for r in comparisons),
   gpuPracticalFailures=sum(r['practicalFailures'] for r in gpu),gpuStrictDifferences=sum(r['strictDifferences'] for r in gpu),
   gpuMaximumAbsoluteError=max((r['maximumAbsoluteError'] for r in gpu),default=None),
   crossPlatformCompared=linux is not None,
   skipped=dict(reloadTest='loads PNG files by filename; this profile rejects texture filenames',
    imageReference='needs OIIO EXR/PNG plugins, which are disabled',managedImageModesAndColorReload='depend on the image reference films'),
   inputsUnchanged=True)
  print(json.dumps({k:v for k,v in state.items() if k!='adapterReport'}),flush=True)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(out/'comparisons.json',comparisons);save(out/'status.json',state)
 return 0 if state['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
