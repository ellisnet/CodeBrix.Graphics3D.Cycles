# Linux ARM64 CPU oracle corpus in a Docker container (Apple Silicon Mac)

Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

> **MANDATORY TEST RULES** (the same as every TESTING document):
>
> 1. **WebGPU binaries: ONLY from the `CodeBrix.WebGpu.Native.MitLicenseForever`
>    1.0.300.210 NuGet package**, unmodified, from the NuGet cache. The CPU oracle
>    does not render with WebGPU, but the host library links the package's
>    `runtimes/linux-arm64/native/libcodebrix_wgpu.so`; no other copy or version.
> 2. **.NET SDK: ONLY 10.0.403** for any .NET step (this procedure needs none
>    inside the container).
> 3. **Linux x64 references come only from the Linux x64 machine.** This procedure
>    produces **Linux ARM64** CPU oracle films; it never produces or replaces Linux
>    x64 references.

## 1. Purpose

Renders the committed oracle corpus (`native/oracle/scenes/**`) with the
`cpu-arm64-linux` backend of `native/oracle/render_oracles.py`: the unmodified
Cycles CPU kernels at the fixed ARM64 oracle baseline (ARMv8-A base level, no FMA
contraction, no reciprocal math; native/DEVICE.md "Validation reference policy"),
GCC 14 and glibc. The result is the Linux ARM64 CPU oracle film set, committed as
a new frozen evidence folder, so that WGSL renders on ARM64 devices can be compared
against a CPU rendering of the same architecture.

These films are **evidence, not yet a gold standard**: which CPU oracle the WGSL
device on ARM64 is judged against is an open decision (see native/DEVICE.md). The
x64 gold standard stays `native/evidence/linux-x64-reference-<date>/`.

Why a container on the Mac and not the Raspberry Pi 5: the oracle is deterministic
for a given ISA baseline, compiler and C library, so any ARM64 machine gives the
same films. An Apple Silicon Mac runs `linux/arm64` containers natively (no
emulation) and builds and renders the corpus in minutes; the Pi 5 needs hours. The
Pi 5 remains the Linux ARM64 reference for the WGSL device on its own GPU
(docs/TESTING-LINUX-ARM64.md); it can use these films for its CPU comparisons.

## 2. Prerequisites and pins

On the Mac (the maintainer installs these; nothing is installed by the procedure):

- Docker Desktop for Mac (Apple Silicon), running `linux/arm64` containers natively.
- The repository checked out at the commit to be rendered (`git pull`), clean.
- The NuGet cache holding `codebrix.webgpu.native.mitlicenseforever/1.0.300.210`
  (restore it once on the Mac, e.g. `dotnet restore research/GpuProbe/GpuProbe.csproj`
  with SDK 10.0.403). The package contains every RID, including
  `runtimes/linux-arm64/native/libcodebrix_wgpu.so`.
- Free disk: about 15 GB in the checkout's `native/work/linux/linux-arm64/`.

Container image (pinned): **Debian 13 "trixie", arm64**, the same Debian release and
glibc (2.41) as the Linux x64 machine, so that the x64 and ARM64 oracles differ only
in the instruction set:

```sh
docker pull --platform linux/arm64 debian:trixie
```

Packages inside the container (the native-build requirements of
docs/TESTING-LINUX-ARM64.md "Native build on the device", minus the GPU parts):

```sh
apt-get update
apt-get install -y --no-install-recommends gcc-14 g++-14 clang cmake make time \
    python3 git ca-certificates file
```

Record `gcc-14 --version`, `ldd --version` (glibc), `cmake --version` and
`python3 --version`; the runner also records them in every film's `meta.json`.

## 3. Start the container

From the repository root on the Mac:

```sh
docker run --rm -it --platform linux/arm64 \
  -v "$PWD":/work/CodeBrix.Graphics3D.Cycles \
  -v "$HOME/.nuget/packages":/root/.nuget/packages:ro \
  -w /work/CodeBrix.Graphics3D.Cycles \
  debian:trixie bash
```

Then install the packages of section 2 inside it. The checkout is shared with the
Mac; the Linux ARM64 build writes only under `native/work/linux/linux-arm64/`,
which no macOS build uses. Confirm the native mode: `uname -m` prints `aarch64`.

## 4. Build (inside the container)

