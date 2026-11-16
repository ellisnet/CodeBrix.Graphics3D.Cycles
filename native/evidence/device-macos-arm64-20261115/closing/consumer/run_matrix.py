#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Scratch consumer matrix for the MacOSArm64 package (git-ignored, native/work/).

    python3 run_matrix.py --phase NAME --version V --feed DIR --payload STAGE [--modes a,b] [--negative-control]

Each mode builds a fresh copy of app/ against the local feed (NuGet.Config: the feed for
CodeBrix.Graphics3D.Cycles.*, nuget.org only for Microsoft.NETCore.App.Runtime.osx-arm64),
records the output layout of the Metal kernel tree, and runs the app with a fresh Cycles
kernel cache (XDG_CACHE_HOME), so Metal compiles from the shipped kernel sources.
"""
import argparse,hashlib,json,os,re,shutil,subprocess,time
from pathlib import Path
HERE=Path(__file__).resolve().parent
HOST='libcodebrix_cycles_host.dylib'
MODES={
 'build':(['dotnet','build','-c','Release'],'bin/Release/net10.0',['dotnet','bin/Release/net10.0/Consumer.dll']),
 'run':(None,'bin/Release/net10.0',['dotnet','run','-c','Release']),
 'publish':(['dotnet','publish','-c','Release'],'bin/Release/net10.0/publish',['dotnet','bin/Release/net10.0/publish/Consumer.dll']),
 'publish-rid':(['dotnet','publish','-c','Release','-r','osx-arm64','--self-contained','false'],'bin/Release/net10.0/osx-arm64/publish',['bin/Release/net10.0/osx-arm64/publish/Consumer']),
 # The SDK also downloads the ASP.NET Core runtime pack for a self-contained app by default (transitive framework
 # references); only the .NET runtime pack is approved for download, and this console app uses no other framework.
 'publish-self-contained':(['dotnet','publish','-c','Release','-r','osx-arm64','--self-contained','-p:DisableTransitiveFrameworkReferenceDownloads=true'],'bin/Release/net10.0/osx-arm64/publish',['bin/Release/net10.0/osx-arm64/publish/Consumer']),
}
CONFIG='''<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <clear />
    <add key="local" value="{feed}" />
    <add key="nuget.org" value="https://api.nuget.org/v3/index.json" />
  </packageSources>
  <packageSourceMapping>
    <packageSource key="local"><package pattern="CodeBrix.Graphics3D.Cycles.*" /></packageSource>
    <packageSource key="nuget.org"><package pattern="Microsoft.NETCore.App.Runtime.osx-arm64" /></packageSource>
  </packageSourceMapping>
</configuration>
'''


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def environment(cache):
 env={k:v for k,v in os.environ.items() if k not in ('CYCLES_KERNEL_PATH',) and not k.startswith('DYLD_')}
 env.update(NUGET_PACKAGES=str(HERE/'packages'),NUGET_HTTP_CACHE_PATH=str(HERE/'http-cache'),DOTNET_CLI_TELEMETRY_OPTOUT='1',
  DOTNET_NOLOGO='1',DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER='1',MSBUILDDISABLENODEREUSE='1',XDG_CACHE_HOME=str(cache))
 return env


def call(command,cwd,env,log,timeout=1800):
 start=time.time();result=subprocess.run(command,cwd=cwd,env=env,capture_output=True,text=True,timeout=timeout)
 log.write_text(f'$ {" ".join(command)}\n(exit {result.returncode}, {time.time()-start:.1f} s)\n--- stdout\n{result.stdout}\n--- stderr\n{result.stderr}')
 return result,round(time.time()-start,1)


def diagnostics(text):
 return sorted(set(re.sub(r'\s*\[[^\]]*\.csproj[^\]]*\]$','',line.strip()) for line in text.splitlines() if re.search(r'\b(warning|error) [A-Z]+[0-9]+',line)))


def layout(output,kernel):
 """Where the host library and the kernel tree landed in OUTPUT."""
 hosts=sorted(str(p.relative_to(output)) for p in output.rglob(HOST))
 basenames={Path(n).name for n in kernel}
 rows=[]
 for host in hosts:
  directory=(output/host).parent;tree=directory/'codebrix-cycles/source'
  present={n:sha(tree/n) for n in kernel if (tree/n).is_file()}
  extra=[str(p.relative_to(tree)) for p in tree.rglob('*') if p.is_file() and str(p.relative_to(tree)) not in kernel] if tree.exists() else []
  flat=sorted(p.name for p in directory.iterdir() if p.is_file() and p.name in basenames)
  state='beside' if len(present)==len(kernel) and all(present[n]==d for n,d in kernel.items()) and not extra else ('absent' if not present else 'partial')
  rows.append(dict(host=host,kernelTree=state,kernelFilesBeside=len(present),kernelFilesMatching=sum(present[n]==kernel[n] for n in present),
   extraFiles=len(extra),flattenedBesideHost=len(flat),flattenedNames=flat[:30]))
 return dict(hostCopies=hosts,rows=rows,uniqueKernelBasenames=len(basenames),kernelFiles=len(kernel))


def renders(stdout):
 rows=[]
 for line in stdout.splitlines():
  try:rows.append(json.loads(line))
  except ValueError:pass
 return rows


def main():
 p=argparse.ArgumentParser();p.add_argument('--phase',required=True);p.add_argument('--version',required=True)
 p.add_argument('--feed',type=Path,required=True);p.add_argument('--payload',type=Path,required=True)
 p.add_argument('--modes',default=','.join(MODES));p.add_argument('--negative-control',action='store_true')
 a=p.parse_args()
 provenance=json.loads((a.payload/'provenance.json').read_text())
 prefix='runtime/codebrix-cycles/source/'
 # A scratch stage lists payloadFiles; the committed payload lists approvedPayload.
 files=provenance.get('payloadFiles') or provenance['approvedPayload']
 kernel={n[len(prefix):]:d for n,d in files.items() if n.startswith(prefix)}
 phase=HERE/'runs'/a.phase;phase.mkdir(parents=True,exist_ok=True);results={}
 for mode in a.modes.split(','):
  build,out,run=MODES[mode];root=phase/mode
  if root.exists():shutil.rmtree(root)
  app=root/'app';shutil.copytree(HERE/'app',app)
  (app/'Consumer.csproj').write_text((app/'Consumer.csproj').read_text().replace('$(CyclesPackageVersion)',a.version))
  (app/'NuGet.Config').write_text(CONFIG.format(feed=a.feed.resolve()))
  env=environment(root/'cycles-cache');entry=dict(mode=mode)
  if build:
   result,seconds=call(build,app,env,root/'build.log');entry.update(buildCommand=' '.join(build),buildExit=result.returncode,buildSeconds=seconds,
    buildDiagnostics=diagnostics(result.stdout+result.stderr))
  if not build or result.returncode==0:
   result,seconds=call(run,app,env,root/'run.log');entry.update(runCommand=' '.join(run),runExit=result.returncode,runSeconds=seconds,
    renders=renders(result.stdout))
   if not build:entry['buildDiagnostics']=diagnostics(result.stdout+result.stderr)
  if (app/out).exists():entry['layout']=layout(app/out,kernel)
  entry['outputFiles']=sum(1 for f in (app/out).rglob('*') if f.is_file()) if (app/out).exists() else 0
  results[mode]=entry
  print(json.dumps(dict(mode=mode,buildExit=entry.get('buildExit'),runExit=entry.get('runExit'),
   layout=[(r['host'],r['kernelTree'],r['kernelFilesBeside'],r['flattenedBesideHost']) for r in entry.get('layout',{}).get('rows',[])],
   renders=[(r.get('label'),r.get('ok'),r.get('seconds'),r.get('error')) for r in entry.get('renders',[]) if r.get('kind')=='render']),indent=None),flush=True)
 if a.negative_control:
  # A copy of the build output without the kernel tree: Metal must fail and the CPU must render.
  source=phase/'build/app/bin/Release/net10.0';copy=phase/'negative-control/bin'
  if copy.parent.exists():shutil.rmtree(copy.parent)
  shutil.copytree(source,copy);shutil.rmtree(copy/'runtimes/osx-arm64/native/codebrix-cycles')
  result,seconds=call(['dotnet',str(copy/'Consumer.dll')],copy,environment(phase/'negative-control/cycles-cache'),phase/'negative-control/run.log')
  results['negative-control']=dict(removed='runtimes/osx-arm64/native/codebrix-cycles',runExit=result.returncode,renders=renders(result.stdout))
  print(json.dumps(dict(mode='negative-control',runExit=result.returncode,renders=[(r.get('label'),r.get('ok'),r.get('error')) for r in results['negative-control']['renders'] if r.get('kind')=='render'])),flush=True)
 previous=json.loads((phase/'results.json').read_text()) if (phase/'results.json').exists() else {}
 previous.update(results);(phase/'results.json').write_text(json.dumps(previous,indent=1)+'\n')


if __name__=='__main__':main()
