#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Render oracle scenes on this machine's backends and compare them.

    python3 native/oracle/render_oracles.py --output native/work/oracles/<run> \\
        --runner <codebrix_cycles_oracle> --host-library <libcodebrix_cycles_host> \\
        [--backend cpu-arm64-macos] [--scene native/oracle/scenes/starter/x.json] [--previews <tool>]
    python3 native/oracle/render_oracles.py --bundle native/work/oracles/<run>   # also writes native/evidence/cycles-oracles-macos-<date>/
    (on an Intel Mac the bundle and evidence directory are named cycles-oracles-macos-x64-<date>)

Films are written once and never adjusted. Metal backends run Cycles with
MetalRT and kernel specialization fixed by environment, each with a fresh
Cycles kernel cache under the run directory.
"""
import argparse,hashlib,json,os,platform,re,shutil,subprocess,sys,tarfile,tempfile,time
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
EVIDENCE=REPO/'native/evidence'
REVISION=json.loads((REPO/'third_party/cycles-import.json').read_text())['revision']
# name: (host backend, environment, description)
BACKENDS={
 'cpu-arm64-macos':('cpu',{},'Cycles CPU device, Apple Clang arm64, plain C++ kernels (no sse2neon), fixed-baseline oracle: no FMA contraction, no reciprocal math'),
 'metal-m2pro':('metal',{'CYCLES_METALRT':'0','CYCLES_METAL_SPECIALIZATION_LEVEL':'0'},
  'Cycles Metal device, MetalRT off (BVH2), generic kernels (PSO_GENERIC), fastMathEnabled as pinned'),
 'metal-m2pro-metalrt':('metal',{'CYCLES_METALRT':'1','CYCLES_METAL_SPECIALIZATION_LEVEL':'0'},
  'Cycles Metal device, MetalRT forced on (Apple ray-tracing API, software on M2), generic kernels; validation of the M3+ default path'),
 # Linux x64 (native/host/targets.cmake builds the runner as codebrix_cycles_oracle).
 'cpu-x64-linux':('cpu',{},'Cycles CPU device, GCC x86-64, fixed-baseline oracle: SSE4.2 kernels only, no FMA contraction, no reciprocal math'),
 'wgsl-linux-intel':('webgpu',{},'CodeBrix WGSL device on the unchanged CodeBrix.WebGpu.Native runtime (Vulkan, Intel Mesa)'),
 # macOS x64 (Intel Mac; tools/build_macos_x64.py builds the runner in oracle-build).
 'cpu-x64-macos':('cpu',{},'Cycles CPU device, Apple Clang x86-64, fixed-baseline oracle: SSE4.2 kernels only (no AVX2 kernel is built), no FMA contraction, no reciprocal math'),
 # Kept for comparison with runs made before the fixed baseline: without an AVX2 kernel the variable has no effect.
 'cpu-x64-macos-sse42':('cpu',{'CYCLES_CPU_NO_AVX2':'1'},'Cycles CPU device, Apple Clang x86-64, CYCLES_CPU_NO_AVX2 set (no effect on the fixed-baseline build: same kernels as cpu-x64-macos)'),
 'wgsl-macos-x64':('webgpu',{},'CodeBrix WGSL device on the unchanged CodeBrix.WebGpu.Native osx-x64 runtime (Metal)'),
 # Windows (native/host/WINDOWS_X64.md): clang-cl builds; the WebGPU backend is fixed per name.
 'cpu-x64-windows':('cpu',{},'Cycles CPU device, clang-cl x86-64, fixed-baseline oracle: SSE4.2 kernels only, no FMA contraction, no reciprocal math'),
 'wgsl-windows-x64-d3d12':('webgpu',{'CODEBRIX_CYCLES_WEBGPU_BACKEND':'d3d12'},'CodeBrix WGSL device on the unchanged CodeBrix.WebGpu.Native win-x64 runtime (Direct3D 12, DXC)'),
 'wgsl-windows-x64-vulkan':('webgpu',{'CODEBRIX_CYCLES_WEBGPU_BACKEND':'vulkan'},'CodeBrix WGSL device on the unchanged CodeBrix.WebGpu.Native win-x64 runtime (Vulkan)'),
 'cpu-arm64-windows':('cpu',{},'Cycles CPU device, clang-cl ARM64 (cross-built), fixed-baseline oracle: ARMv8-A base level, no FMA contraction, no reciprocal math'),
 'wgsl-windows-arm64-d3d12':('webgpu',{'CODEBRIX_CYCLES_WEBGPU_BACKEND':'d3d12'},'CodeBrix WGSL device on the unchanged CodeBrix.WebGpu.Native win-arm64 runtime (Direct3D 12, DXC)'),
 # Linux ARM64 (docs/TESTING-LINUX-ARM64.md; tools/build_linux_arm64.py, natively on the device).
 'cpu-arm64-linux':('cpu',{},'Cycles CPU device, GCC 14 aarch64 (glibc), fixed-baseline oracle: ARMv8-A base level, no FMA contraction, no reciprocal math'),
 'wgsl-linux-arm64-vulkan':('webgpu',{},'CodeBrix WGSL device on the unchanged CodeBrix.WebGpu.Native linux-arm64 runtime (Vulkan)'),
}
# Without --backend, a run renders the backends listed before the macOS x64 ones.
DEFAULT_BACKENDS=['cpu-arm64-macos','metal-m2pro','metal-m2pro-metalrt','cpu-x64-linux','wgsl-linux-intel']
INTEL_MAC=platform.system()=='Darwin' and platform.machine()=='x86_64'
WINDOWS=platform.system()=='Windows'
WINDOWS_RID={'AMD64':'win-x64','ARM64':'win-arm64'}.get(platform.machine()) if WINDOWS else None
LINUX_ARM64=platform.system()=='Linux' and platform.machine()=='aarch64'
DIAGNOSTIC_VARIABLES=['CODEBRIX_CYCLES_WGSL_LOAD','CODEBRIX_CYCLES_WEBGPU_ADAPTER','CODEBRIX_CYCLES_WEBGPU_ALLOW_NONCOMPLIANT',
 'CODEBRIX_CYCLES_DXC_MAX_SHADER_MODEL','CODEBRIX_CYCLES_WEBGPU_INSTANCE_FLAGS','CODEBRIX_CYCLES_WEBGPU_LOG','CODEBRIX_CYCLES_DEBUG_OUTPUT','CODEBRIX_CYCLES_DRIVER_WORKAROUNDS']


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def out(*command):
 try:return subprocess.check_output(list(command),text=True,stderr=subprocess.STDOUT).strip()
 except (OSError,subprocess.CalledProcessError) as error:return f'unavailable: {error}'


def machine():
 gpu=out('system_profiler','SPDisplaysDataType') if platform.system()=='Darwin' else \
  out('powershell','-NoProfile','-Command','(Get-CimInstance Win32_VideoController | ForEach-Object { "Chipset Model: " + $_.Name + " (driver " + $_.DriverVersion + ")" }) -join "`n"') if WINDOWS else out('lspci')
 model=re.search(r'Chipset Model:\s*(.*)',gpu);cores=re.search(r'Total Number of Cores:\s*(\d+)',gpu)
 status=out('git','-C',str(REPO),'status','--porcelain')
 cpuinfo=Path('/proc/cpuinfo').read_text() if Path('/proc/cpuinfo').exists() else ''
 linux_cpu=re.search(r'model name\s*:\s*(.*)',cpuinfo)
 if WINDOWS:
  build=REPO/'native/work/windows'/WINDOWS_RID/'host/invocation.json'
  identity=json.loads(build.read_text())['identity'] if build.exists() else {}
  return dict(os=platform.platform(),cpu=out('powershell','-NoProfile','-Command','(Get-CimInstance Win32_Processor).Name'),
   gpu=dict(model=model.group(1) if model else None,cores=None),compiler=identity.get('compilerVersion'),visualStudio=identity.get('visualStudio'),
   cmake=identity.get('cmakeVersion'),python=sys.version.split()[0],repoCommit=out('git','-C',str(REPO),'rev-parse','HEAD'),
   repoWorkingTreeClean=status=='',cyclesRevision=REVISION)
 return dict(os=out('sw_vers') if platform.system()=='Darwin' else out('uname','-a'),
  cpu=out('sysctl','-n','machdep.cpu.brand_string') if platform.system()=='Darwin' else (linux_cpu.group(1) if linux_cpu else platform.processor()),
  gpu=dict(model=model.group(1) if model else None,cores=int(cores.group(1)) if cores else None),
  compiler=out('clang','--version') if platform.system()=='Darwin' else out('c++','--version'),xcode=out('xcodebuild','-version'),cmake=out('cmake','--version').splitlines()[0],
  python=sys.version.split()[0],repoCommit=out('git','-C',str(REPO),'rev-parse','HEAD'),
  repoWorkingTreeClean=status=='',cyclesRevision=REVISION,
  # ARM64 boards (a Raspberry Pi) name themselves in the device tree; lspci finds no GPU there.
  **(dict(deviceModel=Path('/proc/device-tree/model').read_text().strip('\0\n'),vulkan=out('vulkaninfo','--summary')) if LINUX_ARM64 else {}))


def kernel_flags():
 """CPU kernel compile flags from the build's compile database (macOS host build or the
 Linux x64 build tree). The fixed-baseline oracle (native/DEVICE.md) switches contraction
 and reciprocal math off after Cycles' own math flags, so the last occurrence decides."""
 if platform.system()=='Darwin':
  database=REPO/'native/work/macos'/('osx-x64' if INTEL_MAC else 'osx-arm64')/'host/build/compile_commands.json'
 elif WINDOWS:
  database=REPO/'native/work/windows'/WINDOWS_RID/'host/build/compile_commands.json'
 elif LINUX_ARM64:
  database=REPO/'native/work/linux/linux-arm64/host/build/compile_commands.json'
 else:
  database=REPO/'native/work/cycles-build/compile_commands.json'
 if not database.exists():return None
 for row in json.loads(database.read_text()):
  if row['file'].replace(chr(92),'/').endswith('kernel/device/cpu/kernel.cpp'):
   # clang-cl passes Clang's math options as /clang:<option> (Windows).
   command=row.get('command','').replace('/clang:','');flags=[f for f in command.split() if re.match(r'-(O\d|ffp-contract|ffast-math|f(no-)?reciprocal-math|march|mcpu|mtune|mss?se|mavx|mfma|mf16c|std=)',f)]
   contract=[f for f in flags if f.startswith('-ffp-contract')];reciprocal=[f for f in flags if f.endswith('reciprocal-math') or f=='-ffast-math']
   return dict(file='src/kernel/device/cpu/kernel.cpp',database=database.relative_to(REPO).as_posix(),flags=flags,
    ffpContractExplicit=bool(contract),fpContract=contract[-1].split('=')[1] if contract else 'compiler default',
    reciprocalMath=bool(reciprocal) and reciprocal[-1]!='-fno-reciprocal-math',
    note='the last -ffp-contract / reciprocal-math option on the command line decides; Apple Clang arm64 and GCC default to contraction on without -ffp-contract'
     +('; clang-cl options are recorded without their /clang: prefix' if WINDOWS else ''))
 return None


