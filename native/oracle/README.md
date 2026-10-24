# Cycles oracle runner

Copyright (c) 2026 Jeremy Ellis and contributors

Reference renders that validate the WGSL renderer against independent Cycles
backends. One scene file drives every backend on every platform, through the
public C API only, so the corpus also tests that API. Films are raw float32
buffers; disagreements between reference backends are findings, never tuned away.

## Build

The runner (`runner.cpp`, with `json.h` and `sha256.h`) links only the host
library. On macOS, `python3 tools/build_macos.py` builds it with the Metal host
at `native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle`. On Linux
the host build also produces it as the CMake target `codebrix_cycles_oracle`
(`python3 native/tools/build_cycles.py --target codebrix_cycles_oracle`, output
`native/work/cycles-build/bin/codebrix_cycles_oracle`). Standalone, against any
built host library:

```sh
cmake -S native/oracle -B native/work/oracle-build -DCMAKE_BUILD_TYPE=Release \
  -DCODEBRIX_HOST_LIBRARY=native/work/cycles-build/lib/libcodebrix_cycles_host.so
cmake --build native/work/oracle-build
```

## The CPU oracle's arithmetic (one definition on every platform)

The CPU backends are unmodified Cycles CPU kernels built at a fixed baseline, so a
CPU film does not depend on the machine that built it: no floating-point
contraction (`-ffp-contract=off`), no reciprocal math (`-fno-reciprocal-math`),
and a fixed ISA level: Cycles' SSE4.2 kernels only on x86-64 (never
`-march=native`, no AVX2 kernel), the architecture's base level on ARM64 (no
`-mcpu`). Only build flags change; the pinned source is not edited. The shared
CMake injection (`native/tools/inject_cycles.cmake`, and
`inject_cycles_macos.cmake` for osx-arm64) passes the two math options after
Cycles' own `-ffp-contract=on -freciprocal-math`, the build scripts choose the ISA
level and refuse a CPU kernel compile line that is not at the baseline, and
`native/cycles/cpu_arithmetic.cpp`, compiled with the kernels' flags, refuses an
x86-64 oracle with AVX/FMA or reciprocal math and states the arithmetic in the
capability report (`backendScope.cpu.arithmetic`). Each run's `meta.json`
records the CPU kernel compile line (`cpuKernelCompile`: flags, the deciding
`fpContract` and `reciprocalMath`). Android's on-device CPU is a diagnostic,
never the oracle, and keeps Cycles' flags. The WGSL device and the gates'
reference programs model exactly this arithmetic (native/DEVICE.md,
"Fixed-baseline CPU oracle"). CPU films rendered before the fixed baseline
(under `native/evidence/`) came from `-march=native` builds with contraction and
reciprocal math and are not comparable bit for bit.

## Scene files (`cbcOracleScene` version 1)

`scenes/<family>/<name>.json` holds `id`, `description`, `sourceFixture`,
`render` (`width`, `height`, `samples`, `repeats`, default 2), `colorConfig`
(`null` or an OCIO config path relative to the scene), `nodes` and `images`.
`scenes/author_starters.py` regenerates the starter scenes exactly.

Each node entry has a unique `handle` and either `scene` (a singleton:
`integrator`, `camera`, `film`, `background`) or `create` (a type accepted by
`cbc_host_create_node`, for example `shader`, `mesh`, `object`, `pointlight`,
`arealight`). Entries are applied in order:

- `graph`: `nodes` (`handle`, Cycles shader node `type`, optional `set`) and
  `connect` as `[from, output, to, input]`; `output` is the shader output node.
  Handles inside a graph shadow scene handles.
- `mesh`: `positions` and `indices` (or `positionsFile` and `indicesFile`: raw
  little-endian float32 xyz and uint32 files beside the scene), optional
  `attributes` (`name`, `value`, `domain`, `flags`, then `data` floats, `bytes`,
  or `file`: a raw float32 file, raw bytes for `byte4`) and
  optional `motion` (`steps`, odd, and `positions`: `steps` time-ordered vertex
  arrays whose center equals `positions`; `cbc_host_motion_positions`). Face
  material indices and smoothing are the mesh sockets `shader` and `smooth` in
  `set`; object and camera motion are their `motion` transform arrays.
