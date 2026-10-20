# Linux ARM64 cross-build and CPU validation

Copyright (c) 2026 Jeremy Ellis and contributors

The maintained recipe builds the current native Cycles host, reference/test
executables and WGSL device code for Linux ARM64 from the reviewed repository
snapshots. It uses the unchanged `linux-arm64` WebGPU package binary. It does not
establish native ARM64 GPU execution or complete Cycles feature coverage.

On the current Linux x64 build host, the agreed external inputs include GCC 14's
`aarch64-linux-gnu-gcc-14`/`g++-14`, the installed `/usr/aarch64-linux-gnu` target
SDK and `qemu-aarch64`, plus the existing CMake/build tools. Compiler, SDK runtime
and emulator hashes are recorded. No dependency source is fetched or obtained
from another checkout. The same 18-component source profile is used by the
Linux x64 and Android recipes.

From the repository root:

```sh
python3 tools/build_linux_arm64.py --jobs 4
python3 native/tools/linux_arm64_host_gate.py \
  --output native/work/linux-arm64-validation-v1 \
  --references native/work/host-validation
```

The reference directory must come from a completed, passed maintained Linux
`host_gate.py` run. Its pinned fixture inputs and reference assets are verified.
Use a new validation output directory for each run. `--skip-dependencies` reuses
only a complete matching target prefix. Selected native targets can be built
with repeated `--target` arguments to `native/tools/build_cycles_linux_arm64.py`.

Target artifacts and source copies live under
`native/work/linux/linux-arm64/{dependencies,host}`. Cross-target CPU detection
and the NEON half-conversion include are adapted in a generated Cycles copy;
the pinned snapshot is unchanged and upstream notices are preserved.

The CPU gate reviews compiler sources and observed headers, archives, ELF
architecture/segments, all 36 public exports and the closure resolved by the
emulated target loader. It then exercises independent native Session references,
C lifecycle/process exit, six color-configuration reloads and concurrent host
lifetimes under QEMU. Same-target CPU reference comparisons require bitwise
identity. Cross-platform images retain the established practical criterion and
strict diagnostics; cross-platform lifecycle films also require strict agreement.

QEMU execution proves the tested emulated CPU paths. It does not prove native
hardware behavior, ARM64 GPU/driver behavior, managed consumer execution or a
portable Linux release ABI baseline. The current SDK baseline and target runtime
requirements must be reviewed before release. Test artifacts here are development
outputs; intentional reviewed RID deliverables later belong in `native/binaries/`.