def render(args,scene,name,target,cache,extra_meta):
 backend,environment,description=BACKENDS[name]
 env=dict(os.environ)
 for key in ['CYCLES_METALRT','CYCLES_METAL_SPECIALIZATION_LEVEL','OCIO','DYLD_LIBRARY_PATH','CODEBRIX_CYCLES_WEBGPU_BACKEND','CODEBRIX_CYCLES_DXC_PATH']:env.pop(key,None)
 # The macOS x64 CPU backends fix the kernel dispatch themselves.
 if name.startswith('cpu-x64-macos'):env.pop('CYCLES_CPU_NO_AVX2',None)
 env.update(environment);env['XDG_CACHE_HOME']=str(cache)
 command=[str(args.runner),'--scene',str(scene),'--backend',backend,'--label',name,'--output',str(target),'--threads',str(args.threads)]
 started=time.time()
 result=subprocess.run(command,env=env,capture_output=True,text=True)
 # Not *.log: the repository ignores those, and these logs are published as evidence.
 (target.parent/(target.name+'.output.txt')).write_text(result.stdout+result.stderr)
 if result.returncode:raise RuntimeError(f'{name} failed on {scene}: {result.stderr.strip()}')
 meta=json.loads((target/'meta.json').read_text())
 warnings=[line for line in result.stderr.splitlines() if line.strip() and not line.startswith('[OpenColorIO Info]')]
 meta.update(extra_meta);meta.update(backendDescription=description,environment=environment,processSeconds=round(time.time()-started,3),
  runnerCommand=command,warnings=meta['warnings']+warnings)
 if backend=='metal':
  meta['metalSettings']=dict(device='DEVICE_METAL',metalrt=environment['CYCLES_METALRT']=='1',
   specializationLevel=f"CYCLES_METAL_SPECIALIZATION_LEVEL={environment['CYCLES_METAL_SPECIALIZATION_LEVEL']} (PSO_GENERIC)",
   bvhLayout='BVH_LAYOUT_METAL' if environment['CYCLES_METALRT']=='1' else 'BVH_LAYOUT_BVH2',fastMathEnabled=True,denoiser=None,
   kernelSource=str(Path(args.host_library).parent/'codebrix-cycles/source'),kernelCache=str(cache/'cycles'),
   systemMetalShaderCache='not cleared (macOS-managed)',optionNames=['CYCLES_METALRT','CYCLES_METAL_SPECIALIZATION_LEVEL','DeviceInfo::use_hardware_raytracing','DeviceInfo::kernel_optimization_level'])
 meta['deviceName']=(f"{extra_meta['machine']['gpu']['model']} (the C API does not expose Cycles' DeviceInfo)" if backend=='metal' else
  'WebGPU adapter: see machine.gpu and cbc_host_capabilities_json'+(f" ({environment['CODEBRIX_CYCLES_WEBGPU_BACKEND']})" if 'CODEBRIX_CYCLES_WEBGPU_BACKEND' in environment else '') if backend=='webgpu' else
  f"{extra_meta['machine']['cpu']} (the C API does not expose Cycles' DeviceInfo)")
 (target/'meta.json').write_text(json.dumps(meta,indent=2)+'\n')
 if args.previews:
  subprocess.run([str(args.previews),'preview',str(target/'film-0.f32'),str(meta['width']),str(meta['height']),str(target/'film-0.png')],check=True)
 return meta


