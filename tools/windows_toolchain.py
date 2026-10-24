# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""External Windows toolchain shared by the win-x64 and win-arm64 recipes.

Visual Studio (2022 or later) with its MSVC tools, Windows SDK and bundled
LLVM (clang-cl, lld-link) is the agreed external toolchain; CMake and Ninja are
the copies Visual Studio ships. The build environment is the one vcvarsall.bat
sets up, with every other compiler distribution removed from PATH: Strawberry
Perl, LLVM-MinGW, MSYS2/MinGW, Cygwin and Git's Unix tools (whose link.exe would
shadow the linker). EXCLUDED names them; the recipes reject any reference to them
in configured caches, compile commands and linked libraries, as the macOS recipes
do for Homebrew. Validation helpers here never change build outputs.

Every Windows recipe runs Python in UTF-8 mode (python -X utf8), so text files and
tool output are read as UTF-8 as on Linux and macOS.
"""
import hashlib,json,os,platform,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
# RID -> (vcvarsall argument on an x64 or ARM64 host, CMake processor, clang target, MSVC lib folder)
TARGETS={'win-x64':dict(vcvars={'AMD64':'x64','ARM64':'arm64_amd64'},processor='AMD64',target='x86_64-pc-windows-msvc',lib='x64'),
         'win-arm64':dict(vcvars={'AMD64':'x64_arm64','ARM64':'arm64'},processor='ARM64',target='aarch64-pc-windows-msvc',lib='arm64')}
# Folders (never file names: Boost has predef/os/cygwin.h), matched with either slash.
EXCLUDED=[r'C:\Strawberry\\',r'\\llvm-mingw[^\\]*\\',r'\\msys64\\',r'\\mingw(32|64)\\',r'\\cygwin(64)?\\',r'\\Git\\usr\\',r'\\Git\\mingw64\\']


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def excluded(text):
 """Excluded distribution folders named in text (case-insensitive, either slash)."""
 normal=text.replace('/','\\')+'\\'
 return [p for p in EXCLUDED if re.search(p.replace('C:\\','C:\\\\',1) if p.startswith('C:') else p,normal,re.I)]


def require_windows():
 if platform.system()!='Windows':raise SystemExit('This recipe runs only on Windows; this host is '+platform.system())
 if platform.machine() not in ('AMD64','ARM64'):raise SystemExit('Unsupported Windows build host '+platform.machine())
 # UTF-8 mode (python -X utf8, or PYTHONUTF8=1 without -I, which ignores PYTHON* variables).
 if not sys.flags.utf8_mode:raise SystemExit('Run the Windows recipes in Python UTF-8 mode (python -X utf8 ...), so text is read as on Linux and macOS')


def visual_studio():
 vswhere=Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
 if not vswhere.exists():raise RuntimeError('vswhere.exe not found; install Visual Studio 2022 or later')
 rows=json.loads(subprocess.check_output([str(vswhere),'-products','*','-version','[17.0,','-sort','-format','json',
  '-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','Microsoft.VisualStudio.Component.VC.Llvm.Clang'],text=True))
 rows=[r for r in rows if (Path(r['installationPath'])/'VC/Tools/Llvm').is_dir()]
 if not rows:raise RuntimeError('No Visual Studio with the MSVC tools and the bundled LLVM (C++ Clang tools) is installed')
 row=rows[0];return dict(path=Path(row['installationPath']),version=row['installationVersion'],name=row['displayName'])


# vcvarsall.bat runs once per process and RID: inside its own environment (which a
# recipe may adopt for child processes) it does not set itself up again.
_ENVIRONMENTS={}


class Toolchain:
 """The Visual Studio developer environment for one target RID."""
 def __init__(self,rid):
  if rid not in TARGETS:raise ValueError('Unknown Windows RID '+rid)
  self.rid=rid;self.spec=TARGETS[rid];self.vs=visual_studio();vs=self.vs['path']
  host=platform.machine();self.host=host
  llvm=vs/'VC/Tools/Llvm'/('ARM64' if host=='ARM64' else 'x64')/'bin'
  self.clang_cl=llvm/'clang-cl.exe';self.clangxx=llvm/'clang++.exe';self.lld_link=llvm/'lld-link.exe'
  self.llvm_lib=llvm/'llvm-lib.exe';self.llvm_rc=llvm/'llvm-rc.exe';self.readobj=llvm/'llvm-readobj.exe';self.dlltool=llvm/'llvm-dlltool.exe'
  ide=vs/'Common7/IDE/CommonExtensions/Microsoft/CMake'
  self.cmake=ide/'CMake/bin/cmake.exe';self.ninja=ide/'Ninja/ninja.exe'
  for tool in [self.clang_cl,self.clangxx,self.lld_link,self.llvm_lib,self.llvm_rc,self.readobj,self.dlltool,self.cmake,self.ninja]:
   if not tool.exists():raise RuntimeError('Visual Studio component missing: '+str(tool))
  self._env=None

 def environment(self,jobs=None):
  """vcvarsall.bat's environment with excluded distributions removed from PATH."""
  if self._env is None and self.rid in _ENVIRONMENTS:self._env=_ENVIRONMENTS[self.rid]
  if self._env is None:
   vcvars=self.vs['path']/'VC/Auxiliary/Build/vcvarsall.bat'
   output=subprocess.run(f'"{vcvars}" {self.spec["vcvars"][self.host]} >nul && set',shell=True,capture_output=True,text=True,check=True).stdout
   env={}
   for line in output.splitlines():
    if '=' in line and not line.startswith('='):k,v=line.split('=',1);env[k]=v
   if 'VCToolsInstallDir' not in env or 'WindowsSdkDir' not in env:raise RuntimeError('vcvarsall.bat did not set up the MSVC environment')
   keep=[p for p in env.get('Path',env.get('PATH','')).split(';') if p and not excluded(p)]
   for name in ['Path','PATH']:env.pop(name,None)
   env['PATH']=';'.join([str(self.cmake.parent),str(self.ninja.parent),str(self.clang_cl.parent),*keep])
   for name in ['CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','CMAKE_PREFIX_PATH','PKG_CONFIG_PATH','PYTHONPATH',
                'CFLAGS','CXXFLAGS','LDFLAGS','CC','CXX','CL','_CL_','LINK','_LINK_']:env.pop(name,None)
   env['PYTHONUTF8']='1'
   bad=[k+'='+v for k,v in env.items() if k.upper() in ('INCLUDE','LIB','LIBPATH') and excluded(v)]
   if bad:raise RuntimeError('The MSVC environment names an excluded distribution: '+'; '.join(bad))
   self._env=_ENVIRONMENTS[self.rid]=env
  env=dict(self._env)
  if jobs:env['CMAKE_BUILD_PARALLEL_LEVEL']=str(jobs)
  return env

 def tool(self,*command):return subprocess.check_output([str(c) for c in command],text=True,stderr=subprocess.STDOUT,env=self.environment()).strip()

 def identity(self):
  env=self.environment()
  return dict(rid=self.rid,buildHost=self.host+'-Windows',visualStudio=self.vs['name']+' '+self.vs['version'],
   msvcToolsVersion=env.get('VCToolsVersion'),windowsSdkVersion=env.get('WindowsSDKVersion','').strip('\\'),
   compilerVersion=self.tool(self.clang_cl,'--version').splitlines()[0],compilerSha256=sha(self.clang_cl),linkerSha256=sha(self.lld_link),
   cmakeVersion=self.tool(self.cmake,'--version').splitlines()[0],cmakeSha256=sha(self.cmake),ninjaVersion=self.tool(self.ninja,'--version'),
   target=self.spec['target'])

 def cmake_options(self):
  """Compiler and generator settings every Windows configure step uses."""
  slash=lambda p:str(p).replace('\\','/')
  return {'CMAKE_MAKE_PROGRAM':slash(self.ninja),'CMAKE_C_COMPILER':slash(self.clang_cl),'CMAKE_CXX_COMPILER':slash(self.clang_cl),
   'CMAKE_LINKER':slash(self.lld_link),'CMAKE_AR':slash(self.llvm_lib),'CMAKE_RC_COMPILER':slash(self.llvm_rc),
   'CMAKE_C_COMPILER_TARGET':self.spec['target'],'CMAKE_CXX_COMPILER_TARGET':self.spec['target'],
   # Hybrid CRT: the C++ runtime is linked into each DLL and the Universal CRT
   # (ucrtbase.dll, part of Windows 10 and 11) is shared. No Visual C++
   # Redistributable is needed, and every DLL allocates from the same heap.
   'CMAKE_POLICY_DEFAULT_CMP0091':'NEW','CMAKE_MSVC_RUNTIME_LIBRARY':'MultiThreaded',
   'CMAKE_EXE_LINKER_FLAGS':HYBRID_CRT,'CMAKE_SHARED_LINKER_FLAGS':HYBRID_CRT,'CMAKE_MODULE_LINKER_FLAGS':HYBRID_CRT,
   'CMAKE_IGNORE_PREFIX_PATH':';'.join(slash(p) for p in installed_excluded()),
   'CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH':'OFF',
   # Only a real cross build names the target system: CMake treats any explicit
   # CMAKE_SYSTEM_NAME as cross-compiling (no oracle runner, no try_run).
   **({} if self.host==self.spec['processor'] else {'CMAKE_SYSTEM_NAME':'Windows','CMAKE_SYSTEM_PROCESSOR':self.spec['processor']})}


