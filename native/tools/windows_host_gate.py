#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Review the Windows host build and run the Linux host-gate list on Windows.

    python -X utf8 native/tools/windows_host_gate.py --rid win-x64 --backend d3d12 \\
        --output native/work/windows-validation-<n> [--references <passed Linux host_gate.py output>] [--device-gates]
    python -X utf8 native/tools/windows_host_gate.py --rid win-arm64 --payload native/binaries/win-arm64 ...

The Windows sibling of macos_x64_host_gate.py, with its gate list, comparisons
and tolerances: independent native Session references, C lifecycle and process
exit, concurrent lifetimes, the thin .NET wrapper, the WebGPU lifecycle
(CBC_HOST_GPU), the ABI additions on CPU and GPU, and the example consumer.
Same-host CPU films must be bitwise identical; GPU films must pass the practical
criterion 5e-5 + 2e-4*abs(ref) and, where the Linux gate requires it, the strict
criterion 1e-7 + 1e-7*abs(ref). Disagreements are findings, never adjusted.

--backend selects the WebGPU backend the GPU steps use (CODEBRIX_CYCLES_WEBGPU_BACKEND):
d3d12 (the product default, with the DXC compiler beside the host) or vulkan.
The two OIIO file tests do not apply: this product reads no image files, as on
macOS. --payload runs the gate from a staged payload directory instead of the
build tree (an ARM64 device tests the cross-built win-arm64 payload; it builds
nothing). --device-gates also runs the WGSL device gate executables and records
each one's own pass flag.
"""
import argparse,ctypes,hashlib,json,math,os,platform,re,struct,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'tools'))
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):Path(path).write_text(json.dumps(value,indent=2,default=str)+'\n',newline='\n')
FLOATS=32*24*4
REVISION=json.loads((REPO/'third_party/cycles-import.json').read_text())['revision']
CBC_UNSUPPORTED=2
# webgpu.h WGPUBackendType values, for the adapter report.
BACKEND_TYPES={0:'Undefined',1:'Null',2:'WebGPU',3:'D3D11',4:'D3D12',5:'Metal',6:'Vulkan',7:'OpenGL',8:'OpenGLES'}
EXPECTED_BACKEND={'d3d12':4,'vulkan':6}
DEVICE_GATES=['device','camera','closest','background','svm_world','shader_data','svm_surface','attribute','normal','image',
 'surface','microfacet_svm','microfacet_bounce','microfacet_render','render','atomic_film']
HOST_MACHINE={'AMD64':'win-x64','ARM64':'win-arm64'}


def pe_exports(path):
 """Export names from a PE file's export directory (no external tool needed)."""
 data=Path(path).read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0];assert data[pe:pe+4]==b'PE\0\0',path
 sections=struct.unpack_from('<H',data,pe+6)[0];optional=struct.unpack_from('<H',data,pe+20)[0];opt=pe+24
 magic=struct.unpack_from('<H',data,opt)[0];directory=opt+(112 if magic==0x20b else 96)
 rva=struct.unpack_from('<I',data,directory)[0];table=[struct.unpack_from('<IIII',data,opt+optional+40*i+8) for i in range(sections)]
 def offset(r):
  for size,address,raw_size,raw in table:
   if address<=r<address+max(size,raw_size):return r-address+raw
  raise ValueError(r)
 if not rva:return []
 base=offset(rva);count,names=struct.unpack_from('<II',data,base+24)[0],struct.unpack_from('<I',data,base+32)[0]
 out=[]
 for i in range(count):
  name=offset(struct.unpack_from('<I',data,offset(names)+4*i)[0]);out.append(data[name:data.index(b'\0',name)].decode())
 return sorted(out)


