# macOS x64 (Intel) WGSL/WebGPU build and oracles

Copyright (c) 2026 Jeremy Ellis and contributors

This recipe produces the osx-x64 product binary: the pinned Cycles revision
with the CodeBrix WGSL/WebGPU device and the CPU device, behind the same native C
API and managed API as every other RID. On an Intel Mac the shipped
CodeBrix.WebGpu.Native runtime reaches the GPU through Metal. Cycles' own Metal
device is the osx-arm64 product (see [MACOS_ARM64.md](MACOS_ARM64.md)) and is
not built here. The same build produces CPU-x64 oracle films from Apple Clang
and WGSL films on the Intel Mac's GPU.

The recipe has not yet been executed on an Intel Mac. The test instructions in
[docs/TESTING-MACOS-X64.md](../../docs/TESTING-MACOS-X64.md) mark every step
that still needs validation there.

## Decisions

- Scope is osx-x64 with the WGSL/WebGPU device. `WITH_CYCLES_DEVICE_METAL` is
  off (Cycles enables it by default on Apple platforms), and the configure
  step fails if any Metal code reaches the build.
- The shipped runtime is used unchanged: `CodeBrix.WebGpu.Native.MitLicenseForever`
  1.0.290.1433, `runtimes/osx-x64/native/libcodebrix_wgpu.dylib`, SHA-256
  `3a3abcf9e67ff9a6d6c5c0ebea4e504ead7d21167858de631d4dc835f793c483`. It is a
  thin x86_64 Mach-O with install name `@rpath/libcodebrix_wgpu.dylib`, minimum
  macOS 11.0, an ad-hoc signature, and it links QuartzCore, Metal, Foundation,
  CoreFoundation, libobjc, libiconv and libSystem. It contains wgpu's Metal
  backend and no Vulkan backend.
- The runtime is wired exactly as `libcodebrix_wgpu.so` is on Linux: the
  shared injection `native/tools/inject_cycles.cmake` creates the imported
  `codebrix_wgpu` target and links the device to it; the macOS x64 injection
  points that target at the `.dylib` and the recipe copies the runtime beside
  the host library, where `@loader_path` finds it. The staged payload does not
  carry the runtime; it is delivered by its own package, and `provenance.json`
  records the package, version and hash.
- The deployment target is macOS 15.0, the oldest macOS that .NET 10 supports,
  as on osx-arm64. The host refuses older systems on every Apple RID
  (`cbc_host_create` returns `CBC_UNSUPPORTED`) and the .NET wrapper throws
  `PlatformNotSupportedException` first. The runtime's own minimum (11.0) is
  lower, so it imposes no further limit.
- The product reads no image files, as on osx-arm64: OIIO's format plugins are
  all disabled, libpng is not built, `CBC_HOST_NO_IMAGE_FILES` is set, and the
  host rejects texture `filename` sockets with `CBC_UNSUPPORTED`. Callers pass
  decoded pixels.
- `WITH_CYCLES_NATIVE_ONLY` is off, so the CPU kernels run on every Intel Mac
  that runs macOS 15: Cycles builds its SSE4.2 baseline and its AVX2 kernels and
  picks the AVX2 kernels at run time on CPUs that have AVX2.
  `CYCLES_CPU_NO_AVX2=1` forces the SSE4.2 kernels; the oracle runs record both.
- The build host is a native Intel Mac. The recipes refuse Apple Silicon hosts
  and Rosetta translation, because Cycles' x86 reference options follow
  CMake's host processor, which is not established under translation.
- There is no Metal oracle on Intel Macs. At the pinned revision, Cycles'
  Metal device accepts only Apple GPUs with unified memory and skips every
  device whose name contains "Intel" or "AMD" (`device/metal/util.mm`), so a
  Metal build would find no usable device. Producing Metal films on an Intel
  Mac would need a change to that check in a generated overlay, and whether
  Cycles' Metal kernels then compile and run on Intel and AMD GPUs is
  unknown; nothing here attempts it. The Metal oracle comes from Apple Silicon.
- Binaries are ad-hoc signed. Developer ID signing belongs to whoever ships an
  application that contains them.