def run(args):
 root=args.output.resolve()
 if root.exists() and any(root.iterdir()):raise SystemExit(f'{root} is not empty; use a new run directory')
 oracle=root/'oracle';oracle.mkdir(parents=True)
 # Scene-family manifest.json files list hashes; they are not scenes.
 scenes=[Path(s).resolve() for s in args.scene] or sorted(p for p in (HERE/'scenes').rglob('*.json') if p.name!='manifest.json')
 backends=args.backend or list(DEFAULT_BACKENDS)
 common=dict(machine=machine(),hostLibrarySha256=sha(args.host_library),runnerSha256=sha(args.runner),cpuKernelCompile=kernel_flags())
 record=dict(scenes={},backends=backends,common=common,kernelCompileSeconds={})
 # Opt-in WebGPU diagnostics (native/src/webgpu_device.h) make the run diagnostic,
 # never evidence: they are recorded, and every comparison is reported as such.
 diagnostic={k:v for k,v in os.environ.items() if k in DIAGNOSTIC_VARIABLES and v}
 if diagnostic:record['diagnosticRun']=diagnostic;print(f'DIAGNOSTIC RUN (not evidence): {diagnostic}',flush=True)
 for name in backends:
  cache=root/'cache'/name;cache.mkdir(parents=True)
  if BACKENDS[name][0]=='metal':
   # First render with an empty Cycles cache measures the run-time kernel compile.
   warm=root/'warmup'/name;warm.parent.mkdir(exist_ok=True)
   meta=render(args,scenes[0],name,warm,cache,dict(common,sceneFile=str(scenes[0].relative_to(REPO))))
   record['kernelCompileSeconds'][name]=dict(firstRenderSeconds=meta['renderSeconds'][0],secondRenderSeconds=meta['renderSeconds'][1],scene=meta['sceneId'])
  for scene in scenes:
   identifier=json.loads(scene.read_text())['id'];target=oracle/identifier/name;target.parent.mkdir(parents=True,exist_ok=True)
   extra=dict(common,sceneFile=str(scene.relative_to(REPO)))
   if name in record['kernelCompileSeconds']:extra['kernelCompileSeconds']=record['kernelCompileSeconds'][name]['firstRenderSeconds']
   try:meta=render(args,scene,name,target,cache,extra)
   except RuntimeError as error:
    # A backend that rejects a scene is a recorded finding, not a run abort.
    record.setdefault('failures',{}).setdefault(identifier,{})[name]=str(error).splitlines()[-1]
    print(f'{identifier:<40} {name:<22} FAILED: {str(error).splitlines()[-1]}',flush=True);continue
   record['scenes'].setdefault(identifier,{})[name]=dict(renderSeconds=meta['renderSeconds'],repeatBitwiseIdentical=meta['repeatBitwiseIdentical'])
   print(f'{identifier:<40} {name:<22} {meta["renderSeconds"]}',flush=True)
 comparisons={}
 for identifier in record['scenes']:
  directories=[oracle/identifier/name for name in backends if name in record['scenes'][identifier]]
  if len(directories)<2:comparisons[identifier]=None;continue
  report=root/'comparisons'/(identifier.replace('/','__')+'.json');report.parent.mkdir(exist_ok=True)
  result=subprocess.run([sys.executable,str(HERE/'compare.py'),*map(str,directories),'--json',str(report)],capture_output=True,text=True)
  print(result.stdout,flush=True);comparisons[identifier]=json.loads(report.read_text())['passed'] if report.exists() else False
 record['comparisonsPassed']=comparisons
 shutil.rmtree(root/'cache')
 (root/'run.json').write_text(json.dumps(record,indent=2)+'\n')
 return 0


