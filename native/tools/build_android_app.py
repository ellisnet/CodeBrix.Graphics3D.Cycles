#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build an SDK Android validation application from maintained host/test sources."""
import argparse, hashlib, json, os, shutil, struct, subprocess, sys, zipfile
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(REPO/'native/tools'))
from build import PACKAGE
from device_gate import sha,save
from render_gate import inputs
from android_toolchain import AndroidToolchain,TARGETS

SDK_PACKS=[Path('/usr/share/dotnet/packs')/(name+'/'+version) for name,version in [
 ('Microsoft.Android.Runtime.Mono.36.android-arm64','36.1.118'),
 ('Microsoft.Android.Runtime.Mono.36.android-x64','36.1.118'),
 ('Microsoft.NETCore.App.Runtime.Mono.android-arm64','10.0.13'),
 ('Microsoft.NETCore.App.Runtime.Mono.android-x64','10.0.13')]]


def replace_once(text,old,new):
    if text.count(old)!=1:raise RuntimeError('Managed fixture adaptation anchor changed: '+old)
    return text.replace(old,new)


def checks(source,name):
    text=source.read_text()
    marker='Require(args.Length == 2,'
    if text.count(marker)!=1:raise RuntimeError('Expected one top-level entry point')
    helpers,body=text.split(marker,1)
    at=helpers.index('static void Require(')
    imports,helpers=helpers[:at],helpers[at:]
    body=marker+body
    if name=='HostChecks':
        old='Require(bytes.SequenceEqual(File.ReadAllBytes(Path.Combine(args[0], $"run.{round}.rgba.bin"))), "Managed film differs from original CPU Session");'
        new='comparisons.Add(FilmComparison.Check(bytes, Path.Combine(args[0], $"run.{round}.rgba.bin"), Path.Combine(args[1], $"run.{round}.rgba.bin"), true));'
    else:
        old='Require(bytes.SequenceEqual(File.ReadAllBytes(Path.Combine(args[0], name))), "Managed image mode differs from native reference: " + completed);'
        new='comparisons.Add(FilmComparison.Check(bytes, Path.Combine(args[0], name), Path.Combine(args[1], name), false));'
    body=replace_once(body,old,new)
    if body.count('Console.WriteLine(')!=1:raise RuntimeError('Expected one final fixture report')
    body=body[:body.index('Console.WriteLine(')]+'return comparisons;\n'
    signature='public static async Task<List<FilmComparison>> Run(string[] args)' if name=='HostChecks' else 'public static List<FilmComparison> Run(string[] args)'
    return imports+'// Generated from maintained console checks; Linux checks remain unchanged.\ninternal static class '+name+'\n{\n'+helpers+signature+'\n{\nList<FilmComparison> comparisons = new();\n'+body+'}\n}\n'


