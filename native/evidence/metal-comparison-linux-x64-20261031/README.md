# Metal comparison inputs: Velvet subnormal paths and Radial Tiling with two sides (Linux x64)

Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

Frozen inputs and Linux x64 references for a matching test on the Apple Silicon Metal build. Two WebGPU-device
behaviours are provisional until that test is done:

1. **Velvet subnormal paths.** WebGPU flushes subnormal floats; the Linux x64 CPU oracle keeps them. At grazing half
   vectors the Ashikhmin velvet value, or its product with the closure weight and throughput, falls below FLT_MIN
   (1.17549435e-38). The CPU then continues the path with a negligible throughput and the GPU ends it. In the bounce and render
   gates every such state difference disappears when the CPU oracle runs with the x86 FTZ and DAZ bits set, and the films
   agree within the practical criterion. These state differences are judged by an interim rule that applies to
   Velvet only (native/DEVICE.md). The open question: does pinned Cycles-with-Metal flush these values too?
2. **Radial Tiling with two sides.** The WebGPU device refuses `r_gon_sides <= 2` with `0 < r_gon_roundness < 1`
   (the scene is rejected, never approximated). In that range the CPU outputs are binary32 rounding residue: the
   tangent and secant of binary32 pi/2, cancellation of terms near 1e14, and subnormal and NaN intermediates. Pinned
   Cycles compiles its Metal kernels with `fastMathEnabled = YES` (`third_party/cycles/src/device/metal/device_impl.mm`),
   so stock Metal renders this case with trig that is not correctly rounded and does not refuse it. The open question:
   what does the Metal build output there, compared with the CPU?

## Contents

- `scenes/` (cbcOracleScene 1; render with `native/oracle/render_oracles.py --scene <file>`):
  - `velvet-subnormal.json`: velvet at sigma 0.2, 0.25 and 0.3 on two spheres and a grazing panel.
  - `radial-sides2-r0.json`, `-r0.3`, `-r0.5`, `-r0.75`, `-r1.json`: Radial Tiling with two sides at roundness 0,
    three values in (0, 1), and 1. Each scene has two panels, normalize off and on; every output (segment
    coordinates, id, width, rotation) feeds the material colours.
  - `radial-sides3-r0.5.json`: control (three sides, roundness 0.5).
  - The velvet closure's own oracle scene is `native/oracle/scenes/bsdf/velvet.json`.
- `cpu-x64-linux/`: Linux x64 CPU oracle run (fixed baseline; 32 x 24, 8 samples, 2 repeats; raw float32
  `film-N.f32` with `meta.json`), from the commit recorded in each `meta.json`.
- `cpu-x64-linux-ftz/`: the same with the x86 FTZ and DAZ bits set for the whole process (`probes/ftz_daz.c`, loaded
  with `LD_PRELOAD`). Compare it with `cpu-x64-linux/`: bit-identical films mean the subnormal paths contribute
  nothing at film level for these scenes.
- `wgsl-linux-intel/`: the WebGPU device on the Linux x64 machine (information): the Radial Tiling scenes with
  roundness strictly between 0 and 1 are refused by design.
- `velvet-gate-paths.txt`: the (gate call, path) evaluations of the velvet bounce gate
  (`codebrix_cycles_bsdf3_bounce_gate`, synthetic path states) whose state differs from the normal CPU: the sampled
  closure kind (16 = Ashikhmin velvet), the BSDF sample value on the normal CPU and on the FTZ+DAZ CPU (0 wherever an
  intermediate is subnormal), and the closure weight. They are not oracle scenes; the scene-level check is
  `velvet-subnormal.json`.
- `probes/velvet_chain.c`: the pinned velvet value chain (`bsdf_ashikhmin_velvet.h`, sample) for one of those paths,
  inputs embedded, printed for each fixture sigma. Its sigma-0.3 line reproduces the gate's normal-CPU value
  9.97129997e-37 (expf 1.37306174e-38, just above FLT_MIN).

The procedure and what to record are in docs/TESTING-MACOS-ARM64.md, section "Metal comparison: Velvet subnormal
paths and Radial Tiling with two sides".
