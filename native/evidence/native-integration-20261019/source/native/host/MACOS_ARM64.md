# macOS ARM64 (Apple Silicon) Metal build and oracles

Copyright (c) 2026 Jeremy Ellis and contributors

This recipe produces the osx-arm64 product binary: the pinned Cycles revision
with its own Metal backend, behind the same native C API as the WGSL builds.
Every other RID, osx-x64 included, uses Cycles with the WGSL/WebGPU device. The
same build also produces CPU-arm64 and Metal oracle films for validating WGSL.

## Decisions (2026-10-19)

- Scope is osx-arm64 with Metal only. Intel Macs get a separate osx-x64
  Cycles-with-WGSL build that is not part of this recipe.
- The product binary contains no bitmap file-format code and no WebGPU device.
  Callers pass decoded pixels through `cbc_host_image_create` and receive
  scene-linear float RGBA from `cbc_host_render`; CodeBrix.Imaging converts
  files outside this library. On macOS the host rejects texture `filename`
  sockets with `CBC_UNSUPPORTED`, because nothing could load them.
- OIIO's format plugins are all disabled, so its plugin list is empty. OIIO
  still requires zlib, libtiff, OpenEXR/Imath and libjpeg at configure time.
  Only libraries that the final binaries actually load are shipped; the build
  checks the loaded closure. Every remaining library is permissively licensed
  and reviewed in `third_party/dependency-license-review.json`; no copyleft
  terms are introduced.
- The C API is unchanged apart from the additive `CBC_HOST_METAL = 3`.
  `CBC_HOST_WEBGPU` is unsupported on this RID, and `CBC_HOST_METAL` is
  unsupported on every other RID.
- The deployment target is macOS 15.0, the oldest macOS that .NET 10 supports.
  The loader does not enforce a dylib's minimum OS, so `cbc_host_create`
  returns `CBC_UNSUPPORTED` below 15.0 and the .NET wrapper throws
  `PlatformNotSupportedException` before loading the native library.