HYBRID_CRT='/NODEFAULTLIB:libucrt.lib ucrt.lib /Brepro'
# DLLs a Windows binary may import besides the reviewed prefix: Windows itself
# (including the Universal CRT and its api-ms-win-* contracts).
SYSTEM_DLL=re.compile(r'^(api-ms-win-.*|ext-ms-win-.*|ucrtbase|kernel32|kernelbase|user32|gdi32|advapi32|shell32|ole32|oleaut32|ws2_32|'
 r'bcrypt|crypt32|shlwapi|dbghelp|version|psapi|rpcrt4|ntdll|secur32|userenv|iphlpapi|winmm|setupapi|cfgmgr32|comdlg32|imm32|d3d12|dxgi|dxcore|'
 r'opengl32|vulkan-1|synchronization)\.dll$',re.I)
# Visual C++ runtime DLLs that the hybrid CRT must never import.
VC_RUNTIME=re.compile(r'^(vcruntime\d+.*|msvcp\d+.*|concrt\d+|vcomp\d+|ucrtbased|vcruntime\d+d)\.dll$',re.I)


def installed_excluded():
 roots=[]
 for p in os.environ.get('PATH','').split(';'):
  if p and excluded(p):roots.append(Path(p))
 for fixed in [r'C:\Strawberry',r'C:\msys64',r'C:\cygwin64']:
  if Path(fixed).is_dir():roots.append(Path(fixed))
 return sorted(set(roots))