def pe_machine(path):
 data=Path(path).read_bytes()[:4096];pe=struct.unpack_from('<I',data,0x3c)[0]
 return {0x8664:'AMD64',0xaa64:'ARM64'}.get(struct.unpack_from('<H',data,pe+4)[0],'other')


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 p.add_argument('--rid',choices=['win-x64','win-arm64'],default=HOST_MACHINE.get(platform.machine(),'win-x64'))
 p.add_argument('--backend',choices=['d3d12','vulkan'],default='d3d12')
 p.add_argument('--output',type=Path,required=True);p.add_argument('--references',type=Path,help='A passed Linux x64 host_gate.py output directory')
 p.add_argument('--payload',type=Path,help='Run from a staged payload directory (native/binaries/<rid>) instead of the build tree')
 p.add_argument('--device-gates',action='store_true',help='Also run the WGSL device gate executables (diagnostic)')
 a=p.parse_args()
 if platform.system()!='Windows':raise SystemExit('This gate runs only on Windows')
 if not sys.flags.utf8_mode:raise SystemExit('Run the gate in Python UTF-8 mode (python -X utf8 ...)')
 if HOST_MACHINE.get(platform.machine())!=a.rid:raise SystemExit(f'{a.rid} binaries run on a {a.rid} Windows machine; this host is {platform.machine()}')
 host=REPO/'native/work/windows'/a.rid/'host';out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 if a.payload:
  # The payload carries the host, its DLLs, DXC and the test executables, but not
  # the WebGPU runtime (its own package). Assemble one run folder from the verified
  # payload and the pinned runtime from the NuGet cache, as an application has them.
  payload=a.payload.resolve();provenance=json.loads((payload/'provenance.json').read_text())
  assert provenance['rid']==a.rid,(provenance['rid'],a.rid)
  for name,digest in provenance['approvedPayload'].items():assert sha(payload/name)==digest,'Payload file changed: '+name
  sys.path.insert(0,str(REPO/'research'));from native_runner import PACKAGE
  runtime=PACKAGE/provenance['webgpuRuntime']['path']
  assert runtime.is_file() and sha(runtime)==provenance['webgpuRuntime']['sha256'],f'Restore {provenance["webgpuRuntime"]["id"]} {provenance["webgpuRuntime"]["version"]} first (dotnet restore research/GpuProbe/GpuProbe.csproj)'
  binary=tests=out/'payload-run';binary.mkdir()
  for name in provenance['approvedPayload']:
   if name.startswith(('runtime/','tests/')):(binary/Path(name).name).write_bytes((payload/name).read_bytes())
  (binary/runtime.name).write_bytes(runtime.read_bytes())
  pins={str(binary/Path(name).name):digest for name,digest in provenance['approvedPayload'].items() if name.startswith(('runtime/','tests/'))}
  pins[str(binary/runtime.name)]=provenance['webgpuRuntime']['sha256']
 else:
  binary=tests=host/'build/bin';status=json.loads((host/'status.json').read_text())
  assert status['complete'] and status['passed'],'Build the host first: python -X utf8 native/tools/build_cycles_windows.py --rid '+a.rid
  pins={str(host/'build'/name):digest for name,digest in status['artifacts'].items()}
 library=binary/'codebrix_cycles_host.dll'
 state=dict(complete=False,phase='binary-review',rid=a.rid,backend=a.backend,source='payload' if a.payload else 'build');save(out/'status.json',state)
 results={};comparisons=[];commands=[]
 env=dict(os.environ)
 for name in ['OCIO','CYCLES_CPU_NO_AVX2','CODEBRIX_CYCLES_SHADER_SPECIALIZATION','CODEBRIX_CYCLES_TEST_CRASH_PIPELINE','CODEBRIX_CYCLES_DXC_PATH']:env.pop(name,None)
 env['CODEBRIX_CYCLES_WEBGPU_BACKEND']=a.backend
 # The executables load the host and every DLL it needs from their own folder
 # (and the payload's runtime folder when they live in tests/).
 env['PATH']=os.pathsep.join([str(binary),str(tests),env.get('PATH','')])
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
 def exe(name):return tests/(name+'.exe')
 try:
  assert all(sha(Path(path))==digest for path,digest in pins.items()),'Binaries changed since they were recorded'
  # Binary review: exports, machine type, the DLLs the host needs.
  exported=set(pe_exports(library))
  expected=set(re.findall(r'\b(cbc_host_\w+)\(',(REPO/'native/host/cycles_host.h').read_text()));assert exported==expected and len(exported)==48,sorted(exported^expected)
  want={'win-x64':'AMD64','win-arm64':'ARM64'}[a.rid]
  review=[dict(path=str(f),sha256=sha(f),machine=pe_machine(f)) for f in sorted(binary.glob('*.dll'))+sorted(tests.glob('codebrix_*.exe'))]
  wrong=[r['path'] for r in review if r['machine']!=want];assert not wrong,wrong
  assert (binary/'codebrix_wgpu.dll').exists() and (binary/'dxcompiler.dll').exists(),'The WebGPU runtime or DXC is not beside the host'
  codecs=[r['path'] for r in review if re.match(r'(lib)?(jpeg|png|webp|gif|heif|raw|openjp|avif|jxl)',Path(r['path']).name,re.I)];assert not codecs,codecs
  if not a.payload:
   # The references link the original colorspace object (codebrix_host_reference_color),
   # not the overlaid one. lld-link's map names objects without their directory, so the
   # executable's link inputs in build.ninja show which object it is.
   ninja=(host/'build/build.ninja').read_text(errors='replace')
   for kind in ['session','session64']:
    line=next((l for l in ninja.splitlines() if l.startswith(rf'build bin\codebrix_host_{kind}_reference.exe:')),'')
    assert r'codebrix_host_reference_color.dir\src\util\colorspace.cpp.obj' in line,kind
    link=(host/'build'/('codebrix_host_'+kind+'_reference.map')).read_text(errors='replace')
    assert 'colorspace.cpp.obj' in link,kind
  save(out/'binary-review.json',dict(passed=True,exports=sorted(exported),binaries=review,
   scope=a.rid+' exports, PE machine type, runtime and DXC beside the host, no image codec DLL'))

  # Contract checks through the public API, without any test executable.
  api=ctypes.CDLL(str(library));config=(ctypes.c_uint32*8)(32,1,3,1,0,0,0,0);handle=ctypes.c_uint64(0)
  metal=api.cbc_host_create(config,ctypes.byref(handle))
  config[2]=2;assert api.cbc_host_create(config,ctypes.byref(handle))==0
  node=ctypes.c_uint64(0);shader=ctypes.c_uint64(0);assert api.cbc_host_create_node(handle,b'shader',ctypes.byref(shader))==0
  assert api.cbc_host_shader_node(handle,shader,b'image_texture',ctypes.byref(node))==0
  rejected=api.cbc_host_set_string(handle,node,b'filename',b'C:\\texture.png');cleared=api.cbc_host_set_string(handle,node,b'filename',b'')
  assert api.cbc_host_destroy(handle)==0
  contract=dict(metalCreateStatus=metal,filenameSetStatus=rejected,emptyFilenameStatus=cleared)
  assert contract==dict(metalCreateStatus=CBC_UNSUPPORTED,filenameSetStatus=CBC_UNSUPPORTED,emptyFilenameStatus=0),contract
  results['contract']=contract;save(out/'results.json',results)

  cpu=folder('cpu');cpu64=folder('cpu64')
  for target,kind in [(cpu,'session'),(cpu64,'session64')]:
   rows=report([exe('codebrix_host_'+kind+'_reference'),'CPU',target/'run'],kind+'-reference')
   assert rows[0]['actualSession'] and rows[0]['renders']==3 and rows[0]['device']=='CPU'
  reference_hashes={str(f):sha(f) for d in [cpu,cpu64] for f in d.rglob('*') if f.is_file()}
  for mode in ['cpu','cpu-exit']:
   target=folder(mode+'-consumer');report([exe('codebrix_host_cpu_test'),target/'run',*(['--exit-live'] if mode=='cpu-exit' else [])],mode)
   for i in range(3):compare(target/f'run.{i}.rgba.bin',cpu/f'run.{i}.rgba.bin','same-host CPU lifecycle',bitwise=True)

  stress_input=folder('stress-input');stress_output=folder('stress-output')
  for variant in ['a','b']:(stress_input/f'{variant}.ocio').write_bytes((REPO/'native/tests/host/configs'/f'{variant}.ocio').read_bytes())
  linux=a.references.resolve() if a.references else None
  if linux:(stress_input/'input.5.bin').write_bytes((linux/'references/a/output/input.5.bin').read_bytes())
  else:(stress_input/'input.5.bin').write_bytes(bytes((37*i+11*(i//4))%256 for i in range(7*5*4)))
  rows=report([exe('codebrix_host_concurrency_test'),stress_input,stress_output],'concurrency',1200)
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
  rows=[json.loads(line) for line in call(['dotnet',managed/'HostLifecycle.dll',cpu,target],'managed-lifecycle',900).splitlines() if line.strip().startswith('{')]
  assert rows and rows[-1]['passed'] and rows[-1]['cpuReferenceBitwiseEqual'],rows;results['managed-lifecycle']=rows;save(out/'results.json',results)
  for i in range(3):compare(target/f'run.{i}.rgba.bin',cpu/f'run.{i}.rgba.bin','managed CPU lifecycle',bitwise=True)

  # WebGPU lifecycle through CBC_HOST_GPU on the selected backend.
  for mode,count in [('gpu',6),('gpu-exit',5)]:
   target=folder(mode)
   rows=report([exe('codebrix_host_gpu_test'),target/'run',*(['--exit-live'] if mode=='gpu-exit' else [])],mode,3600)
   if mode=='gpu':assert rows[0]['explicitWebGpu'] and rows[0]['repeatStrictCriterionPassed'] and rows[0]['repeatChecks']==3,rows
   for i in range(count):
    reference=cpu64/'run.1.rgba.bin' if i==3 else cpu/f'run.{i if i<3 else 1}.rgba.bin'
    compare(target/f'run.{i}.rgba.bin',reference,f'{mode} vs same-host CPU Session reference')

  # ABI additions on the CPU reference and through CBC_HOST_GPU.
  abi={}
  for mode in ['cpu','gpu']:
   target=folder('abi-'+mode)
   row=report([exe('codebrix_host_abi_test'),target/'run',mode],'abi-'+mode,3600)[0]
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
    assert backend_type==EXPECTED_BACKEND[a.backend] and adapter['backendName']==BACKEND_TYPES[backend_type],(a.backend,adapter)
    # Direct3D 12 compiles with the DXC beside the host; Vulkan loads none.
    assert (adapter['dxcPath'] is not None)==(a.backend=='d3d12'),adapter
    if adapter['dxcPath']:assert Path(adapter['dxcPath']).parent==library.parent,adapter
   abi[mode]=target
  for name in ['base','object-motion','deforming']:
   compare(abi['gpu']/f'run.{name}.rgba.bin',abi['cpu']/f'run.{name}.rgba.bin','ABI GPU vs CPU')
  # As on Linux: the four light films require the practical criterion; strict differences are recorded.
  for name in ['light-pointlight','light-spotlight','light-arealight','light-sunlight']:
   compare(abi['gpu']/f'run.{name}.rgba.bin',abi['cpu']/f'run.{name}.rgba.bin','ABI GPU vs CPU light film',strict=False)
  for mode in ['cpu','gpu']:
   row=report([exe('codebrix_host_example_test'),mode],'example-'+mode,3600)[0]
   assert row['backend']==(2 if mode=='cpu' else 1) and row['progressEvents']>0
   assert row['depthStatus']==(0 if mode=='cpu' else 1)

  if linux:
   assert json.loads((linux/'status.json').read_text())['passed'];old=json.loads((linux/'invocation.json').read_text())
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
    executable=exe(f'codebrix_cycles_{name}_gate');target=folder('device-gate-'+name)
    try:
     text=call([executable,target/'run'],'device-gate-'+name,3600)
     rows=[json.loads(line) for line in text.splitlines() if line.strip().startswith('{')]
     library_note=(target/'run.library.txt').read_text().strip() if (target/'run.library.txt').exists() else None
     gates[name]=dict(exit=0,passed=bool(rows) and all(r.get('passed') for r in rows),loadedLibrary=library_note)
    except (subprocess.CalledProcessError,subprocess.TimeoutExpired) as error:
     gates[name]=dict(exit=getattr(error,'returncode','timeout'),passed=False,stderrTail=(out/f'device-gate-{name}.stderr.txt').read_text()[-800:])
    state['deviceGates']=gates;save(out/'status.json',state)

  assert all(sha(Path(path))==digest for path,digest in {**pins,**reference_hashes}.items()),'Inputs changed during the gate'
  failures=[r for r in comparisons if r['practicalFailures'] or (r['strictRequired'] and r['strictDifferences']) or (r['bitwiseRequired'] and not r['bitwiseEqual'])]
  gpu=[r for r in comparisons if not r['bitwiseRequired'] and 'cross-platform' not in r['kind']]
  save(out/'invocation.json',dict(binarySha256=pins,referenceSha256=reference_hashes,commands=commands,
   buildInvocationSha256=None if a.payload else sha(host/'invocation.json')))
  state.update(complete=True,phase='complete',passed=not failures,filmsCompared=len(comparisons),failedFilms=len(failures),
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