```sh
git config --global --add safe.directory /work/CodeBrix.Graphics3D.Cycles
python3 tools/dependencies/build_linux_arm64.py --jobs 8
python3 native/tools/build_cycles_linux_arm64.py --jobs 8 --device-set
RUNNER=$(find native/work/linux/linux-arm64 -name codebrix_cycles_oracle -type f -perm -u+x | head -1)
HOSTLIB=native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so
echo "$RUNNER"; ls -l "$HOSTLIB"
```

- The dependency build runs natively (`platform.machine()` aarch64): system GCC 14
  and glibc from the container, the in-repo pinned snapshots, no network.
- `build_cycles_linux_arm64.py` checks that the CPU kernels are at the fixed oracle
  baseline and fails otherwise. `--device-set` builds the default set (including
  the host library), every device gate and, natively, the oracle runner
  (docs/TESTING-LINUX-ARM64.md, "Native build on the device"). Record the
  `RUNNER` and `HOSTLIB` paths printed above.
- Budgets (INFERENCE, an M2 Pro with 8 jobs): dependencies about 15-30 min the first
  time, the Cycles build about 10-20 min; later runs are incremental.

## 5. Render the corpus (inside the container)

```sh
RUN=native/work/oracles/linux-arm64-<date>
```

- Without `--scene`, the runner renders every scene under `native/oracle/scenes/`.
  The two `assets/bench_01*` documents read raw files that are generated, not
  committed, so they cannot render in place. Generate the bench bundle on the Mac
  first (outside the container; docs/TESTING-MACOS-ARM64.md section 6 writes it to
  `native/work/oracle-assets/bench_01/`), then render the corpus without the two
  in-place bench documents and the bundle's two documents instead:

  ```sh
  scenes=(); for f in $(find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" | sort) \
      native/work/oracle-assets/bench_01/bench_01.json native/work/oracle-assets/bench_01/bench_01-hdri.json; do
    scenes+=(--scene "$f"); done
  python3 native/oracle/render_oracles.py --output "$RUN" \
    --runner "$RUNNER" --host-library "$HOSTLIB" \
    --backend cpu-arm64-linux "${scenes[@]}"
  ```

  Record any scene the runner refuses or fails, verbatim (the two shader-volume
  scenes render on the CPU backends; only the WebGPU device refuses them).
- Every film is rendered twice; `repeatBitwiseIdentical` must be `true` for every
  scene. Films are written once and never adjusted.
- Budget (INFERENCE): about 10-20 min for the whole corpus.

## 6. Compare with the Linux x64 CPU oracle (information only)

`compare.py` compares backend directories of one scene; loop over the scenes
(X = the Linux x64 evidence run of the same or the nearest commit, e.g.
`native/evidence/linux-x64-reference-<date>/oracles/corpus`; the bench scenes are
under its `oracles/bench`):

```sh
X=native/evidence/linux-x64-reference-<date>/oracles/corpus; mkdir -p "$RUN/vs-x64"
for d in "$RUN"/oracle/*/*/cpu-arm64-linux; do s=${d#"$RUN"/oracle/}; s=${s%/cpu-arm64-linux}
  [ -d "$X/oracle/$s/cpu-x64-linux" ] || { echo "no x64 film: $s"; continue; }
  python3 native/oracle/compare.py "$X/oracle/$s/cpu-x64-linux" "$d" --json "$RUN/vs-x64/${s//\//__}.json"
done
```

Record, per scene: identical / within the practical criterion / beyond it, with the
counts and maxima. Differences between the two CPU oracles are **findings to
record, never failures and never tuned away** (native/DEVICE.md open item 25:
x64 and ARM64 CPU oracles are known to differ on some scenes). Use the
`linux-x64-reference-<date>` folder of the same or the nearest commit and name it.

## 7. Evidence to commit

Copy the run into a NEW folder `native/evidence/linux-arm64-cpu-oracle-<date>/`
(never into an existing evidence folder): `oracle/<scene>/cpu-arm64-linux/`
(films and `meta.json`), `run.json`, the comparison JSON of section 6, and a
`README.md` with the commit, the image (`debian:trixie` digest from
`docker image inspect`), the tool versions of section 2, the walls, and the list of
refused or failed scenes. Then run `python3 tools/licensing/audit.py` and
`python3 tools/licensing/notices.py --check`, and commit.

## 8. What to record

- Commit hash and whether the tree was clean.
- Image digest, `gcc-14`, glibc, CMake and Python versions.
- Build and render walls; `uname -m`; the Mac model.
- Per-scene repeat identity, refusals, failures (verbatim).
- The section 6 comparison summary.