def imports(toolchain,path):
 """Names of the DLLs a PE file imports (normal and delay-load)."""
 text=toolchain.tool(toolchain.readobj,'--coff-imports',str(path))
 return sorted(set(re.findall(r'^\s*Name:\s*(\S+\.dll)\s*$',text,re.M|re.I)),key=str.lower)


def machine(toolchain,path):
 text=toolchain.tool(toolchain.readobj,'--file-headers',str(path))
 found=re.search(r'Machine:\s*IMAGE_FILE_MACHINE_(\w+)',text)
 return found.group(1) if found else None


def check_binary(toolchain,path,allowed=()):
 """Imports only Windows, the given DLLs (the reviewed prefix) and nothing from
 the Visual C++ runtime; the machine type matches the target."""
 allowed={a.lower() for a in allowed};problems=[]
 for name in imports(toolchain,path):
  if VC_RUNTIME.match(name):problems.append('Visual C++ runtime '+name)
  elif not (SYSTEM_DLL.match(name) or name.lower() in allowed):problems.append('unreviewed '+name)
 want={'win-x64':'AMD64','win-arm64':'ARM64'}[toolchain.rid];got=machine(toolchain,path)
 if got!=want:problems.append(f'machine {got}, expected {want}')
 if problems:raise RuntimeError(f'{Path(path).name}: '+'; '.join(problems))


def check_isolation(build):
 """Reject excluded distributions in a configured cache or compile database."""
 cache=Path(build)/'CMakeCache.txt';problems=[]
 for line in cache.read_text(errors='replace').splitlines():
  if line.startswith(('//','#')) or not excluded(line):continue
  if line.split(':',1)[0].split('=',1)[0] in ('CMAKE_IGNORE_PREFIX_PATH','CMAKE_IGNORE_PATH'):continue
  if line.endswith('-NOTFOUND'):continue
  problems.append(line)
 commands=Path(build)/'compile_commands.json'
 if commands.exists():
  for row in json.loads(commands.read_text()):
   if excluded(row.get('command','')+' '.join(row.get('arguments',[]))):problems.append(row['file'])
 if problems:raise RuntimeError(f'Excluded distribution referenced by {Path(build).name}: {problems[:5]}')