- `set`: socket names and values exactly as Cycles registers them (see
  `cbc_host_registry_json`). The loader picks the setter from
  `cbc_host_property_info`: numbers, booleans, strings or enum labels, flat or
  nested number arrays, and node handles. The pseudo-socket `image` binds an
  entry of `images` to an image or environment texture node; set that node's
  `colorspace` (`__builtin_srgb`, `scene_linear`, `data`) and `alpha_type`
  (`unassociated`, `associated`, ...) explicitly.

`images` entries name a raw pixel file beside the scene: tightly packed rows,
bottom row first, in one of `float`, `float4`, `half`, `half4`, `byte`, `byte4`,
`ushort`, `ushort4`. Anything the loader cannot express fails with the socket named.
To reproduce a scene exactly across backends, set every integrator, film and
camera control the result depends on; do not rely on defaults.

The library's managed loader (`CodeBrix.Graphics3D.Cycles.Bundles.OracleSceneBundle`)
reads the same format and builds the same scene through the managed API; on the
CPU reference backend its films are bit-identical to the runner's for the same
file. The Android validation app renders a bundle with it (`android_app_gate.py
--mode bundle`, see MAINTAINER-README.txt).

## Bench oracle scene (`scenes/assets/`)

`bench_01.json` is the textured ForestCommunity bench with its original materials
(the Blender glTF importer's node graph: Principled BSDF, image textures,
Separate Color, Mix Color, tangent-space normal map from the glTF TANGENT),
lit by the converter's default sun and a constant world. `bench_01-hdri.json`
is the same bench lit only by a procedural environment (512x256 float4,
importance sampled through a background light with `use_mis` and
`map_resolution` 512). Only the two scene documents and `manifest.json` are
committed. The raw geometry, attribute and texture files derive from the
external asset and are never committed; generate them on a machine that has the
asset and .NET:

```sh
python3 native/oracle/scenes/make_bench_bundle.py \
  --model .../ForestCommunity/Domes/ForestVillage/CommunityProps/bench_01.gltf \
  --output native/work/oracle-assets/bench_01
```

It runs the glTF example tool (which decodes the textures with CodeBrix.Imaging)
and `scenes/author_environment.py` (the procedural environment: IEEE arithmetic
only, the same bytes on every platform), requires both scene documents to be
byte-identical to the committed ones and every raw file to match the SHA-256
values in `manifest.json` (which also records the decoder identity), and leaves
`bench_01.json` and `bench_01-hdri.json` in the output directory for the runner.
On a machine without the asset, copy a verified bundle directory (the two scene
documents, `bundle-manifest.json` and every raw file) and check its files against
`manifest.json`; nothing else is needed to render it.

## Linux gate corpus (`scenes/linux-gates/`)

`python3 native/oracle/export_scenes.py` regenerates the corpus byte for byte:
the maintained Linux gate fixtures re-expressed through the C ABI, each with
`sourceFixture` naming the gate source and case. It holds glass-render (the 20
GGX refraction/glass graphs), textured-plane (13x9 byte4 cubic texture from the
image fixture formula), world-volume and closed-volumes, triangle-lighting
modes 0, 1, 2, 3 and 5, broad-lighting 0-11, transparent-lighting 0-7,
object-motion 0-7 and deforming-motion 0-7. `scenes/linux-gates/manifest.json`
lists the SHA-256 of every scene and pixel file. The gates render their
fixtures with their own scheduler and override some kernel data directly, so
these scenes are compared backend against backend (CPU, WebGPU, Metal), never
with the gates' films. Integrator controls a gate leaves at their defaults stay
at the pinned Cycles defaults; Session-only denoising is disabled.

## Run, compare and bundle

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/<new run> \
  --runner <codebrix_cycles_oracle> --host-library <host library> [--backend NAME] [--scene FILE]