- Reviewed deliverables go to `native/binaries/osx-x64/` (runtime, unstripped
  and symbols) with provenance. The maintainer makes every commit and push.

## Why separate scripts

The osx-x64 recipes are siblings of the osx-arm64 ones, not an architecture
switch inside them. The osx-arm64 dependency recipe hashes its own source into
the identity of its dependency prefix, and the osx-arm64 `provenance.json`
records the recipe hashes; any edit to those files, even one that keeps their
behaviour, would invalidate the Apple Silicon prefix and the recorded
provenance. The siblings import the osx-arm64 dependency recipe as a module and
reuse its component profile, so both RIDs build the same configuration of the
reviewed snapshots.

| Purpose | osx-x64 | osx-arm64 |
|---|---|---|
| Coordinator | `tools/build_macos_x64.py` | `tools/build_macos.py` |
| Dependency prefix | `tools/dependencies/build_macos_x64.py` | `tools/dependencies/build_macos.py` |
| Cycles host and gates | `native/tools/build_cycles_macos_x64.py` | `native/tools/build_cycles_macos.py` |
| CMake injection | `native/tools/inject_cycles_macos_x64.cmake` (includes `inject_cycles.cmake`) | `native/tools/inject_cycles_macos.cmake` |
| Host gate | `native/tools/macos_x64_host_gate.py` | `native/tools/macos_host_gate.py` |
| Staging | `native/tools/stage_macos_x64_binaries.py` | `native/tools/stage_macos_binaries.py` |
| Work tree | `native/work/macos/osx-x64/` | `native/work/macos/osx-arm64/` |

Every script refuses to run anywhere but an Intel Mac and prints why; `--help`
works everywhere.

## Toolchain and isolation

Agreed external inputs on the build machine: Xcode or the Xcode command-line
tools (Apple Clang, the macOS SDK and libc++), CMake 3.24 or newer, Python 3.12
or newer and the .NET 10 SDK for x64. `zstd` is needed only to bundle oracle
runs. Exact versions and compiler and CMake hashes are recorded with each build.
Dependency source comes only from the verified snapshots in `third_party/`.

