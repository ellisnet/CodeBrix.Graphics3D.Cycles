#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Stage the linux-x64 deliverable in native/binaries/linux-x64.

    python3 native/tools/stage_linux_binaries.py

Builds the host and the oracle runner at the current commit first (a no-op when
they are current), and refuses when the native sources have uncommitted changes,
so the payload always names the one commit it was built from.

runtime/    stripped host and dependency libraries, named by SONAME
unstripped/ the same libraries before stripping (symbol tables retained; the build
            has no DWARF, so there is no symbols/ folder)
provenance.json records inputs, toolchain, hashes, the ELF review, the minimum
system library versions and the exact approved payload that
tools/licensing/audit.py accepts.

The shipped WebGPU runtime (libcodebrix_wgpu.so) is not part of this payload: it is
delivered by the CodeBrix.WebGpu.Native.MitLicenseForever package, and
provenance.json records its package, version and SHA-256. NuGet places it beside
this payload in a consuming application.

ELF review, for every staged library: x86-64; RUNPATH exactly $ORIGIN; every NEEDED
library is in this payload, is the WebGPU runtime or is a system C/C++ runtime
library. RUNPATH is rewritten in place, so no extra tool is needed; the rewrite is
refused when the new string does not fit, and the dynamic symbols and NEEDED list
are compared before and after it, because linkers can share string table tails.