- Shipped Metal settings follow Blender: MetalRT (Apple's ray-tracing API)
  only on M3 and later, where the GPU has ray-tracing hardware; otherwise
  Cycles' BVH2. Kernels use Cycles' default full specialization. Standalone
  Cycles would enable MetalRT on every chip that exposes the API, so the host
  applies Blender's rule explicitly. Whether to enable MetalRT on M1/M2 is
  decided by measurement on the M2 Pro build machine, not assumed.
- Oracle runs force MetalRT off and generic kernels through Cycles' own
  `CYCLES_METALRT=0` and `CYCLES_METAL_SPECIALIZATION_LEVEL=0`, recorded in
  each `meta.json`. MetalRT is also forced on here and compared against the
  CPU oracles, so the code path that M3 machines use by default is tested.
- Metal compiles kernels at run time from source. The runtime payload carries
  the kernel source tree beside the host library, and the host points Cycles
  at it. The first render on a machine pays the compile; Cycles caches the
  result under `~/.cache/cycles`.
- Binaries are ad-hoc signed. Developer ID signing belongs to whoever ships an
  application that contains them.
- sse2neon is not imported; this build targets only macOS with Metal, and the
  CPU device uses Cycles' plain C++ kernels.
- Reviewed deliverables go to `native/binaries/osx-arm64/` (runtime,
  unstripped and symbols) with provenance. Jeremy creates every commit and push.

## Toolchain and isolation

Agreed external inputs on the build machine: Xcode command-line tools (Apple
Clang, the Metal framework and its runtime compiler, the macOS SDK and libc++),
CMake 3.24 or newer and Python 3. Exact versions are recorded with each build.
Dependency source comes only from the verified snapshots in `third_party/`.

Homebrew must not leak into the build. CMake installed by Homebrew searches its
own prefix by default, so the recipes pass `CMAKE_IGNORE_PREFIX_PATH` for
`/opt/homebrew` and `/usr/local`, clear compiler and pkg-config search variables,
and fail if any configured cache or linked binary references those prefixes.
CMake 4 rejects the old minimum versions declared by the jpeg, robin-map and
yaml-cpp snapshots, so the recipes pass `CMAKE_POLICY_VERSION_MINIMUM=3.5`.

## Oracle tooling

The C++ runner `codebrix_cycles_oracle` and `compare.py` live in
`native/oracle/` and build on Linux too, so every backend renders from the same
scene files. Scene textures are raw pixel files; films are raw float32 RGBA.
A scratch .NET 10 tool in `native/work/` uses the CodeBrix.Imaging.ApacheLicenseForever
1.0.290.858 NuGet package only to convert bitmaps to raw pixel files and to
make previews from films. It is not part of any repository build, and
comparisons never use its output.

## Build, validate and stage

From the repository root on an Apple Silicon Mac:

```sh
python3 tools/build_macos.py --jobs 8
python3 native/tools/macos_host_gate.py --output native/work/macos-validation-<n>
python3 native/oracle/render_oracles.py --output native/work/oracles/<run> \
  --runner native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle \
  --host-library native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib
python3 native/tools/stage_macos_binaries.py --gate native/work/macos-validation-<n> --oracles native/work/oracles/<run>
python3 tools/licensing/audit.py
```

`--skip-dependencies` reuses only a complete prefix whose recipe, toolchain and
library hashes still match. `tools/dependencies/build_macos.py` builds 16 of the
18 snapshots; libpng is not built, and OIIO is configured with every format
plugin disabled. `native/tools/build_cycles_macos.py` builds the host and
lifecycle targets from a verified copy of the Cycles snapshot under
`native/work/`, and stages the Metal kernel include closure (279 files) beside
the library as `codebrix-cycles/source`.

Adaptations, all recorded by the recipes:

- `util/half.h` gains a guarded `#include <arm_neon.h>` in the generated copy
  (`platform.patch`), as on Linux ARM64. Upstream gets that header through
  sse2neon, which this profile leaves out, so CPU kernels use Cycles' plain C++
  path. GPU compilation never sees the change; shipped kernel sources are the
  pristine snapshot.
- Apple Clang accepts Cycles' `-msse4.2` probe for arm64, so the recipe records
  `CXX_HAS_SSE42`, `CXX_HAS_AVX2` and `CXX_HAS_F16C` as absent, and rejects any
  x86 or host-specific CPU flag. `WITH_CYCLES_NATIVE_ONLY` is off, so the CPU
  kernels run on every M-series chip.
- `FMT_CONSTEVAL=` keeps fmt's run-time format checks under Apple Clang 21, as
  on NDK 29.
- The concurrency test gets an Apple-only barrier, because macOS lacks POSIX
  barriers.
- Cycles and the host are compiled with `-gline-tables-only` for the dSYM.
  CPU films are bit-identical with and without it.
- Every dependency and the host use `-ffile-prefix-map=<checkout>=.`, and the
  host links with `-oso_prefix`, so no build path reaches a binary, a line table
  or the debug map. Cycles appends its own linker flags as a CMake list, so the
  macOS hook adds `-oso_prefix` with `add_link_options` instead.

`stage_macos_binaries.py` copies the host and its dependency closure (16 dylibs)
into `native/binaries/osx-arm64/{runtime,unstripped,symbols}`. It removes
build-tree rpaths (leaving only `@loader_path`), strips the runtime copies
(`strip -x -S`), signs everything ad hoc and writes the host dSYM, checking
UUIDs across all three copies. `provenance.json` records the inputs,
toolchain, build commands, hashes, the UUID mapping, notices per library,
validation evidence and the exact approved payload. `tools/licensing/audit.py`
accepts compiled files only when listed there with matching SHA-256, and kernel
source copies only when byte-identical to `third_party/cycles/src`. Before the
recorded result, the stager copies the runtime directory alone to a temporary
folder and renders from it on CPU and Metal.

Two Linux validations do not apply here. The image reference executable writes
and reads EXR/PNG through OIIO, and the reload test sets texture filenames.
The WGSL device and render gates are absent because this profile has no WebGPU
device.

## Results and findings (2026-10-19)

Build machine: Mac mini Mac14,12, Apple M2 Pro (16-core GPU), 16 GB, macOS
27.0.1, Xcode 27.0, Apple Clang 21.0.0, CMake 4.4.3 (Homebrew; isolated as
described above), Python 3.14.8. Base commit `3ac7cd3d` plus this session's
uncommitted changes.

- Host gate: 36 exports, macOS 15.0 minimum, ad-hoc signatures valid, Metal
  framework linked, no image codec library in the load closure. CPU C-API
  films are bitwise identical to the independent native Session references
  (6 films), as are the concurrent-lifetime films and the thin .NET wrapper's
  films from `tests/HostLifecycle`. The Metal lifecycle test
  (edits after compilation, sample budget changes, cancel, destroy during
  render) passes. Metal films differ from the CPU references by at most
  4.5e-8, with MetalRT both off and on.
- The runner's JSON-driven `starter/diffuse-triangle` reproduces the C
  lifecycle test's first film bit for bit on CPU.
- Oracle comparisons, film 0 (strict differences are reported, not required):

  | Scene | Pair | Max abs | Practical failures | Strict differences |
  |---|---|---|---|---|
  | diffuse-triangle | Metal vs CPU | 3e-8 | 0 | 0 |
  | diffuse-triangle | MetalRT vs CPU | 3e-8 | 0 | 0 |
  | glossy-sphere-point-light | Metal vs CPU | 1.3e-4 | 0 | 417 |
  | glossy-sphere-point-light | MetalRT vs CPU | 1.1e-4 | 0 | 417 |
  | glass-and-area-light | Metal vs CPU | 1.4e-4 | 0 | 1189 |
  | glass-and-area-light | MetalRT vs CPU | 1.4e-4 | 0 | 1213 |
  | textured-plane | Metal vs CPU | 9.6e-5 | **4** | 1558 |
  | textured-plane | MetalRT vs CPU | 9.6e-5 | **4** | 1558 |

- Finding: with linear interpolation of a byte4 texture, Metal exceeds the
  practical criterion in 4 components (worst 1.16 times the tolerance), with
  MetalRT on or off. The same scene with `closest` interpolation matches CPU
  to 3e-8 with no strict differences. The difference comes from the GPU's
  hardware bilinear filtering, which is not bit-exact with Cycles' CPU float
  interpolation. Nothing was adjusted. This matters for choosing which
  reference the WGSL texture path should match.
- Metal repeat renders are not bitwise identical (film accumulation uses
  atomics); repeat differences stay below 2e-6. CPU repeats are identical.
- MetalRT on the M2 Pro, with shipped full specialization, 640x480 at 256
  samples: 10% slower than BVH2 for a 65k-triangle glossy sphere (0.90 s
  against 0.82 s) and for the glass scene (1.09 s against 0.99 s). Blender's
  rule therefore stays: MetalRT only on M3 and later.
- Run-time Metal kernel compile with an empty Cycles cache took about 7.5 s on
  the first oracle run. Later runs are faster because macOS keeps its own
  shader cache, which the recipes do not clear.

## Session record (2026-10-19)

- Deliverable: `native/binaries/osx-arm64` (runtime 25 MB, unstripped 30 MB,
  symbols 13 MB; 314 approved files).
- Oracle output: `native/evidence/cycles-oracles-macos-20261019/` (75 files
  and `manifest.json`), extracted unchanged from the verified bundle
  `cycles-oracles-macos-20261019.tar.zst`, SHA-256
  `a3871626a2479b28027d4940c0468026eb52307e9368cb5355db29bef394ce33`. The
  `meta.json` files keep the absolute paths of the run as recorded. Later runs
  of `render_oracles.py --bundle` publish there themselves. The 14 runner logs
  were renamed from `.log` to `.output.txt` after extraction, because the
  repository ignores `*.log`; their bytes are unchanged, and `manifest.json`
  records the rename under `renamedAfterBundle`. New runs write `.output.txt`.
- Fresh-checkout build: the tracked and new files were copied to an empty
  directory at a different path, with no `native/work`, and
  `tools/build_macos.py` rebuilt everything there. That build passed the host
  gate and restaged a payload. Its CPU oracle films are bitwise identical to the
  recorded ones, and its Metal films pass the practical criterion against them.
  302 of the 314 payload files are byte-identical: all kernel sources and 11 of
  the 16 libraries. The rest vary with the length of the checkout path. OpenEXR,
  IlmThread and both OIIO libraries keep their own upstream rpath handling,
  which links an absolute prefix rpath that installation rewrites but whose
  header space stays. The host's debug map names the object for
  `native/host/cycles_host.cpp` after its absolute source path (CMake's naming
  for sources outside the Cycles tree), which changes its UUID. No build path
  appears in any string, line table or rpath of the shipped files.
- Next steps: the Linux side exports its gate fixtures to `native/oracle/scenes`
  and renders CPU-x64 and WGSL films with the same runner; the other RIDs drop
  OIIO format plugins and decide on `CBC_HOST_NO_IMAGE_FILES`; NuGet packaging
  places `codebrix-cycles/source` beside the osx-arm64 runtime library.
- Nothing was committed or pushed.
