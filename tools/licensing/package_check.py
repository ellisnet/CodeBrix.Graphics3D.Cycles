#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Check a packed family against the reviewed payloads, then run the package audit.

    python3 tools/licensing/package_check.py nugets/Release/<version> --version <version>
        [--payload linux-x64=<scratch stage folder>] [--payload osx-arm64=<scratch stage folder>]

The expected contents come from the repository, never from the package: the notices
and documents must equal the files at the repository root, and a platform package's
natives must be exactly the runtime/ files approved in native/binaries/<rid>/provenance.json:
each at runtimes/<rid>/native/<name>, except a folder in OUTSIDE_NATIVE, which is at the
package root (osx-arm64: the Metal kernel sources), and a package's PACKAGE_FILES (its
buildTransitive targets) must equal those repository files.
Only the packer's own metadata and the .Core assembly, which a pack run compiles, are
taken from the package, by exact name. The result is the reviewed manifest that
audit.py --package compares the package with, written to <folder>/audit/.

Also required: one version across the family, an exact .Core dependency and the
exact WebGPU runtime dependency on every platform package that needs it, and no
dependencies and no natives on .Core. A payload whose native sources changed since
the commit it was built from (its sourceCommit) is reported as stale, because a package
can be packed but not published from it. Staleness is judged per build profile:
osx-arm64 (Cycles-with-Metal) by METAL_SOURCES (tools/licensing/metal_sources.py),
every other RID (Cycles-with-WGSL) by NATIVE_SOURCES below.

--payload RID=FOLDER checks that platform package against a scratch stage
(native/tools/stage_linux_binaries.py or stage_macos_binaries.py with --output
FOLDER) instead of the tracked native/binaries/<rid>/: its natives must be exactly
the stage's payloadFiles. A scratch stage is never approved, so the check passes
but reports the package as NOT publishable, with the uncommitted-change hash the
stage recorded.
"""
import argparse,hashlib,json,re,subprocess,sys,xml.etree.ElementTree as ET,zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/licensing'))
import audit
from metal_sources import METAL_SOURCES

SUFFIX='.ApacheLicenseForever'
CORE='CodeBrix.Graphics3D.Cycles.Core'+SUFFIX
WEBGPU='CodeBrix.WebGpu.Native.MitLicenseForever'
PLATFORMS={'CodeBrix.Graphics3D.Cycles.LinuxX64'+SUFFIX:'linux-x64','CodeBrix.Graphics3D.Cycles.MacOSArm64'+SUFFIX:'osx-arm64'}
# The payload's GPU backend does not use the WebGPU runtime (osx-arm64: Cycles' own Metal device).
NO_WEBGPU={'CodeBrix.Graphics3D.Cycles.MacOSArm64'+SUFFIX}
DOCUMENTS=['icon-codebrix-128.png','README.md','AGENT-README.txt','THIRD-PARTY-NOTICES.txt','LICENSE','NOTICE']
CORE_ASSEMBLY=['lib/net10.0/CodeBrix.Graphics3D.Cycles.dll','lib/net10.0/CodeBrix.Graphics3D.Cycles.xml']
# runtime/ folders shipped at the package root instead of under runtimes/<rid>/native/: NuGet flattens
# native-asset subfolders under a runtime identifier, so the MacOSArm64 package's buildTransitive
# targets copy its Metal kernel sources beside the host library instead.
OUTSIDE_NATIVE={'osx-arm64':['codebrix-cycles/']}
# Package path -> repository file, for the files a platform package carries besides natives and DOCUMENTS.
PACKAGE_FILES={'CodeBrix.Graphics3D.Cycles.MacOSArm64'+SUFFIX:{
 f'buildTransitive/CodeBrix.Graphics3D.Cycles.MacOSArm64{SUFFIX}.targets':f'build/nuget/CodeBrix.Graphics3D.Cycles.MacOSArm64{SUFFIX}.targets'}}
# The Cycles-with-WGSL profile: every RID except osx-arm64.
NATIVE_SOURCES=['native/host','native/cycles','native/shaders','native/oracle/runner.cpp','native/manylinux','third_party/cycles','third_party/deps','tools/dependencies']


def digest(data):return hashlib.sha256(data).hexdigest()


def nuspec_of(archive):
 names=[n for n in archive.namelist() if n.endswith('.nuspec') and '/' not in n]
 if len(names)!=1:raise RuntimeError('Expected one nuspec, found '+str(names))
 root=ET.fromstring(archive.read(names[0]));namespace=root.tag[:root.tag.index('}')+1]
 metadata=root.find(namespace+'metadata')
 groups={}
 for group in metadata.findall(f'{namespace}dependencies/{namespace}group'):
  for item in group.findall(namespace+'dependency'):groups[item.get('id')]=item.get('version')
 return metadata.find(namespace+'id').text,metadata.find(namespace+'version').text,groups,names[0]


def metadata_files(archive,nuspec):
 """The packer's own entries: these are the only names allowed besides the expected ones."""
 files={}
 for name in archive.namelist():
  if name in (nuspec,'[Content_Types].xml','_rels/.rels') or re.fullmatch(r'package/services/metadata/core-properties/[0-9A-Za-z]+\.psmdcp',name):
   files[name]=digest(archive.read(name))
 return files


def profile_sources(rid):
 """The build profile of a RID and the source paths its payload's staleness is judged by."""
 return ('Cycles-with-Metal',METAL_SOURCES) if rid=='osx-arm64' else ('Cycles-with-WGSL',NATIVE_SOURCES)