def inspect(apk,app,payload,native,notices):
    sdk_names={'libSystem.Globalization.Native.so','libSystem.IO.Compression.Native.so','libSystem.Native.so',
      'libSystem.Security.Cryptography.Native.Android.so','libmono-component-debugger.so','libmono-component-hot_reload.so',
      'libmono-component-marshal-ilgen.so','libmonosgen-2.0.so','libmonodroid.so','libxamarin-debug-app-helper.so'}
    sdk_files={}
    for root in SDK_PACKS:
        for p in root.rglob('*.so'):
            if p.name in sdk_names or p.name=='libmono-android.debug.so':sdk_files.setdefault(sha(p),[]).append(str(p))
    expected={'lib/'+TARGETS[row['rid']][0]+'/'+Path(row['staged']).name:row['stagedSha256'] for row in native}
    rows=[]
    with zipfile.ZipFile(apk) as package:
        assert len(package.namelist())==len(set(package.namelist())), 'Duplicate APK entries'
        for name,digest in expected.items():assert hashlib.sha256(package.read(name)).hexdigest()==digest,name
        for name,digest in payload['references'].items():assert hashlib.sha256(package.read('assets/'+name)).hexdigest()==digest,name
        for row in notices:assert hashlib.sha256(package.read('assets/'+row['asset'])).hexdigest()==row['sha256']
        assert package.read('assets/payload.json')==(app/'payload.json').read_bytes()
        for name in package.namelist():
            if not name.startswith('lib/'):continue
            data=package.read(name);digest=hashlib.sha256(data).hexdigest()
            assert data[:6]==b'\x7fELF\x02\x01',name
            assert struct.unpack_from('<H',data,18)[0]==(183 if '/arm64-v8a/' in name else 62),name
            offset=struct.unpack_from('<Q',data,32)[0];size,count=struct.unpack_from('<HH',data,54)
            loads=[]
            for i in range(count):
                kind,flags,position,address,physical,file_size,memory_size,alignment=struct.unpack_from('<IIQQQQQQ',data,offset+size*i)
                if kind==1:
                    assert alignment>=16384 and position%16384==address%16384,name
                    loads.append(alignment)
            assert loads
            origin='reviewed renderer/dependency binary';paths=[]
            if Path(name).name in sdk_names:
                assert digest in sdk_files,name
                origin='installed .NET Android runtime pack';paths=sdk_files[digest]
            elif name not in expected:
                assert Path(name).name.startswith('lib_') or Path(name).name in {'libarc.bin.so','libxamarin-app.so'},name
                origin='SDK-generated managed assembly/data container'
            rows.append(dict(entry=name,sha256=digest,origin=origin,runtimePackPaths=paths,loadSegmentAlignment=loads))
    aapt=Path.home()/'Android/Sdk/build-tools/36.1.0/aapt'
    badging=subprocess.check_output([str(aapt),'dump','badging',str(apk)],text=True)
    (app/'apk-badging.txt').write_text(badging)
    assert "sdkVersion:'33'" in badging
    save(app/'apk-inspection.json',dict(passed=True,apk=str(apk),apkSha256=sha(apk),minimumApi=33,
        reviewedNativeEntries=len(expected),noticeAssets=len(notices),referenceAssets=len(payload['references']),
        nativeEntries=rows,nativeReleaseCleared=False,
        scope='Local SDK-built validation app; exact reviewed native payloads, notices and reference assets, external SDK runtime packs and generated containers. Not release or 16-KiB-page-device clearance.'))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--templates',type=Path,default=REPO/'tools/hosttests/AndroidHost')
    parser.add_argument('--native-review',type=Path,help='Reuse a matching native binary review, or inspect current native builds when omitted')
    parser.add_argument('--references',type=Path,required=True)
    parser.add_argument('--application-id',default='com.codebrix.cycles.hostvalidation',
                        help='Android package id; a separate id leaves an installed validation app untouched')
    args=parser.parse_args();out=args.output.resolve();templates=args.templates.resolve();refs=args.references.resolve()
    out.mkdir(parents=True,exist_ok=False);app=out/'app';app.mkdir()
    source_pin=inputs();pins={str(Path(__file__).resolve()):sha(Path(__file__))}
    state=dict(complete=False,phase='staging');save(out/'status.json',state)
    try:
        for name in ['AndroidHost.csproj','AndroidManifest.xml','MainActivity.cs','FilmComparison.cs','WebGpuChecks.cs']:
            path=templates/name
            assert path.is_file(),path
            shutil.copy2(path,app/path.name);pins[str(path)]=sha(path)
        # WebGPU validation scenes shared with the Linux managed tests.
        shared=REPO/'tools/hosttests/WebGpuScenes/WebGpuValidationScenes.cs'
        shutil.copy2(shared,app/shared.name);pins[str(shared)]=sha(shared)
        for project,name in [('HostLifecycle','HostChecks'),('ImageModes','ImageChecks')]:
            source=REPO/'tools/hosttests'/project/'Program.cs'
            (app/(name+'.cs')).write_text(checks(source,name));pins[str(source)]=sha(source)
        reference_status=json.loads((refs/'status.json').read_text());assert reference_status['passed'] and reference_status['independentReferenceFilms']==786
        reference_pins=json.loads((refs/'invocation.json').read_text())['referenceSha256']
        assert all(sha(Path(path))==digest for path,digest in reference_pins.items())
        pins.update(reference_pins)
        payload=dict(native={},references={});native=[]
        for rid,(abi,triple) in TARGETS.items():
            toolchain=AndroidToolchain(rid)
            if args.native_review is None:
                from android_host_gate import inspect as inspect_native
                review_folder=out/'native-review'/rid;review_folder.mkdir(parents=True)
                inspect_native(rid,review_folder)
            else:
                review_folder=args.native_review/rid
            review=json.loads((review_folder/'binary-review.json').read_text());assert review['passed']
            host=REPO/'native/work/android'/rid/'host';build=host/'build'
            assert sha(host/'invocation.json')==review['buildInvocationSha256']
            assert sha(host/'status.json')==review['buildStatusSha256']
            current=json.loads((host/'invocation.json').read_text())
            assert toolchain.identity()==current['toolchain']
            assert all(sha(REPO/name)==digest for name,digest in current['sourceSha256'].items())
            reviewed={row['path']:row['sha256'] for row in review['files']}
            prefix=host.parent/'dependencies/install'
            files=sorted({p.resolve() for p in (prefix/'lib').glob('*.so')})
            files += [build/'lib/libcodebrix_cycles_host.so',toolchain.runtime,PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so']
            assert len(files)==28 and len({p.name for p in files})==28
            folder=app/'native'/abi;folder.mkdir(parents=True);payload['native'][abi]={}
            for path in files:
                assert sha(path)==reviewed[str(path.resolve())],path
                destination=folder/path.name;shutil.copy2(path,destination);command=None
                if path.name!='libcodebrix_wgpu.so':
                    command=[str(toolchain.bin/'llvm-strip'),'--strip-debug',str(destination)]
                    subprocess.run(command,check=True)
                native.append(dict(rid=rid,source=str(path),sourceSha256=sha(path),staged=str(destination),stagedSha256=sha(destination),stripCommand=command))
                pins[str(path)]=sha(path);payload['native'][abi][path.name]=sha(destination)
        for path,digest in reference_pins.items():
            path=Path(path)
            relative=path.relative_to(refs)
            if relative.parts[0]=='cpu':name=Path('reference')/relative
            elif relative.parts[0]=='references':name=Path('reference')/Path(*relative.parts[1:])
            else:continue
            target=app/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
            payload['references'][str(name)]=digest
        notices=[]
        manifest=json.loads((REPO/'third_party/dependencies.json').read_text())
        local_notices={name for component in manifest['components'] for name in component['notices']}
        local_notices.update(['LICENSE','NOTICE','THIRD-PARTY-NOTICES.txt'])
        notice_sources=[(REPO/name,Path('notices')/name) for name in sorted(local_notices)]
        for root in SDK_PACKS:
            for name in ['LICENSE.TXT','THIRD-PARTY-NOTICES.TXT']:
                path=root/name
                if path.exists():notice_sources.append((path,Path('notices/dotnet')/root.relative_to('/usr/share/dotnet/packs')/name))
        notice_sources += [(Path('/usr/share/dotnet')/name,Path('notices/dotnet')/name) for name in ['LICENSE.txt','ThirdPartyNotices.txt']]
        notice_sources.append((AndroidToolchain('android-arm64').root/'NOTICE',Path('notices/ndk-NOTICE.txt')))
        for path,name in notice_sources:
            target=app/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
            pins[str(path)]=sha(path);notices.append(dict(source=str(path),asset=str(name),sha256=sha(path)))
        save(app/'payload.json',payload);save(app/'native-inputs.json',dict(files=native,newExternalImplementationImported=False,nativeReleaseCleared=False))
        save(app/'notice-inputs.json',dict(files=notices))
        staged={str(p):sha(p) for p in app.rglob('*') if p.is_file()}
        command=['dotnet','build',str(app/'AndroidHost.csproj'),'-c','Debug','--configfile',str(REPO/'tools/hosttests/NuGet.Config'),'--nologo',
            '-p:CodeBrixSourceRoot='+str(REPO),'-p:JavaSdkDirectory=/usr/lib/jvm/java-21-openjdk-amd64',
            '-p:AndroidSdkDirectory='+str(Path.home()/'Android/Sdk'),'-p:ApplicationId='+args.application_id]
        save(out/'invocation.json',dict(sourceSha256=source_pin,inputSha256=pins,stagedSha256=staged,command=command))
        state['phase']='build';save(out/'status.json',state)
        with (out/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
        apk=app/'bin/Debug/net10.0-android'/(args.application_id+'-Signed.apk')
        state['phase']='inspect';save(out/'status.json',state);inspect(apk,app,payload,native,notices)
        assert inputs()==source_pin
        assert all(sha(Path(path))==digest for path,digest in {**pins,**staged}.items())
        state.update(complete=True,passed=True,phase='complete',applicationId=args.application_id,apk=str(apk),apkSha256=sha(apk),nativePayloads=len(native),noticeAssets=len(notices),references=len(payload['references']))
        print(json.dumps(state),flush=True)
    except Exception as error:
        state.update(complete=True,passed=False,error=repr(error));raise
    finally:save(out/'status.json',state)


if __name__=='__main__':main()
