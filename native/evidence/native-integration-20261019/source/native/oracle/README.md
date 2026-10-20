# Cycles oracle runner

Copyright (c) 2026 Jeremy Ellis and contributors

Reference renders that validate the WGSL renderer against independent Cycles
backends. One scene file drives every backend on every platform, through the
public C API only, so the corpus also tests that API. Films are raw float32
buffers; disagreements between reference backends are findings, never tuned away.

## Build

The runner (`runner.cpp`, with `json.h` and `sha256.h`) links only the host
library. On macOS, `python3 tools/build_macos.py` builds it with the Metal host
at `native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle`. Elsewhere:

```sh
cmake -S native/oracle -B native/work/oracle-build -DCMAKE_BUILD_TYPE=Release \
  -DCODEBRIX_HOST_LIBRARY=native/work/cycles-build/lib/libcodebrix_cycles_host.so
cmake --build native/work/oracle-build
```

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
- `mesh`: `positions`, `indices` and optional `attributes`
  (`name`, `value`, `domain`, `flags`, then `data` floats or `bytes`).
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

## Run, compare and bundle

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/<new run> \
  --runner <codebrix_cycles_oracle> --host-library <host library> [--backend NAME] [--scene FILE]
python3 native/oracle/compare.py REFERENCE_DIR OTHER_DIR [...] [--json OUT]
python3 native/oracle/compare.py --self-test
python3 native/oracle/render_oracles.py --bundle native/work/oracles/<run>
```

Each run writes `oracle/<scene id>/<backend>/film-N.f32` (RGBA float32,
little-endian, lower-left origin) and `meta.json`: scene and film SHA-256,
repeat statistics, render times, host library and runner hashes, repository
commit, OS, CPU, GPU, compiler, Xcode, CMake, CPU kernel flags and, for Metal,
the fixed settings. The runner refuses a non-empty output directory, so films
are never overwritten.

macOS backends: `cpu-arm64-macos`; `metal-m2pro` (MetalRT off, so BVH2, and
generic kernels, through `CYCLES_METALRT=0` and
`CYCLES_METAL_SPECIALIZATION_LEVEL=0`); and `metal-m2pro-metalrt`, which
forces MetalRT on to validate the path M3 and later GPUs use by default. Each
Metal backend gets a fresh Cycles kernel cache in the run directory, and its
first render is recorded as `kernelCompileSeconds`.

`compare.py` treats the earlier directory as the reference `b`. The practical
criterion `abs(a - b) <= 5e-5 + 2e-4 * abs(b)` must hold for every component;
strict differences (`1e-7 + 1e-7 * abs(b)`) are reported. NaN/Inf, shape or
scene mismatches and films whose bytes differ from their recorded SHA-256 fail.

Previews (`film-0.png`: Reinhard, then 1/2.2 power, rows flipped) and the
conversion of bitmap textures into raw pixel files come from a scratch .NET
tool outside the repository build that uses the CodeBrix.Imaging NuGet package.
The bundle (`cycles-oracles-macos-<date>.tar.zst`) carries a `manifest.json`
with the SHA-256 of every file and is verified by extracting it and re-hashing.
That verified extraction is published as `native/evidence/cycles-oracles-macos-<date>/`
(a numeric suffix is added rather than overwriting existing evidence), so other
platforms compare against the repository copy.
