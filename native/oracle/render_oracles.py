#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Render oracle scenes on this machine's backends and compare them.

    python3 native/oracle/render_oracles.py --output native/work/oracles/<run> \\
        --runner <codebrix_cycles_oracle> --host-library <libcodebrix_cycles_host> \\
        [--backend cpu-arm64-macos] [--scene native/oracle/scenes/starter/x.json] [--previews <tool>]
    python3 native/oracle/render_oracles.py --bundle native/work/oracles/<run>   # also writes native/evidence/cycles-oracles-macos-<date>/

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
 'cpu-arm64-macos':('cpu',{},'Cycles CPU device, Apple Clang arm64, plain C++ kernels (no sse2neon)'),
 'metal-m2pro':('metal',{'CYCLES_METALRT':'0','CYCLES_METAL_SPECIALIZATION_LEVEL':'0'},
  'Cycles Metal device, MetalRT off (BVH2), generic kernels (PSO_GENERIC), fastMathEnabled as pinned'),
 'metal-m2pro-metalrt':('metal',{'CYCLES_METALRT':'1','CYCLES_METAL_SPECIALIZATION_LEVEL':'0'},
  'Cycles Metal device, MetalRT forced on (Apple ray-tracing API, software on M2), generic kernels; validation of the M3+ default path'),
}


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def out(*command):
 try:return subprocess.check_output(list(command),text=True,stderr=subprocess.STDOUT).strip()
 except (OSError,subprocess.CalledProcessError) as error:return f'unavailable: {error}'


def machine():
 gpu=out('system_profiler','SPDisplaysDataType') if platform.system()=='Darwin' else out('lspci')
 model=re.search(r'Chipset Model:\s*(.*)',gpu);cores=re.search(r'Total Number of Cores:\s*(\d+)',gpu)
 status=out('git','-C',str(REPO),'status','--porcelain')
 return dict(os=out('sw_vers') if platform.system()=='Darwin' else out('uname','-a'),
  cpu=out('sysctl','-n','machdep.cpu.brand_string') if platform.system()=='Darwin' else platform.processor(),
  gpu=dict(model=model.group(1) if model else None,cores=int(cores.group(1)) if cores else None),
  compiler=out('clang','--version'),xcode=out('xcodebuild','-version'),cmake=out('cmake','--version').splitlines()[0],
  python=sys.version.split()[0],repoCommit=out('git','-C',str(REPO),'rev-parse','HEAD'),
  repoWorkingTreeClean=status=='',cyclesRevision=REVISION)


def kernel_flags():
 """CPU kernel compile flags from the macOS build's compile database."""
 database=REPO/'native/work/macos/osx-arm64/host/build/compile_commands.json'
 if not database.exists():return None
 for row in json.loads(database.read_text()):
  if row['file'].endswith('kernel/device/cpu/kernel.cpp'):
   command=row.get('command','');flags=[f for f in command.split() if re.match(r'-(O\d|ffp-contract|ffast-math|march|mcpu|std=)',f)]
   return dict(file='src/kernel/device/cpu/kernel.cpp',flags=flags,ffpContractExplicit=any('ffp-contract' in f for f in flags),
    note='Apple Clang default for arm64 without -ffp-contract is on (fused multiply-add within expressions)')
 return None


def render(args,scene,name,target,cache,extra_meta):
 backend,environment,description=BACKENDS[name]
 env=dict(os.environ)
 for key in ['CYCLES_METALRT','CYCLES_METAL_SPECIALIZATION_LEVEL','OCIO','DYLD_LIBRARY_PATH']:env.pop(key,None)
 env.update(environment);env['XDG_CACHE_HOME']=str(cache)
 command=[str(args.runner),'--scene',str(scene),'--backend',backend,'--label',name,'--output',str(target),'--threads',str(args.threads)]
 started=time.time()
 result=subprocess.run(command,env=env,capture_output=True,text=True)
 (target.parent/(target.name+'.log')).write_text(result.stdout+result.stderr)
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
 meta['deviceName']=f"{extra_meta['machine']['gpu']['model'] if backend=='metal' else extra_meta['machine']['cpu']} (the C API does not expose Cycles' DeviceInfo)"
 (target/'meta.json').write_text(json.dumps(meta,indent=2)+'\n')
 if args.previews:
  subprocess.run([str(args.previews),'preview',str(target/'film-0.f32'),str(meta['width']),str(meta['height']),str(target/'film-0.png')],check=True)
 return meta


def run(args):
 root=args.output.resolve()
 if root.exists() and any(root.iterdir()):raise SystemExit(f'{root} is not empty; use a new run directory')
 oracle=root/'oracle';oracle.mkdir(parents=True)
 scenes=[Path(s).resolve() for s in args.scene] or sorted((HERE/'scenes').rglob('*.json'))
 backends=args.backend or list(BACKENDS)
 common=dict(machine=machine(),hostLibrarySha256=sha(args.host_library),runnerSha256=sha(args.runner),cpuKernelCompile=kernel_flags())
 record=dict(scenes={},backends=backends,common=common,kernelCompileSeconds={})
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
   meta=render(args,scene,name,target,cache,extra)
   record['scenes'].setdefault(identifier,{})[name]=dict(renderSeconds=meta['renderSeconds'],repeatBitwiseIdentical=meta['repeatBitwiseIdentical'])
   print(f'{identifier:<40} {name:<22} {meta["renderSeconds"]}',flush=True)
 comparisons={}
 for identifier in record['scenes']:
  directories=[oracle/identifier/name for name in backends]
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
 manifest=dict(bundle=f'cycles-oracles-macos-{time.strftime("%Y%m%d")}',created=time.strftime('%Y-%m-%dT%H:%M:%S%z'),
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
 a=p.parse_args()
 if a.bundle:return bundle(a.bundle)
 if not (a.output and a.runner and a.host_library):p.error('--output, --runner and --host-library are required')
 return run(a)


if __name__=='__main__':sys.exit(main())
