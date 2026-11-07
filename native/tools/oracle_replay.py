#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Replay of an oracle scene: records the native scheduler's commands of one committed scene on Linux and replays them against the scene's independent CPU render buffer.

  oracle_replay.py capture --scene volumes/absorption-point --out DIR [--start-sample 1]   CPU reference render, WebGPU render with the capture on, reference copied beside the capture
  oracle_replay.py replay --capture DIR --out DIR2 [--rid android-arm64 --serial S]       native/tools/render_replay.py on that capture

A replay needs `render_replay.py build --rid RID` first. A scene whose arena still grows after the start sample is retried from later samples, never a partial capture; a scene that does not reach its last sample leaves no capture, which is reported. The reference is the x64 CPU render, so an ARM64 replay of a scene whose two CPUs differ needs the ARM64 film instead (--reference-film)."""
import argparse, json, os, shutil, subprocess, sys
from pathlib import Path
NATIVE = Path(__file__).resolve().parents[1]; BIN = NATIVE/'work/cycles-build/bin/codebrix_cycles_oracle'; SCENES = NATIVE/'oracle/scenes'


def capture(args):
    out = args.out.resolve(); out.mkdir(parents=True); scene = SCENES/(args.scene+'.json'); doc = json.loads(scene.read_text()); r = doc['render']; w, h, n = r['width'], r['height'], r['samples']
    if n <= args.start_sample: raise SystemExit(f'{args.scene}: {n} samples, nothing after sample {args.start_sample} to capture')
    cpu = out/'cpu'; cpu.mkdir()
    subprocess.run([str(BIN), '--scene', str(scene), '--backend', 'cpu', '--label', 'cpu', '--output', str(cpu/'o'), '--threads', str(args.threads)], check=True, capture_output=True,
                   env=dict(os.environ, CODEBRIX_CYCLES_RENDER_BUFFER_DUMP=str(cpu)))
    reference = args.reference_film or cpu/f's{n}.bin'
    if not Path(reference).exists(): raise SystemExit(f'{args.scene}: the CPU render wrote no buffer for sample {n} ({sorted(p.name for p in cpu.glob("s*.bin"))})')
    for start in range(args.start_sample, min(n, args.start_sample+6)):
        (out/'capture-error.txt').unlink(missing_ok=True); shutil.rmtree(out/'wgsl', ignore_errors=True)
        e = dict(os.environ, CODEBRIX_CYCLES_RENDER_CAPTURE=str(out), CODEBRIX_CYCLES_RENDER_CAPTURE_SIZE=f'{w}x{h}x{n}x{start}', MESA_SHADER_CACHE_DIR=os.environ.get('MESA_SHADER_CACHE_DIR') or str(out/'mesa'))
        run = subprocess.run([str(BIN), '--scene', str(scene), '--backend', 'webgpu', '--label', 'capture', '--output', str(out/'wgsl'), '--threads', str(args.threads)], capture_output=True, text=True, errors='replace', env=e)
        (out/'capture.stderr.txt').write_text(run.stderr)
        if run.returncode: raise SystemExit(f'{args.scene}: WebGPU render failed: {run.stderr.strip()[-400:]}')
        if not (out/'capture-error.txt').exists(): break
    else: raise SystemExit(f'{args.scene}: capture refused from every start sample tried: '+(out/'capture-error.txt').read_text().strip())
    if not (out/'run.capture.json').exists(): raise SystemExit(f'{args.scene}: the capture did not finish (a render that stops before its last sample has no replay)')
    shutil.copy2(reference, out/'run.cpu.film.bin')
    shutil.rmtree(out/'mesa', ignore_errors=True); shutil.rmtree(out/'wgsl', ignore_errors=True); shutil.rmtree(cpu, ignore_errors=True)
    print(out, json.loads((out/'run.capture.json').read_text())['commands'], 'commands')


def replay(args):
    cmd = [sys.executable, str(NATIVE/'tools/render_replay.py'), 'run', '--rid', args.rid, '--capture', str(args.capture), '--output', str(args.out), '--timeout', str(args.timeout)]
    if args.serial: cmd += ['--serial', args.serial]
    raise SystemExit(subprocess.run(cmd).returncode)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter); s = p.add_subparsers(dest='action', required=True)
    a = s.add_parser('capture'); a.add_argument('--scene', required=True); a.add_argument('--out', type=Path, required=True); a.add_argument('--start-sample', type=int, default=1); a.add_argument('--threads', type=int, default=4); a.add_argument('--reference-film', type=Path)
    a = s.add_parser('replay'); a.add_argument('--capture', type=Path, required=True); a.add_argument('--out', type=Path, required=True); a.add_argument('--rid', default='linux-x64'); a.add_argument('--serial'); a.add_argument('--timeout', type=int, default=900)
    args = p.parse_args(); capture(args) if args.action == 'capture' else replay(args)


if __name__ == '__main__': main()