python3 native/oracle/compare.py REFERENCE_DIR OTHER_DIR [...] [--json OUT]
python3 native/oracle/compare.py --scene-root RUN_DIR SCENE_ID [--json OUT]
python3 native/oracle/compare.py --self-test
python3 native/oracle/render_oracles.py --bundle native/work/oracles/<run>
```

Each run writes `oracle/<scene id>/<backend>/film-N.f32` (RGBA float32,
little-endian, lower-left origin) and `meta.json`: scene and film SHA-256,
repeat statistics, render times, host library and runner hashes, repository
commit, OS, CPU, GPU, compiler, Xcode, CMake, CPU kernel flags and, for Metal,
the fixed settings. The runner refuses a non-empty output directory, so films
are never overwritten.

Linux backends: `cpu-x64-linux` and `wgsl-linux-intel` (WebGPU). A backend
that rejects a scene is recorded under `failures` in `run.json` and the run
continues; such a scene is compared only among the backends that rendered it.

macOS backends: `cpu-arm64-macos`; `metal-m2pro` (MetalRT off, so BVH2, and
generic kernels, through `CYCLES_METALRT=0` and
`CYCLES_METAL_SPECIALIZATION_LEVEL=0`); and `metal-m2pro-metalrt`, which
forces MetalRT on to validate the path M3 and later GPUs use by default. Each
Metal backend gets a fresh Cycles kernel cache in the run directory, and its
first render is recorded as `kernelCompileSeconds`.

macOS x64 (Intel Mac) backends: `cpu-x64-macos` (the fixed-baseline CPU
oracle: SSE4.2 kernels only), `cpu-x64-macos-sse42` (`CYCLES_CPU_NO_AVX2=1`,
kept for comparison with older runs; without an AVX2 kernel it runs the same
kernels) and
`wgsl-macos-x64` (the CodeBrix WGSL device on the shipped runtime's Metal route).
`python3 tools/build_macos_x64.py` builds the runner at
`native/work/macos/osx-x64/oracle-build/codebrix_cycles_oracle`; see
[MACOS_X64.md](../host/MACOS_X64.md). On an Intel Mac the bundle and its
evidence directory are named `cycles-oracles-macos-x64-<date>`. Without
`--backend`, a run renders the Apple Silicon and Linux backends only.

`compare.py` is the multi-backend comparator: it takes any number of backend
directories for one scene (`cpu-x64-linux`, `cpu-arm64-macos`, `metal-m2pro`,
`wgsl-linux-intel`, `wgsl-android-<device>`, `cpu-arm64-android-<device>`, ...;
`--scene-root` collects every backend under `RUN_DIR/oracle/SCENE_ID`, CPU
backends first, so a Mac bundle drops in as more directories) and reports the
pairwise matrix: practical failures, strict differences, maximum absolute and
relative error, RMS, the worst pixel and NaN/Inf counts, plus every backend's
repeat films re-read and compared with film-0. The verdict is `perfect` when
every pair passes the practical criterion and nothing else failed. For each pair
it treats the earlier directory as the reference `b`. The practical
criterion `abs(a - b) <= 5e-5 + 2e-4 * abs(b)` must hold for every component;
strict differences (`1e-7 + 1e-7 * abs(b)`) are reported. NaN/Inf, shape or
scene mismatches and films whose bytes differ from their recorded SHA-256 fail.

Previews (`film-0.png`: Reinhard, then 1/2.2 power, rows flipped) come from a
scratch .NET tool outside the repository build that uses the CodeBrix.Imaging
NuGet package. Bitmap textures of glTF assets are converted into raw pixel files
by the glTF example tool (`tools/gltf/CycleGltfRender`, CodeBrix.Imaging).
The bundle (`cycles-oracles-macos-<date>.tar.zst`) carries a `manifest.json`
with the SHA-256 of every file and is verified by extracting it and re-hashing.
That verified extraction is published as `native/evidence/cycles-oracles-macos-<date>/`
(a numeric suffix is added rather than overwriting existing evidence), so other
platforms compare against the repository copy.