def bundle(root):
 """Pack the run with a manifest, verify a fresh extraction, and publish that
 extraction as new repository evidence. Existing evidence is never overwritten."""
 root=root.resolve();files=sorted(p for p in root.rglob('*') if p.is_file() and p.name!='manifest.json' and 'bundle' not in p.parts)
 name='cycles-oracles-macos-x64' if INTEL_MAC else 'cycles-oracles-macos'
 manifest=dict(bundle=f'{name}-{time.strftime("%Y%m%d")}',created=time.strftime('%Y-%m-%dT%H:%M:%S%z'),
  files={str(p.relative_to(root)):sha(p) for p in files})
 (root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 target=root/'bundle';target.mkdir(exist_ok=True);tar=target/(manifest['bundle']+'.tar')
 with tarfile.open(tar,'w') as archive:
  for p in [root/'manifest.json',*files]:archive.add(p,arcname=str(p.relative_to(root)))
 zst=tar.with_suffix('.tar.zst')
 if zst.exists():zst.unlink()
 subprocess.run(['zstd','-19','-q','--rm',str(tar),'-o',str(zst)],check=True)
 with tempfile.TemporaryDirectory() as fresh:
  subprocess.run(['zstd','-d','-q',str(zst),'-o',str(Path(fresh)/'b.tar')],check=True)
  with tarfile.open(Path(fresh)/'b.tar') as archive:archive.extractall(Path(fresh)/'x',filter='data')
  extracted=json.loads((Path(fresh)/'x/manifest.json').read_text())
  assert extracted==manifest and all(sha(Path(fresh)/'x'/name)==digest for name,digest in manifest['files'].items())
  evidence=EVIDENCE/manifest['bundle'];suffix=2
  while evidence.exists():evidence=EVIDENCE/f"{manifest['bundle']}-{suffix}";suffix+=1
  shutil.copytree(Path(fresh)/'x',evidence)
 print(json.dumps(dict(bundle=str(zst),sha256=sha(zst),files=len(manifest['files']),verified=True,
  evidence=str(evidence.relative_to(REPO))),indent=2))
 return 0


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 p.add_argument('--output',type=Path);p.add_argument('--runner',type=Path);p.add_argument('--host-library',type=Path)
 p.add_argument('--backend',action='append',choices=list(BACKENDS));p.add_argument('--scene',action='append',default=[])
 p.add_argument('--threads',type=int,default=4);p.add_argument('--previews',type=Path);p.add_argument('--bundle',type=Path)
 p.add_argument('--payload',type=Path,help='Windows: render from a staged payload (native/binaries/<rid>) instead of --runner/--host-library')
 a=p.parse_args()
 if a.bundle:return bundle(a.bundle)
 if a.payload:
  if not a.output:p.error('--output is required')
  a.runner,a.host_library=payload_run_folder(a.payload.resolve(),a.output.resolve())
 if not (a.output and a.runner and a.host_library):p.error('--output, --runner and --host-library are required')
 return run(a)


def payload_run_folder(payload,output):
 """A staged Windows payload carries the host, its DLLs, DXC and the oracle runner,
 but not the WebGPU runtime (its own NuGet package). Assemble one run folder from
 the verified payload and the pinned runtime, as an application has them
 (native/tools/windows_host_gate.py --payload does the same)."""
 provenance=json.loads((payload/'provenance.json').read_text())
 for name,digest in provenance['approvedPayload'].items():
  if sha(payload/name)!=digest:raise SystemExit('Payload file changed since staging: '+name)
 sys.path.insert(0,str(REPO/'research'));from native_runner import PACKAGE
 runtime=PACKAGE/provenance['webgpuRuntime']['path']
 if not runtime.is_file() or sha(runtime)!=provenance['webgpuRuntime']['sha256']:
  raise SystemExit(f'Restore {provenance["webgpuRuntime"]["id"]} {provenance["webgpuRuntime"]["version"]} first (dotnet restore research/GpuProbe/GpuProbe.csproj)')
 folder=output.parent/(output.name+'-payload-run')
 if folder.exists():raise SystemExit(f'{folder} exists; use a new run directory')
 folder.mkdir(parents=True)
 for name in provenance['approvedPayload']:
  if name.startswith(('runtime/','tests/')):shutil.copy2(payload/name,folder/Path(name).name)
 shutil.copy2(runtime,folder/runtime.name)
 return folder/'codebrix_cycles_oracle.exe',folder/'codebrix_cycles_host.dll'


if __name__=='__main__':sys.exit(main())
