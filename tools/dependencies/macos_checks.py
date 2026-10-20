#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Validation-only checks for the macOS recipes: Homebrew isolation, linked
libraries and Mach-O platform data. These never change build outputs."""
import json,re,shutil,subprocess
from pathlib import Path
EXCLUDED_PREFIXES=['/opt/homebrew','/usr/local']


def tool(*command):return subprocess.check_output(command,text=True,stderr=subprocess.STDOUT).strip()


def excluded(text):return [p for p in EXCLUDED_PREFIXES if re.search(re.escape(p)+r'(/|\b)',text)]


def cmake_paths():
 """CMake's own executables and modules, which may live in an excluded prefix."""
 paths=set()
 for name in ['cmake','ctest','cpack','ccmake']:
  found=shutil.which(name)
  if found:paths.update([found,str(Path(found).resolve())])
 return paths


def check_isolation(build):
 """Reject any excluded prefix outside CMake's own program/module paths."""
 cache=build/'CMakeCache.txt';problems=[];own=cmake_paths()
 root=re.search(r'^CMAKE_ROOT:INTERNAL=(.*)$',cache.read_text(),re.M)
 if root:own.add(root.group(1))
 own=sorted(own,key=len,reverse=True)
 allowed=('CMAKE_IGNORE_PATH','CMAKE_IGNORE_PREFIX_PATH')
 for line in cache.read_text().splitlines():
  if line.startswith(('//','#')):continue
  for path in own:line=line.replace(path,'<cmake>')
  if not excluded(line):continue
  if line.split(':',1)[0].split('=',1)[0] in allowed:continue
  # NOTFOUND results that merely name an ignored search path are harmless.
  if line.endswith('-NOTFOUND'):continue
  problems.append(line)
 commands=build/'compile_commands.json'
 if commands.exists():
  for row in json.loads(commands.read_text()):
   if excluded(row.get('command','')+' '.join(row.get('arguments',[]))):problems.append(row['file'])
 if problems:raise RuntimeError(f'Excluded prefix referenced by {build.name}: {problems[:5]}')


def linked(path):
 lines=tool('otool','-L',str(path)).splitlines()[1:]
 return [line.strip().split(' (')[0] for line in lines]


def check_binary(path,deployment_target):
 for name in linked(path):
  if not (name.startswith('@rpath/') or name.startswith('/usr/lib/') or name.startswith('/System/Library/')):
   raise RuntimeError(f'{path.name} links outside the reviewed prefix and the OS: {name}')
 build=tool('otool','-l',str(path))
 minos=re.findall(r'minos (\S+)',build)
 if minos and any(v!=deployment_target for v in minos):raise RuntimeError(f'{path.name} has minimum OS {minos}')
 if 'arm64' not in tool('lipo','-archs',str(path)).split():raise RuntimeError(path.name+' is not arm64')