Homebrew lives in `/usr/local` on Intel Macs and in `/opt/homebrew` on Apple
Silicon. Both prefixes are excluded explicitly: CMake receives
`CMAKE_IGNORE_PREFIX_PATH` and `CMAKE_IGNORE_PATH` for both, compiler and
pkg-config search variables (and Homebrew's `HOMEBREW_*` variables) are
cleared, and the build fails if a configured cache, a compile command, a link
command, a compiler dependency file or a linked library names either prefix.
On an Intel Mac `/usr/local` also holds Homebrew's CMake and Python; the checks
allow CMake's own program and module paths, which are tools, not inputs. Before
the first component, the dependency recipe asks Apple Clang for its default
header search list and fails if it reaches a Homebrew prefix outside the SDK.
CMake 4 rejects the old minimum versions declared by the jpeg, robin-map and
yaml-cpp snapshots, so the recipes pass `CMAKE_POLICY_VERSION_MINIMUM=3.5`.

## WGSL kernels

The WGSL overlay is generated by the same code as on Linux
(`native/tools/build_cycles.py`, `overlay`). Part of it is translated from the
pinned upstream sources through Clang's AST dump, which runs Apple Clang on the
Mac. To prove the Mac generated the same kernels, record the Linux overlay's
hashes and pass them to the Mac build:

```sh
# On Linux, after a maintained Linux build:
python3 native/tools/build_cycles_macos_x64.py --write-overlay-hashes native/work/cycles-overlay \
  --output native/work/linux-overlay-hashes.json
# On the Intel Mac, with that file copied over:
python3 tools/build_macos_x64.py --jobs 8 --reference-overlay linux-overlay-hashes.json
```

The comparison covers every embedded WGSL include, the translated kernels and
the registration overlay; AST dumps, caches and the device header (which names
an absolute path) are excluded. A difference fails the build and names the
files.

## Build, validate and stage

From the repository root on an Intel Mac:

```sh
python3 tools/build_macos_x64.py --jobs 8 --reference-overlay linux-overlay-hashes.json
python3 native/tools/macos_x64_host_gate.py --output native/work/macos-x64-validation-<n> \
  --references <a passed Linux host_gate.py output> [--device-gates]
python3 native/oracle/render_oracles.py --output native/work/oracles/<run> \
  --runner native/work/macos/osx-x64/oracle-build/codebrix_cycles_oracle \
  --host-library native/work/macos/osx-x64/host/build/lib/libcodebrix_cycles_host.dylib \
  --backend cpu-x64-macos --backend cpu-x64-macos-sse42 --backend wgsl-macos-x64
python3 native/tools/stage_macos_x64_binaries.py --gate native/work/macos-x64-validation-<n> --oracles native/work/oracles/<run>
python3 tools/licensing/audit.py
```

`--skip-dependencies` reuses only a complete prefix whose recipe, toolchain and
library hashes still match. `native/tools/build_cycles_macos_x64.py` builds the
host, the lifecycle and ABI consumers, the independent Session references and
the WGSL device gates from the pinned snapshot directly (no platform adaptation
is needed on x86_64), checks the exports against `exports.macos`, the x86_64
architecture, the minimum OS, the load closure and the absence of image codec
libraries and of a direct Metal link, and copies the runtime beside the host.
The coordinator then builds the oracle runner from `native/oracle` against
that host. Selected targets can be built with repeated `--target` arguments.

The host gate runs the Linux host-gate list (`host_gate.py`) with its
comparisons and tolerances: independent native Session references, the C
lifecycle and process exit, concurrent lifetimes, the thin .NET wrapper, the
WebGPU lifecycle through `CBC_HOST_GPU`, the ABI additions on CPU and GPU and
the example consumer. Same-host CPU films must be bitwise identical. GPU films
must pass the practical criterion and, where the Linux gate requires it, the
strict one; the four ABI light films require the practical criterion and record
strict differences. The image reference and the color-reload tests read and
write image files and do not apply. With `--references`, the CPU films are also
compared with a passed Linux x64 host gate under the cross-platform rules of
`linux_arm64_host_gate.py`. `--device-gates` also runs the built WGSL device
gate executables and records each one's pass flag; the Linux gate drivers'
additional coverage assertions are not reproduced. The gate records the WebGPU
adapter report from the GPU ABI run.

`stage_macos_x64_binaries.py` copies the host and its dependency closure into
`native/binaries/osx-x64/{runtime,unstripped,symbols}`, removes build-tree
rpaths (leaving only `@loader_path`), strips the runtime copies
(`strip -x -S`), signs everything ad hoc and writes the host dSYM, checking
UUIDs across all three copies. Its Mach-O review requires, for every staged
dylib: `otool -L` names only `/usr/lib`, `/System/Library`, `@rpath` libraries
of the payload and `@rpath/libcodebrix_wgpu.dylib`; `lipo` reports x86_64
alone; the minimum OS is 15.0; the only rpath is `@loader_path`; `codesign -dv`
reports an ad-hoc signature without a team identifier, and `codesign --verify`
passes. The host must not link Metal itself. `provenance.json` records the
inputs, toolchain, build commands, overlay comparison, hashes, the UUID
mapping, the Mach-O review, the runtime package identity, notices per library,
validation evidence and the exact approved payload. Before the recorded
result, the stager copies the runtime directory and the package runtime alone
to a temporary folder and renders from it on CPU and WebGPU.

## WebGPU backend selection on macOS

The shipped runtime's macOS builds contain wgpu's Metal backend and no Vulkan
backend, so the WGSL device (`native/src/webgpu_device.cpp`) requests the
Metal instance backend under `__APPLE__` and names Metal in its two error
messages ("Cannot create Metal WebGPU instance", "No hardware Metal adapter").
Every other platform keeps the Vulkan backend, so Linux and Android are
unchanged. UNTESTED: the Apple branch has not yet been compiled or run on a
Mac. On osx-x64 the product stays Cycles with WGSL kernels; the runtime only
routes WGSL to Metal.