def stale(rid,provenance):
 commit=provenance.get('sourceCommit')
 if not commit:return None
 profile,sources=profile_sources(rid)
 changed=subprocess.run(['git','diff','--name-only',commit,'--',*sources],cwd=ROOT,capture_output=True,text=True)
 if changed.returncode:return f'{rid}: could not compare {profile} sources with {commit}'
 count=len(changed.stdout.split())
 return f'{rid}: payload is STALE: {count} {profile} source files changed since its source commit {commit}; restage it before publishing' if count else None


def scratch_payload(rid,folder):
 """The natives of a scratch stage, and the warning that it is not publishable."""
 provenance=json.loads((folder/'provenance.json').read_text())
 if provenance.get('rid')!=rid or provenance.get('scratchStage') is not True or provenance.get('approved') is not False:
  raise RuntimeError(f'{folder} is not a scratch stage of {rid}')
 files={name:sha for name,sha in provenance['payloadFiles'].items() if name.startswith('runtime/')}
 for name,sha in files.items():
  if digest((folder/name).read_bytes())!=sha:raise RuntimeError(f'{folder/name} changed after it was staged')
 changes=provenance.get('uncommittedChanges')
 note=f'uncommitted changes, git diff --binary sha256 {changes["gitDiffBinarySha256"]}' if changes else 'no uncommitted changes'
 commit=provenance.get('sourceCommit') or f'not recorded by the build; staged at {provenance.get("stagedAtCommit")}'
 return files,f'{rid}: packed from the scratch stage {folder} (commit {commit}, {note}): NOT APPROVED, NOT PUBLISHABLE'


def package_path(rid,name):
 """Where a payload runtime/ file is in its platform package."""
 relative=name[len('runtime/'):]
 return relative if any(relative.startswith(folder) for folder in OUTSIDE_NATIVE.get(rid,[])) else f'runtimes/{rid}/native/{relative}'


def expected_files(package_id,archive,nuspec,payloads={}):
 files=metadata_files(archive,nuspec)
 for name in DOCUMENTS:files[name]=digest((ROOT/name).read_bytes())
 for name,source in PACKAGE_FILES.get(package_id,{}).items():files[name]=digest((ROOT/source).read_bytes())
 if package_id==CORE:
  for name in CORE_ASSEMBLY:
   if name not in archive.namelist():raise RuntimeError(f'{package_id} has no {name}')
   files[name]=digest(archive.read(name))
  return files,None
 rid=PLATFORMS.get(package_id)
 if rid is None:raise RuntimeError(f'{package_id} is not a known package of this family')
 if rid in payloads:
  natives,warning=scratch_payload(rid,payloads[rid])
  for name,sha in natives.items():files[package_path(rid,name)]=sha
  return files,warning
 provenance=json.loads((ROOT/'native/binaries'/rid/'provenance.json').read_text())
 for name,sha in provenance['approvedPayload'].items():
  if name.startswith('runtime/'):files[package_path(rid,name)]=sha
 return files,stale(rid,provenance)


def check_dependencies(package_id,dependencies,version,webgpu_pin):
 if package_id==CORE:
  if dependencies:raise RuntimeError(f'{CORE} must have no dependencies; it has {dependencies}')
  return
 wanted={CORE:f'[{version}]'}
 if package_id not in NO_WEBGPU:wanted[WEBGPU]=f'[{webgpu_pin}]'
 if dependencies!=wanted:raise RuntimeError(f'{package_id} dependencies are {dependencies}, expected {wanted}')


def main():
 parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 parser.add_argument('folder',type=Path);parser.add_argument('--version',required=True)
 parser.add_argument('--payload',action='append',default=[],metavar='RID=FOLDER',help='Check that platform package against a scratch stage')
 args=parser.parse_args()
 payloads={}
 for item in args.payload:
  rid,_,folder=item.partition('=')
  if rid not in PLATFORMS.values() or not folder:parser.error(f'--payload {item}: expected RID=FOLDER with RID one of {sorted(PLATFORMS.values())}')
  payloads[rid]=Path(folder).resolve()
 packages=sorted(args.folder.glob('*.nupkg'))
 if not packages:raise SystemExit(f'No packages in {args.folder}')
 project=(ROOT/'src/CodeBrix.Graphics3D.Cycles/CodeBrix.Graphics3D.Cycles.csproj').read_text()
 webgpu_pin=re.search(r'Include="'+re.escape(WEBGPU)+r'" Version="\[([^\]]+)\]"',project).group(1)
 output=args.folder/'audit';output.mkdir(exist_ok=True);seen=[];warnings=[]
 for package in packages:
  with zipfile.ZipFile(package) as archive:
   package_id,version,dependencies,nuspec=nuspec_of(archive)
   if version!=args.version:raise RuntimeError(f'{package.name} has version {version}, expected {args.version}')
   check_dependencies(package_id,dependencies,version,webgpu_pin)
   files,warning=expected_files(package_id,archive,nuspec,payloads)
  if warning:warnings.append(warning)
  manifest=output/(package_id+'.payload-manifest.json')
  manifest.write_text(json.dumps(dict(approvedForPackaging=True,files=files),indent=1,sort_keys=True)+'\n')
  report=audit.package_audit(package,manifest)
  print(f'{package.name}: {len(report["files"])} files match the reviewed payload; audit passed; {package.stat().st_size} bytes')
  seen.append(package_id)
 if CORE not in seen:raise RuntimeError(f'{CORE} is missing from {args.folder}')
 for warning in warnings:print('WARNING: '+warning)


if __name__=='__main__':main()