The runtime payload is then copied to a temporary directory together with the
WebGPU runtime and rendered with on CPU and WebGPU in a clean environment. The CPU
film must equal the newest committed Linux x64 reference film of that scene.
"""
import hashlib,json,os,platform,re,shutil,struct,subprocess,sys,tempfile,time
from pathlib import Path

REPO=Path(__file__).resolve().parents[2]
RID='linux-x64'
BUILD=REPO/'native/work/cycles-build';INSTALL=REPO/'native/work/dependencies/install/lib'
TARGET=REPO/'native/binaries'/RID;HOST='libcodebrix_cycles_host.so'
# OIDN loads its CPU device by name at run time, so no NEEDED entry reaches it.
RUNTIME_LOADED=['libOpenImageDenoise_device_cpu.so.2.5.1']
RUNTIME_NAME='libcodebrix_wgpu.so'
RUNTIME_PACKAGE=dict(id='CodeBrix.WebGpu.Native.MitLicenseForever',version='1.0.300.210',path=f'runtimes/{RID}/native/{RUNTIME_NAME}')
SYSTEM={'libc.so.6','libm.so.6','libgcc_s.so.1','libstdc++.so.6','ld-linux-x86-64.so.2','libdl.so.2','libpthread.so.0','librt.so.1'}
NATIVE_SOURCES=['native/host','native/cycles','native/shaders','native/oracle/runner.cpp','third_party/cycles','third_party/deps','tools/dependencies']
REFERENCE='native/evidence/linux-x64-reference-20261107'
CHECK_SCENE='starter/diffuse-triangle'
# Shipped library name prefix -> third_party/dependencies.json component.
COMPONENTS=[('libcodebrix_cycles_host','cycles'),('libOpenImageDenoise','oidn'),('libOpenImageIO','oiio'),('libOpenColorIO','ocio'),
 ('libOpenEXR','openexr'),('libIex','openexr'),('libIlmThread','openexr'),('libImath','imath'),('libboost_','boost'),('libexpat','expat'),
 ('libpugixml','pugixml'),('libtbb','tbb'),('libzstd','zstd'),('libz.','zlib'),('libpng','png'),('libjpeg','jpeg')]
STATIC={'cycles':['Cycles (with mikktspace and sky)','CodeBrix host','CodeBrix WGSL/WebGPU device','embree'],
 'ocio':['yaml-cpp','pystring','minizip','ocio-embedded'],'oiio':['fmt','robin-map','oiio-embedded']}


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def tool(*args,**kw):return subprocess.run(args,check=True,capture_output=True,text=True,**kw).stdout


def dynamic(path):
 text=tool('readelf','-d','-W',str(path))
 return dict(needed=re.findall(r'\(NEEDED\).*\[(.+?)\]',text),soname=(re.findall(r'\(SONAME\).*\[(.+?)\]',text) or [None])[0],
  runpath=re.findall(r'\((?:RUNPATH|RPATH)\).*\[(.*?)\]',text))


def dynamic_symbols(path):
 lines=tool('readelf','--dyn-syms','-W',str(path)).splitlines()
 return sorted(line.split(None,1)[1] for line in lines if re.match(r'\s*\d+:',line))


def set_runpath(path,value=b'$ORIGIN'):
 """Overwrite the existing DT_RUNPATH or DT_RPATH string in place (ELF64, little endian)."""
 data=bytearray(Path(path).read_bytes())
 if data[:6]!=b'\x7fELF\x02\x01':raise RuntimeError(f'{path} is not a little-endian ELF64 file')
 phoff=struct.unpack_from('<Q',data,32)[0];phentsize,phnum=struct.unpack_from('<HH',data,54)
 loads=[];dynamic_at=None
 for index in range(phnum):
  at=phoff+index*phentsize;kind=struct.unpack_from('<I',data,at)[0]
  offset,vaddr=struct.unpack_from('<QQ',data,at+8);filesz=struct.unpack_from('<Q',data,at+32)[0]
  if kind==1:loads.append((vaddr,offset,filesz))
  elif kind==2:dynamic_at=(offset,filesz)
 if dynamic_at is None:raise RuntimeError(f'{path} has no dynamic segment')
 def file_offset(address):
  for vaddr,offset,filesz in loads:
   if vaddr<=address<vaddr+filesz:return address-vaddr+offset
  raise RuntimeError(f'{path}: address {address:#x} is outside the load segments')
 strtab=None;entry=None
 for at in range(dynamic_at[0],dynamic_at[0]+dynamic_at[1],16):
  tag,val=struct.unpack_from('<QQ',data,at)
  if tag==0:break
  if tag==5:strtab=val
  elif tag in (15,29):entry=val
 if strtab is None or entry is None:raise RuntimeError(f'{path} has no run path entry to rewrite')
 start=file_offset(strtab)+entry;end=data.index(0,start)
 if len(value)>end-start:raise RuntimeError(f'{path}: the new run path does not fit')
 data[start:end]=value+bytes(end-start-len(value))
 Path(path).write_bytes(data)


def closure():
 """SONAME -> the file that holds it, from the host through NEEDED, plus what OIDN loads by name."""
 found={};queue=[(HOST,BUILD/'lib'/HOST)]+[(name,INSTALL/name) for name in RUNTIME_LOADED]
 while queue:
  name,path=queue.pop(0)
  if name in found:continue
  found[name]=path
  for needed in dynamic(path)['needed']:
   if (INSTALL/needed).exists():queue.append((needed,INSTALL/needed))
   elif needed not in SYSTEM and needed!=RUNTIME_NAME:raise RuntimeError(f'{name} needs {needed}, outside the payload and the system set')
 return found


def review(path,names):
 info=dynamic(path)
 if 'Machine:                           Advanced Micro Devices X86-64' not in tool('readelf','-h',str(path)):raise RuntimeError(f'{path.name} is not x86-64')
 if info['runpath']!=['$ORIGIN']:raise RuntimeError(f'{path.name} run path is {info["runpath"]}')
 if info['soname'] not in (path.name,None):raise RuntimeError(f'{path.name} has SONAME {info["soname"]}')
 for needed in info['needed']:
  if needed not in names and needed!=RUNTIME_NAME and needed not in SYSTEM:raise RuntimeError(f'{path.name} links {needed}')
 return info


def floors(paths):
 """The newest symbol version of each system library family the payload asks for."""
 best={}
 for path in paths:
  for family,version in re.findall(r'\b(GLIBC|GLIBCXX|CXXABI|GCC)_([0-9][0-9.]*)',tool('objdump','-T',str(path))):
   key=tuple(int(part) for part in version.split('.'))
   if family not in best or key>best[family][0]:best[family]=(key,version)
 return {family:version for family,(key,version) in sorted(best.items())}


def standalone_render(runtime,wgpu):
 """The runtime payload alone must load and render: copy it, point a runner at it, clear the environment."""
 with tempfile.TemporaryDirectory() as temporary:
  root=Path(temporary)/'payload';shutil.copytree(runtime,root);shutil.copyfile(wgpu,root/RUNTIME_NAME)
  runner=root/'codebrix_cycles_oracle';shutil.copyfile(BUILD/'bin/codebrix_cycles_oracle',runner);runner.chmod(0o755);set_runpath(runner)
  env={'PATH':'/usr/bin:/bin','HOME':str(Path.home()),'XDG_CACHE_HOME':str(Path(temporary)/'cache')}
  resolved=tool('ldd',str(runner),env=env)
  if 'not found' in resolved:raise RuntimeError('Standalone payload has unresolved libraries:\n'+resolved)
  outside=[line.strip() for line in resolved.splitlines() if '=>' in line and not line.split('=>')[1].strip().startswith(str(root))
           and line.split()[0] not in SYSTEM]
  if outside:raise RuntimeError('Standalone payload resolved libraries elsewhere:\n'+'\n'.join(outside))
  scene=REPO/'native/oracle/scenes'/(CHECK_SCENE+'.json');results={}
  for backend in ['cpu','webgpu']:
   output=Path(temporary)/backend
   result=subprocess.run([str(runner),'--scene',str(scene),'--backend',backend,'--output',str(output)],capture_output=True,text=True,env=env)
   if result.returncode:raise RuntimeError(f'Standalone payload failed on {backend}: {result.stderr[-800:]}')
   results[backend]=json.loads((output/'meta.json').read_text())['filmSha256'][0]
 expected=json.loads((REPO/REFERENCE/'oracles/corpus/oracle'/CHECK_SCENE/'cpu-x64-linux/meta.json').read_text())['filmSha256'][0]
 if results['cpu']!=expected:raise RuntimeError(f'Standalone CPU film {results["cpu"]} differs from the reference film {expected}')
 return dict(scene=CHECK_SCENE,filmSha256=results,cpuFilmEqualsReference=REFERENCE)


def approvable(payload):
 """The audit requires every approved file to be a tracked candidate, so a git-ignored folder is
 recorded in `binaries` but not approved. Un-ignoring it and staging again approves it."""
 ignored=subprocess.run(['git','check-ignore','--stdin'],cwd=REPO,input='\n'.join(str(TARGET.relative_to(REPO)/name) for name in payload),capture_output=True,text=True).stdout.split()
 return {name:digest for name,digest in payload.items() if str(TARGET.relative_to(REPO)/name) not in ignored}


def main():
 if platform.system()!='Linux' or platform.machine()!='x86_64':
  raise SystemExit(f'{RID}: staging runs only on the Linux x64 machine; this host is {platform.system()} {platform.machine()}')
 if TARGET.exists() and any(TARGET.iterdir()):raise RuntimeError(f'{TARGET} already holds a payload; review and remove it first')
 dirty=tool('git','status','--porcelain','--',*NATIVE_SOURCES,cwd=REPO).strip()
 if dirty:raise RuntimeError('Native sources have uncommitted changes; commit them first:\n'+dirty)
 commit=tool('git','rev-parse','HEAD',cwd=REPO).strip()
 subprocess.run([sys.executable,str(REPO/'native/tools/build_cycles.py'),'--jobs','6','--target','codebrix_cycles_host','--target','codebrix_cycles_oracle'],check=True,cwd=REPO,capture_output=True)
 package=Path(os.environ.get('NUGET_PACKAGES') or Path.home()/'.nuget/packages')/RUNTIME_PACKAGE['id'].lower()/RUNTIME_PACKAGE['version']/RUNTIME_PACKAGE['path']
 if not package.exists():raise RuntimeError(f'{package} is missing; run dotnet restore first')
 sources=closure();names=set(sources)
 runtime,unstripped=TARGET/'runtime',TARGET/'unstripped'
 for folder in (runtime,unstripped):folder.mkdir(parents=True)
 manifest=json.loads((REPO/'third_party/dependencies.json').read_text());by_id={c['id']:c for c in manifest['components']}
 mapping={}
 for leaf,source in sorted(sources.items()):
  shutil.copyfile(source,unstripped/leaf);(unstripped/leaf).chmod(0o755)
  before=(dynamic(source)['needed'],dynamic_symbols(source))
  set_runpath(unstripped/leaf)
  if (dynamic(unstripped/leaf)['needed'],dynamic_symbols(unstripped/leaf))!=before:raise RuntimeError(f'{leaf}: rewriting the run path changed its symbols or NEEDED list')
  shutil.copyfile(unstripped/leaf,runtime/leaf);(runtime/leaf).chmod(0o755)
  subprocess.run(['strip','--strip-unneeded',str(runtime/leaf)],check=True,capture_output=True)
  if (dynamic(runtime/leaf)['needed'],dynamic_symbols(runtime/leaf))!=before:raise RuntimeError(f'{leaf}: stripping changed its symbols or NEEDED list')
  info=review(runtime/leaf,names)
  component=next(c for prefix,c in COMPONENTS if leaf.startswith(prefix))
  mapping[leaf]=dict(runtime=f'runtime/{leaf}',unstripped=f'unstripped/{leaf}',buildSource=str(Path(source).resolve().relative_to(REPO)),needed=info['needed'],runpath=info['runpath'],
   component=component,license=by_id[component]['license'],notices=by_id[component]['notices'],staticallyIncluded=STATIC.get(component,[]))
 payload={str(f.relative_to(TARGET)):sha(f) for f in sorted(TARGET.rglob('*')) if f.is_file()}
 for leaf,entry in mapping.items():
  entry['runtimeSha256']=payload[entry['runtime']];entry['unstrippedSha256']=payload[entry['unstripped']]
 build_record=json.loads((REPO/'native/work/cycles-build.json').read_text())
 record=dict(
  rid=RID,created=time.strftime('%Y-%m-%dT%H:%M:%S%z'),sourceCommit=commit,
  scope='Reviewed linux-x64 product binaries: Cycles with its CPU device and the CodeBrix WGSL/WebGPU device behind the CodeBrix C API.',
  sources=dict(cyclesRevision=build_record['cyclesRevision'],dependencyManifestSha256=sha(REPO/'third_party/dependencies.json'),
   dependencyImportsSha256=sha(REPO/'third_party/dependency-imports.json'),registrationPatchSha256=build_record['registrationPatchSha256']),
  toolchain=dict(compiler=tool('gcc','--version').splitlines()[0],libc=tool('ldd','--version').splitlines()[0]),
  buildCommands=build_record['commands'],cpuKernelCompile=build_record['cpuKernelCompile'],
  minimumSystemVersions=floors(runtime/leaf for leaf in mapping),
  webGpuRuntime=dict(RUNTIME_PACKAGE,sha256=sha(package)),
  binaries=mapping,standaloneRender=standalone_render(runtime,package),
  notices=dict(repositoryNotices='THIRD-PARTY-NOTICES.txt'),approvedPayload=approvable(payload),snapshotCopies={})
 # Repository paths are recorded relative to the checkout root.
 text=json.dumps(record,indent=2).replace(str(REPO)+'/','').replace(str(REPO),'.')
 (TARGET/'provenance.json').write_text(text+'\n')
 print(json.dumps(dict(staged=str(TARGET.relative_to(REPO)),commit=commit,binaries=len(mapping),files=len(payload),
  runtimeBytes=sum((runtime/leaf).stat().st_size for leaf in mapping),minimumSystemVersions=record['minimumSystemVersions'],standaloneRender=record['standaloneRender']),indent=2))


if __name__=='__main__':main()
