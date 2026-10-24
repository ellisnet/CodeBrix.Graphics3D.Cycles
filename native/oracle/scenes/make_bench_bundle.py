#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Generate the bench oracle bundle locally and verify it against the committed scene files.

    python3 native/oracle/scenes/make_bench_bundle.py --output DIR [--model PATH/TO/bench_01.gltf] [--tool EXE]

The committed files are native/oracle/scenes/assets/bench_01.json, bench_01-hdri.json and
manifest.json (expected SHA-256 of every generated raw file). The raw geometry, attribute and
pixel files are derived from the bench asset in native/oracle/scenes/assets/ForestVillage/ and
are generated, not committed. This
script runs the glTF example tool (tools/gltf/CycleGltfRender; it decodes the textures with
CodeBrix.Imaging) and native/oracle/scenes/author_environment.py into DIR, requires the scene
documents to be byte-identical to the committed ones and every raw file to match the manifest,
and leaves DIR/bench_01.json and DIR/bench_01-hdri.json ready for codebrix_cycles_oracle. If a
raw file does not match (another decoder build), copy a verified bundle directory instead (see
native/oracle/README.md).
"""
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[3];ASSETS=Path(__file__).resolve().parent/'assets'
TOOL_PROJECT=REPO/'tools/gltf/CycleGltfRender/CodeBrix.Graphics3D.Cycles.GltfRender.csproj'


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--model',type=Path,default=REPO/'native/oracle/scenes/assets/ForestVillage/CommunityProps/bench_01.gltf',help='bench_01.gltf (with bench_01.bin beside it and ../Textures); default: the asset in the repository')
    parser.add_argument('--output',type=Path,required=True,help='a new directory (outside tracked paths)')
    parser.add_argument('--tool',type=Path,help='a built CodeBrix.Graphics3D.Cycles.GltfRender executable (default: dotnet run of the project)')
    args=parser.parse_args()
    manifest=json.loads((ASSETS/'manifest.json').read_text())
    if args.output.exists():raise SystemExit(f'make_bench_bundle.py: {args.output} exists; use a new directory')
    work=args.output/'tool';command=[str(args.tool)] if args.tool else ['dotnet','run','--project',str(TOOL_PROJECT),'-c','Release','--']
    command+= [str(args.model),*manifest['toolArguments'],'--write-bundle',str(work)]
    print(' '.join(command),flush=True);subprocess.run(command,check=True)
    for name in sorted(p.name for p in work.iterdir()):
        target=args.output/('bench_01.json' if name=='scene.json' else name);shutil.move(str(work/name),target)
    work.rmdir()
    subprocess.run([sys.executable,str(Path(__file__).resolve().parent/'author_environment.py'),'--bundle',str(args.output),'--scene','bench_01.json'],check=True)
    problems=[]
    for name in ['bench_01.json','bench_01-hdri.json']:
        if (args.output/name).read_bytes()!=(ASSETS/name).read_bytes():problems.append(f'{name} differs from the committed {ASSETS/name}')
    for name,digest in manifest['files'].items():
        path=args.output/name
        if not path.is_file():problems.append(f'missing {name}')
        elif sha(path)!=digest:problems.append(f'{name}: SHA-256 {sha(path)} differs from the manifest {digest}')
    (args.output/'verification.json').write_text(json.dumps(dict(passed=not problems,problems=problems,decoder=manifest.get('decoder')),indent=2)+'\n')
    if problems:
        print('make_bench_bundle.py: the generated bundle does not match the committed manifest:\n  '+'\n  '.join(problems),file=sys.stderr);return 1
    print(f'bundle verified: {args.output}/bench_01.json and bench_01-hdri.json ({len(manifest["files"])} raw files)')
    return 0


if __name__=='__main__':sys.exit(main())
