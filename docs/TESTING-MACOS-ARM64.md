# Testing on macOS ARM64 (Apple Silicon, Metal)

Instructions for building, validating and recording the osx-arm64 product
binary on an Apple Silicon Mac, and for generating the CPU-arm64 and Metal
oracle films that the Linux machine compares against CPU-x64 and WGSL. The
recipe itself is described in [MACOS_ARM64.md](../native/host/MACOS_ARM64.md)
and the oracle runner in [native/oracle/README.md](../native/oracle/README.md);
this document is the ordered test procedure built on them.

Lines marked **MISSING** name a step for which the repository has no recipe or
tool yet. Do not work round them on the Mac; record what was done instead and
bring the output back.

## 1. Purpose and evidence produced

The osx-arm64 RID ships the pinned Cycles revision with its own Metal device
behind the same C API and managed API as every WGSL build. Every other RID,
osx-x64 included, uses the WGSL/WebGPU device (see
[TESTING-MACOS-X64.md](TESTING-MACOS-X64.md) for Intel Macs). A run of this
procedure produces:

- a build of the dependency snapshots, the Metal host and the oracle runner
  from the committed tree, with its build records;
- a passed (or failed, with the failure recorded) macOS host gate: export and
  load-closure review, minimum OS, ad-hoc signatures, the public-API contract
  checks, CPU lifecycle films bitwise against the native Session references,
  the thin .NET wrapper's CPU films, the memory-image reference and reload
  fixtures (native and managed) bitwise against an independent image
  reference, and the Metal lifecycle with MetalRT off and forced on, compared
  with the CPU references;
- an oracle run over the scene corpus in `native/oracle/scenes` (starter
  scenes, the exported Linux gate corpus and, where its generated bundle is
  present, the bench) on three backends:
  `cpu-arm64-macos`, `metal-m2pro` and `metal-m2pro-metalrt`, with pairwise
  comparisons;
- a verified oracle bundle (`.tar.zst` with `manifest.json`) for the
  Milestone 1 two-oracle comparator on the Linux machine;
- the managed build, test and pack results on macOS, including the managed
  GPU render tests on Cycles' Metal device, judged against the recorded Metal
  references (section 5);
- optionally, a restaged osx-arm64 payload under `native/binaries/osx-arm64`
  (only on the maintainer's decision; see step 9).

## 2. Prerequisites and pins

Pins (the build scripts check them; do not change them):

- Cycles revision `a456b761034dda42c32eef9f4aae0fa5a5c9f604`, the filtered
  snapshot in `third_party/cycles` (`third_party/cycles-import.json`).
- CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210 is the shipped
  runtime of the WGSL RIDs. The osx-arm64 profile contains no WebGPU device and
  does not load it; it is not needed on this machine.
- Dependency sources only from the verified snapshots in `third_party/deps`
  (manifest `third_party/dependency-imports.json`). Nothing is fetched.

Machine and toolchain:

- An Apple Silicon Mac. The backend labels `metal-m2pro` and
  `metal-m2pro-metalrt` are fixed names in `render_oracles.py`; on another
  chip the films are still labelled that way, so record the real chip (section
  7). MetalRT is forced on only as a validation path; the shipped rule is
  MetalRT on M3 and later only.
- macOS 15.0 or later (the deployment target is macOS 15.0, the oldest macOS
  that .NET 10 supports; `cbc_host_create` returns `CBC_UNSUPPORTED` below it).
- Xcode command-line tools: Apple Clang, the Metal framework and its run-time
  compiler, the macOS SDK and libc++. The recorded build machine used Xcode
  27.0 and Apple Clang 21.0.0. Cycles compiles the Metal kernels at run time
  from the kernel sources staged beside the host library; no offline Metal
  compiler step exists.
- CMake 3.24 or newer (CMake 4 works; the recipes pass
  `CMAKE_POLICY_VERSION_MINIMUM=3.5` for the old snapshot minimums).
- Python 3.
- zstd (`render_oracles.py --bundle` runs `zstd -19`).
- The .NET 10 SDK (the host gate builds `tools/hosttests/HostLifecycle`; the
  managed tests and package).
- Homebrew may be installed but must not leak into the build: the recipes
  ignore `/opt/homebrew` and `/usr/local` and fail if a configured cache or
  linked binary references them. Do not set `CC`, `CXX`, `CFLAGS`,
  `LDFLAGS`, `SDKROOT`, `MACOSX_DEPLOYMENT_TARGET` or `DYLD_*` in the shell;
  the recipes clear them anyway.

Checkout:

```sh
git clone https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles
cd CodeBrix.Graphics3D.Cycles
git rev-parse HEAD
git status --porcelain
```

Record the commit. `git status --porcelain` must print nothing before the
build; the oracle `meta.json` records `repoWorkingTreeClean`.

## 3. Build commands

All commands run from the repository root.

```sh
python3 tools/build_macos.py --jobs 8
```

This runs `tools/dependencies/build_macos.py` (the dependency snapshots into
`native/work/macos/osx-arm64/dependencies`), then
`native/tools/build_cycles_macos.py` (the Metal host, the lifecycle and
reference executables, and the Metal kernel include closure staged beside the
library as `codebrix-cycles/source`), then the oracle runner. Outputs:

- host library: `native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib`
- oracle runner: `native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle`
- build records: `native/work/macos/osx-arm64/host/status.json`,
  `native/work/macos/osx-arm64/host/invocation.json` and the dependency marker
  `native/work/macos/osx-arm64/dependencies/install/codebrix-osx-arm64-build.json`

`--skip-dependencies` reuses only a complete dependency prefix whose recipe,
toolchain and library hashes still match. Selected host targets can be rebuilt
with `python3 native/tools/build_cycles_macos.py --jobs 8 --target NAME`
(repeatable).

## 4. Ordered steps and budgets

Run the steps in this order. Each step writes to a NEW output directory; the
scripts refuse a non-empty one. Measure every step's wall time (for example
with `/usr/bin/time -p` in front of the command) and record it.

