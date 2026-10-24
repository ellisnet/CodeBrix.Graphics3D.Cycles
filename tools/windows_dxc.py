# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""The pinned DirectX Shader Compiler for the Windows RIDs (third_party/dxc-import.json).

dxcompiler.dll is an unchanged, Microsoft-signed runtime binary stored as a
reviewed payload file in native/binaries/<rid>/runtime/ (the only place the
source audit admits compiled files), next to its license text. The Windows
recipes copy it beside the host library, where the WebGPU device finds it.
"""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
IMPORT=ROOT/'third_party/dxc-import.json'
VERSION='1.9.2609.5'
DLL={'win-x64':'0689e2fee0a1c9df8662a59680469f24e2a3ad7ce6bbbcc9284f7a83172bfc59',
     'win-arm64':'ec234595c270a908da183eb04f1f5a1a667d3ce81e69f5bc55370503dab48373'}
LICENSE_NAME='LICENSE-DirectXShaderCompiler.txt'
LICENSE_SHA256='729615317e28dd03907e46f0fc3b5e88f7853cee61d1a1471d2749335516b46f'


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def files(rid):
 """The verified DXC DLL and license text for a Windows RID."""
 runtime=ROOT/'native/binaries'/rid/'runtime';dll=runtime/'dxcompiler.dll';text=runtime/LICENSE_NAME
 record={row['path']:row['sha256'] for row in json.loads(IMPORT.read_text())['files']}
 for path,digest in [(dll,DLL[rid]),(text,LICENSE_SHA256)]:
  name=path.relative_to(ROOT).as_posix()
  if record.get(name)!=digest:raise RuntimeError('DXC import record does not pin '+name)
  if not path.is_file() or sha(path)!=digest:raise RuntimeError('The pinned DXC file is missing or changed: '+name)
 return dict(dll=dll,license=text,version=VERSION,sha256=DLL[rid])