Budgets: macOS walls have not been measured for these steps. The only recorded
figures are the first Metal kernel compile with an empty Cycles cache (about
7.5 s) and single renders of the starter-scale scenes (about 1 s each at
640x480, 256 samples). Where a step shows "no measurement yet", record the
wall; the scripts' own per-call timeouts still apply (stated per step). Stop a
step that exceeds three times a stated budget and bring back its log.

| Step | Command | Budget |
| --- | --- | --- |
| 1 | identity record (section 7) | no measurement yet |
| 2 | `python3 tools/build_macos.py --jobs 8` | no measurement yet |
| 3 | macOS host gate | no measurement yet; script timeouts 900 s per call, 1,800 s per Metal run |
| 4 | oracle run, three backends, whole corpus | no measurement yet; first Metal compile about 7.5 s per backend |
| 5 | compare against the previous macOS oracle evidence | seconds |
| 6 | oracle bundle | no measurement yet |
| 7 | managed build, test, pack (Release, then Debug) | no measurement yet |
| 8 | licensing audit | no measurement yet |
| 9 | staging (only on the maintainer's decision) | no measurement yet |

### Step 3: macOS host gate

```sh
python3 native/tools/macos_host_gate.py --output native/work/macos-validation-<n>
```

It requires a complete, passed build (step 2) and checks, in order: every
pinned build artifact hash; the host's exports against `native/host/cycles_host.h`;
that every linked library is `@rpath/`, `/usr/lib/` or `/System/Library/`;
the Mach-O minimum OS; ad-hoc signatures (`codesign --verify`); that the Metal
framework is linked and no image codec library is in the load closure; the
public-API contract (`CBC_HOST_WEBGPU` and texture `filename` sockets
rejected with `CBC_UNSUPPORTED`); the native Session references (`session` and
`session64`, CPU); the C lifecycle and process-exit consumers, bitwise
against those references; the thin .NET wrapper (`HostLifecycle`, built with
`dotnet build`), bitwise against the same references; and the Metal lifecycle
(`codebrix_host_gpu_test`) with shipped settings and with `CYCLES_METALRT=1`,
compared with the CPU references by the practical criterion. It then runs the
image fixtures in their memory-only form, because this profile reads no image
files: the independent image reference (`codebrix_host_image_reference`, three
runs of 256 memory-image cases), the C reload test (six rounds, bitwise against
that reference), and the managed `tools/hosttests/ImageModes` and
`tools/hosttests/ColorReload` fixtures (built into `managed-bin/`, bitwise
against the same reference), plus the guarded-concurrency films. The four file
and rebind cases per round apply only to builds that read files; here the
fixtures verify that texture filenames are rejected instead, and `status.json`
lists them under `notApplicable`. Result: `status.json` (`passed`), `results.json`,
`comparisons.json`, `binary-review.json`, `commands.json` and one
`.stdout.txt`/`.stderr.txt` pair per call.

### Step 4: oracle run

Pass the three macOS backends explicitly. Without `--backend`,
`render_oracles.py` runs every backend it knows, including the Linux labels
`cpu-x64-linux` and `wgsl-linux-intel`, which on a Mac would produce
mislabelled CPU films and a recorded WebGPU failure.

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/<run> \
  --runner native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle \
  --host-library native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib \
  --backend cpu-arm64-macos --backend metal-m2pro --backend metal-m2pro-metalrt
```

Without `--scene` it renders every scene under `native/oracle/scenes`
(`assets`, `linux-gates` and `starter`; `manifest.json` files are skipped).
The two `assets/` bench scenes read raw files that
`native/oracle/scenes/make_bench_bundle.py` generates from the committed bench
asset; the raw files are not committed (native/oracle/README.md, "Bench oracle
scene"), so the documents under `scenes/assets/` cannot render in place. Each
Metal backend's kernel-compiling warm-up renders the first scene, which would
be one of them, so generate the bundle first (section 6) and pass the scenes
explicitly, the bench documents from the generated bundle directory (in zsh,
build the `--scene` list as an array; an unquoted string stays one argument):

```sh
bash -c 'scenes=(); for f in $(find native/oracle/scenes/starter native/oracle/scenes/linux-gates -name "*.json" ! -name manifest.json | sort) native/work/oracle-assets/bench_01/bench_01.json native/work/oracle-assets/bench_01/bench_01-hdri.json; do scenes+=(--scene "$f"); done
python3 native/oracle/render_oracles.py --output native/work/oracles/<run> \
  --runner native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle \
  --host-library native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib \
  --backend cpu-arm64-macos --backend metal-m2pro --backend metal-m2pro-metalrt "${scenes[@]}"'
```

Without the generated bundle, leave the two bench documents out of the list and
record that the bench was not rendered. `--threads`
defaults to 4; keep the default so CPU films are comparable with earlier runs.
Each Metal backend gets a fresh Cycles kernel cache inside the run directory,
and its first render is recorded as `kernelCompileSeconds`. A backend that
rejects a scene is recorded under `failures` in `run.json` and the run
continues; such a scene is compared only among the backends that rendered it.
After rendering, the script runs `compare.py` for every scene and writes
`comparisons/<scene>.json` and `comparisonsPassed` in `run.json`.

`--previews TOOL` writes `film-0.png` previews with an external converter;
the repository has no such tool, so omit the option. Previews are never used
for comparison.

### Step 5: compare with the previous macOS evidence

The previous macOS oracle evidence lives under
`native/evidence/cycles-oracles-macos-<date>/`. Compare each starter scene's
films from this run against it, per backend, reference first:

```sh
python3 native/oracle/compare.py \
  native/evidence/cycles-oracles-macos-<date>/oracle/starter/diffuse-triangle/cpu-arm64-macos \
  native/work/oracles/<run>/oracle/starter/diffuse-triangle/cpu-arm64-macos \
  --json native/work/oracles/<run>/previous-evidence-diffuse-triangle-cpu.json
```

Repeat for every starter scene and for `metal-m2pro` and
`metal-m2pro-metalrt`. A CPU film that is no longer bitwise identical to the
earlier one, or any practical failure, is a finding to record; nothing is
adjusted.

### Step 6: oracle bundle

```sh
python3 native/oracle/render_oracles.py --bundle native/work/oracles/<run>
```

It writes `manifest.json` (SHA-256 of every file), packs
`bundle/cycles-oracles-macos-<date>.tar.zst`, verifies a fresh extraction
against the manifest, and publishes that extraction as a NEW directory
`native/evidence/cycles-oracles-macos-<date>/`, adding a numeric suffix rather
than touching an existing one. It prints the bundle path, its SHA-256 and the
evidence directory; save that output.

### Step 7: managed build, tests and package

```sh
dotnet restore CodeBrix.Graphics3D.Cycles.slnx
dotnet build --configuration Release CodeBrix.Graphics3D.Cycles.slnx
dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Release --no-build
dotnet pack --configuration Release --no-build CodeBrix.Graphics3D.Cycles.slnx
dotnet build --configuration Debug CodeBrix.Graphics3D.Cycles.slnx
dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Debug --no-build
```

The build must report 0 warnings. The test project's `NativeHostLibrary`
loads the in-tree host of the platform: on macOS arm64
`native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib`
(with `codebrix-cycles/source` beside it), on Linux x64 the Linux build. The
tests that need it skip only when it has not been built (the skip reason names
`python3 tools/build_macos.py --jobs 8`). Every test runs on macOS, including
`GpuRenderTests`, where `CyclesBackend.Gpu` resolves to Cycles' Metal device:
the validation scenes and glTF samples, progress and cancellation, device-error
recovery, object edits after a render, passes, and the capability and device
reports. Their films are judged by the reference policy in section 5. Record
the passed, skipped and failed counts, and any skip reasons.

The package carries no native runtime assets yet (MAINTAINER-README.txt,
PACKAGING AND PUBLISHING), so no consumer-style package render is possible.

**Recording the Metal references.** `tests/CodeBrix.Graphics3D.Cycles.Tests/References/metal-osx-arm64/`
holds one reviewed Metal film per GPU test scene (`<name>.rgba.f32`) and
`manifest.json` (SHA-256, length, recording OS and device, library version and
the film's CPU comparison). Re-record them only when the Cycles revision, the
toolchain, a scene or its render settings change:

```sh
CODEBRIX_CYCLES_RECORD_METAL_REFERENCES=1 dotnet test --project tests/CodeBrix.Graphics3D.Cycles.Tests \
  --configuration Release --no-build --filter-class "*GpuRenderTests"
```

Then review every changed film and its recorded CPU comparison (`git diff` of
`manifest.json`; a CPU comparison that grew is a finding to explain before the
films are committed) and run the tests again without the variable. A test
whose film no longer matches its manifest length fails with a request to
re-record; a missing reference fails, never skips.

### Step 8: licensing audit

```sh
python3 tools/licensing/audit.py
```

It must pass with the new evidence directory from step 6 in place.

### Step 9: staging (only on the maintainer's decision)

```sh
python3 native/tools/stage_macos_binaries.py --gate native/work/macos-validation-<n> --oracles native/work/oracles/<run>
python3 tools/licensing/audit.py
```

The stager refuses while `native/binaries/osx-arm64/` already holds a payload
("review and remove it first"). The committed payload is a reviewed
deliverable: replace it only when the maintainer decides to, after reviewing what
changed. When it runs, it copies the host and its dependency closure into
`runtime/`, `unstripped/` and `symbols/`, removes build-tree rpaths, strips and
ad-hoc signs the runtime copies, writes the dSYM, records `provenance.json`
and renders from a temporary copy of `runtime/` alone on CPU and Metal.

**MISSING:** a comparison of a fresh build against the committed payload
without restaging. Until one exists, compare the SHA-256 of
`native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib`
and of the dependency libraries with `approvedPayload` in
`native/binaries/osx-arm64/provenance.json` by hand (`shasum -a 256`) and
record the result; differences are expected wherever the build path length or
the sources changed.

## 5. Reference policy and tolerances

- The CPU kernels are the oracle. On this machine the CPU-arm64 films
  (`cpu-arm64-macos`, Apple Clang, plain C++ kernels without sse2neon) are the
  local CPU oracle; on the Linux machine CPU-x64 is the other.
- Both are built at the fixed baseline (native/DEVICE.md, "Fixed-baseline CPU
  oracle"): no FMA contraction (Apple Clang's arm64 default is on; the CMake
  injection passes `-ffp-contract=off`), no reciprocal math, the architecture's
  base level (no `-mcpu`). The build recipe refuses a CPU kernel compile line
  that is not at the baseline, and each oracle `meta.json` records the line
  (`cpuKernelCompile`). The macOS CPU evidence recorded before this rule
  (`native/evidence/cycles-oracles-macos-20261019`) used contraction, so its
  CPU films are not expected to match new ones bit for bit.
- Practical criterion, must pass for every component:
  `abs(a - b) <= 5e-5 + 2e-4 * abs(b)`, with `b` the reference (the earlier
  directory given to `compare.py`).
- Strict diagnostic, reported, never required:
  `abs(a - b) <= 1e-7 + 1e-7 * abs(b)`.
- NaN or Inf, shape or scene mismatches, and films whose bytes differ from
  their recorded SHA-256 always fail.
- Same-host CPU films in the host gate must be bitwise identical to the native
  Session references; Metal films use the practical criterion against them.
- Metal repeat renders are not bitwise identical (film accumulation uses
  atomics); record `repeatMaxAbsDifference`.
- Texture filtering: Metal's hardware bilinear filtering is not bit-exact with
  Cycles' CPU interpolation and can exceed the practical criterion on textured
  scenes (`starter/textured-plane`). The CPU kernels and the Metal backend are
  both gold standards (native/DEVICE.md, "Validation reference policy"): record
  the Metal-versus-CPU numbers per scene as information, never as a failure,
  and never change Metal's sampling to hide them.
- Which film is the authority depends on the device under test. The CPU is the
  authority for the CodeBrix WGSL/WebGPU device, which this repository builds.
  Cycles' own Metal device (osx-arm64) is upstream's validated implementation:
  the managed GPU tests judge a Metal film against the reviewed Metal reference
  recorded for it (`References/metal-osx-arm64`, step 7) with the practical
  criterion, and compare and report the CPU film without requiring a match. A
  Metal render is therefore never failed only for differing from the CPU; a
  Metal film that drifts from its own recorded reference is.
- A scene where two reference backends disagree beyond the practical
  criterion is a finding for the maintainer, never something to resolve by changing
  settings, thresholds or scenes on the Mac.
- Tolerances are never relaxed.
- The glibc device-scene reference flow of the Android and Linux ARM64 WebGPU
  devices does not apply here: this profile has no WGSL kernels.

## 6. Oracle bundle to bring back

Layout of a run directory (and of the bundle and the published evidence
directory), per the oracle output contract:

```
manifest.json                         SHA-256 of every file (written by --bundle)
run.json                              backends, scenes, render seconds, failures,
                                      kernelCompileSeconds, comparisonsPassed
oracle/<scene id>/<backend>/
  film-0.f32, film-1.f32, ...         scene-linear RGBA float32, little-endian,
                                      width*height*4 values, lower-left origin
  meta.json                           scene and film SHA-256, repeat statistics,
                                      render seconds, host library and runner
                                      SHA-256, repository commit and cleanliness,
                                      OS, CPU, GPU, compiler, Xcode, CMake,
                                      CPU kernel compile flags, Metal settings
oracle/<scene id>/<backend>.output.txt   the runner's output
warmup/<backend>/                     the first (kernel-compiling) Metal render
comparisons/<scene id with / as __>.json pairwise compare.py reports
bundle/cycles-oracles-macos-<date>.tar.zst
```

Scenes are `cbcOracleScene` JSON files under `native/oracle/scenes/<family>/`
(`starter/`, `linux-gates/` with its `manifest.json` of scene and pixel-file
hashes). Backend names are `cpu-arm64-macos`, `metal-m2pro` and
`metal-m2pro-metalrt` here, `cpu-x64-linux` and `wgsl-linux-intel` on the
Linux machine.

Bring back:

1. `bundle/cycles-oracles-macos-<date>.tar.zst` and the SHA-256 printed by
   step 6.
2. The published evidence directory `native/evidence/cycles-oracles-macos-<date>/`
   (or its suffixed name) created by step 6.
3. The host gate directory `native/work/macos-validation-<n>/` minus
   `managed-bin/` (built executables are not evidence; record their SHA-256
   from `commands.json` and `results.json` instead).
4. The build records listed in section 3, plus
   `native/work/macos/osx-arm64/host/platform.patch` and the configure and
   build logs under `native/work/macos/osx-arm64/host/`. The repository
   ignores `*.log`; copy each log as `<name>.output.txt` and list the
   renames in the record.
5. The managed build and test output (section 4, step 7).
6. The identity record and the walls (section 7).

Where it goes: the oracle evidence directory from step 6 is already a new
directory. Put items 3 to 6 in ONE new directory, for example
`native/evidence/device-macos-arm64-<date>/`, never inside an existing
`native/evidence/*` checkpoint. Never copy executables or libraries into
evidence; record their SHA-256.

On the Linux machine, the films are compared with CPU-x64 and WGSL films of
the same scenes, rendered there with:

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/<linux run> \
  --runner native/work/cycles-build/bin/codebrix_cycles_oracle \
  --host-library native/work/cycles-build/lib/libcodebrix_cycles_host.so \
  --backend cpu-x64-linux --backend wgsl-linux-intel
python3 native/oracle/compare.py \
  native/work/oracles/<linux run>/oracle/<scene id>/cpu-x64-linux \
  native/evidence/cycles-oracles-macos-<date>/oracle/<scene id>/cpu-arm64-macos \
  native/evidence/cycles-oracles-macos-<date>/oracle/<scene id>/metal-m2pro \
  native/work/oracles/<linux run>/oracle/<scene id>/wgsl-linux-intel \
  --json native/work/oracles/<linux run>/four-way-<scene>.json
```

The Milestone 1 bench scenes are `native/oracle/scenes/assets/bench_01.json`
and `bench_01-hdri.json`, and the asset they are built from is in the repository
(`native/oracle/scenes/assets/ForestVillage/`, CC0, see its `PROVENANCE.txt`).
Generate the bundle on the Mac with
`python3 native/oracle/scenes/make_bench_bundle.py --output native/work/oracle-assets/bench_01`
(needs .NET and the glTF example tool; the script fails if any generated raw file
differs from `scenes/assets/manifest.json`, in which case copy the verified Linux
bundle directory instead of re-decoding, as `native/oracle/README.md` says) and
render both documents with `--scene` on both machines. No comparator ingests two
whole bundles and produces the four-way table for every scene at once;
`compare.py` works one scene's directories at a time.

## 7. What to record

Identity (save the output in the record directory):

```sh
sw_vers
uname -a
sysctl -n machdep.cpu.brand_string hw.memsize hw.ncpu
system_profiler SPHardwareDataType SPDisplaysDataType
xcodebuild -version
xcode-select -p
xcrun --show-sdk-version
clang --version
cmake --version
python3 --version
zstd --version
dotnet --info
git rev-parse HEAD
git status --porcelain
```

Per step: the exact command, start time, wall seconds, exit status and the
output directory. For the oracle run also `kernelCompileSeconds` per Metal
backend from `run.json`.

Failures: the failing step, the last lines of its `.stderr.txt`, the
`status.json` `error` field, and a reproducer (the exact command and
environment, including any `CYCLES_*` variables). For an oracle scene
failure, the scene id, backend and `<backend>.output.txt`. For a comparison
failure, the `compare.py` JSON (worst pixel, maximum error, practical and
strict counts).

Metal kernel compile diagnostics appear in each `meta.json` `warnings`;
bring them back verbatim.

## 8. Standing rules

- Never commit evidence into a frozen checkpoint: every existing
  `native/evidence/*` directory is immutable. New output goes to new
  directories only.
- Never relax a tolerance, edit a film, rerun until a comparison passes, or
  change scenes or Metal settings to make backends agree.
- Never modify the shipped CodeBrix.WebGpu.Native runtime or the pinned
  snapshots under `third_party/`.
- Never commit from the test machine without reviewing the complete list of
  new and changed paths (`git status --porcelain`) first; build output under
  `native/work/` stays uncommitted (it is ignored).
- Record every MISSING step that blocked or limited the run.
