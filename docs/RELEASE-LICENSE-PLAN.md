# WGSL repository publication readiness

Copyright (c) 2026 Jeremy Ellis and contributors

SPDX-License-Identifier: Apache-2.0

The governing policy is Jeremy's October 18, 2026
`~/ClaudeHome/Apache-2.0 License Plan for Cycles Build.md`. Original contributions
are Apache-2.0; permissive third-party source retains its own license. No Git
commit, push or history rewrite is authorized. Metal work is out of scope.

The bounded native scene/scheduler rendering proof is complete. The repository
publication-readiness gates also passed on October 18, 2026 under the approved
policy. See `native/evidence/license-compliance-20261018` and the new
`native/evidence/license-clean-render` checkpoint. The four acceptance gates were:

1. Review and import the required dependency source snapshots, with pinned
   origins, per-file hashes, license bases, modifications and exclusions.
2. Reconcile a complete attribution manifest and regenerate root `NOTICE` and
   `THIRD-PARTY-NOTICES.txt`, covering current source, embedded components,
   retained evidence/test captures and applicable external runtime notices.
3. Review proposed Git contents, history and remaining development artifacts;
   require passing source checks and resolved deep-scan findings.
4. Build from a fresh publication candidate offline with personal checkouts
   unavailable, audit actual source/header/link/runtime inputs, and rerun the
   established rendering and GPU replay checks without relaxing their criteria.

## Implemented source changes

- `third_party/cycles` is a filtered snapshot of the pinned upstream
  revision, the released Cycles v5.3.0 (`third_party/cycles-import.json` lists
  every file). The excluded atomic, dependency-target and maintenance files are
  absent. Independent Apache-2.0 atomic and build adapters replace required APIs.
  The CIE standard illuminant D65 data table, which the CIE publishes under
  CC BY-SA 4.0, is not imported, nor is anything derived from it; the kernels are
  therefore built without spectral sampling and dispersion is not available. Two
  functions the release marks as adopted from Ceres Solver carry that project's
  BSD-3-Clause notice (`third_party/licenses/CeresSolver-BSD-3-Clause.txt`).
  MAINTAINER-README.txt, "THE CYCLES PIN", describes the import.
- Atomic tests cover integer wraparound, compare/exchange, float representation
  edge cases, multithreaded film accumulation and memory accounting. Linux and
  API 33 Android targets passed the replacement tests. The repository-contained
  rebuild and fresh offline host preserve the original rendering results.
- Eighteen dependency snapshots live in `third_party/deps`. See
  `dependency-imports.json` and `dependency-license-review.json` for exact inputs
  and license decisions. The normal build uses these sources, not personal
  source checkouts, Blender prebuilt libraries or automatic source downloads.
- The dependency recipe disables the unwanted plugin/codec chains. The unused
  legacy TIFF LZW implementation is excluded; the TIFF metadata-header profile
  has LZW/JBIG disabled and the OIIO TIFF plugin remains disabled. The existing
  renderer functionality remains the regression baseline.
- Root notices are generated from `third_party/dependencies.json`. Full original
  license texts and per-source embedded notice supplements are included.
- Old extracted dependency prefixes, old build trees and 43 superseded development
  executables/libraries have been quarantined
  under `~/ClaudeHome/cycles-license-migration-20261018`, outside the repo.
- Historical evidence is immutable. Existing notice documents and old blocker
  messages are retained as documentary provenance, not permission to include
  the excluded implementation. Keyword exceptions are exact-file-hash reviews.

## Required verification and evidence

Run `python3 tools/licensing/audit.py` and
`python3 tools/licensing/notices.py --check` when external code or content enters the repository (a new or updated
third-party dependency or source snapshot, vendored code, a new binary package or runtime, a new outside asset).
Porting or changing code already in the repository, and adding renders, evidence or documents, does not need them
(MAINTAINER-README.txt, "Licensing and source checks"). The source audit verifies imported
inventories, notices, proposed Git files and exact reviewed wording exceptions.
Deep scanning, compiler/link closure and rendering checks are separate gates;
a keyword scan alone is not sufficient.

`tools/licensing/offline_build.py` copies precisely the proposed Git contents
without `.git` or existing work directories. It builds in private network and
mount namespaces, with only loopback networking and the specified old source
locations hidden. Compilers, platform SDKs/toolchains, explicitly allowed system
runtimes and the pinned unchanged WebGPU package remain external inputs.

`tools/licensing/binary_audit.py` records compilation databases, compiler header
inputs, static archives and resolved ELF dependencies. Dynamic system glibc and
GCC runtimes under the explicit Runtime Library Exception are permitted by
Jeremy's policy. Other renderer dependencies must resolve to the source-built
prefix. Android replay uses the unchanged shipped runtime and NDK API 33.

Durable reports are in `native/evidence/license-compliance-20261018`; working
copies are in `native/work/license-clean`. All 18,370 imported dependency files
match the reviewed manifest and passed the deep scan. The publication review
records 72 special scanner findings and their resolutions. Two large generated
documents exceeded the monolithic scan limit; 304 overlapping chunks passed
with exact byte coverage, and all 74 final full-text notice inputs have error-free
scans. The 33-group root notices and 19 source-comment supplements regenerate
exactly. The original timeout reports remain in the evidence.

The fresh offline build passed with personal checkouts hidden, only loopback
networking, no previous build outputs and no source mutations. Its actual
compiler/header/archive/link/runtime audit passed. Both freshly built proof
scenes preserve the original CPU films, GPU films and scheduler commands
byte-for-byte. Current complete-command replay passed on Linux and all three
Android devices through the unchanged package, including API 33. Strict float
parity and full Cycles functionality remain unproven; tolerances were not relaxed.

The proposed source contents and existing Git history have been reviewed. Older
license inventories and blocker descriptions are preserved as documentary
evidence, without importing the excluded implementations. This is readiness for
Jeremy's source-repository review/commit/push under the agreed policy, not a
clearance of unbuilt native RIDs or an actual future NuGet payload. Agents have
not committed, pushed, staged files or rewritten history.

## Future native deliverables

OpenImageDenoise (denoising) is a dependency (OIDN build conditions). Its licence
check, trained weights included, passed for version 2.5.1, on four build
conditions that keep it Apache-2.0: build it from a reviewed source snapshot in
`third_party/deps` (never a prebuilt package); turn on only its CPU device, plus
its Metal device on osx-arm64; set its CUDA, HIP and SYCL devices explicitly off
on every RID (a CUDA Runtime build installs Intel's proprietary binary licence
instead); and turn its apps off (`OIDN_APPS` OFF, `OIDN_INSTALL_DEPENDENCIES` OFF:
the package ships the reviewed oneTBB snapshot). The snapshot is
`third_party/deps/oidn`, imported from the release archive (SHA-256 pinned in
`tools/licensing/dependency_sources.py`) by its `add` action with the per-file
review `third_party/oidn-license-review.json`; the CUDA, HIP and SYCL devices,
their external libraries, the apps, the training scripts, the documentation
(including `doc/BINARY-LICENSE.txt`) and the notice inventories of components it
does not import are not in the snapshot. Three upstream build files carry
recorded CodeBrix adaptations (documentation install list, relocatable run path,
Android thread affinity). `tools/dependencies/build.py` builds it on every RID
with those switches set explicitly; its `LICENSE.txt` and the weights' licence
ship with its libraries (the device modules are separate shared libraries and
ship with the core library). ISPC (BSD-3-Clause) compiles its CPU kernels at
build time and is not shipped. Only the RT filter's weights are built
(`OIDN_FILTER_RTLIGHTMAP` OFF on every RID: Cycles creates only the RT filter);
the lightmap weights remain in the reviewed snapshot but are not compiled in.

Embree (the BVH of the product CPU device; the CPU oracle keeps BVH2) is a
dependency (Embree build conditions). Its licence check passed for version 4.4.1
(the release's source archive, the series the pinned Cycles is written against,
`RTC_VERSION >= 40400`): every imported file is Apache-2.0 (Intel, plus three
Kitware CMake helpers) except `common/simd/arm/sse2neon.h`, which is MIT and is
compiled on ARM64 only. The build conditions: build it from the reviewed source
snapshot `third_party/deps/embree` (never a prebuilt release), imported from the
pinned archive by the `add` action of `tools/licensing/dependency_sources.py` with
the per-file review `third_party/embree-license-review.json`; set
`EMBREE_SYCL_SUPPORT`, `EMBREE_ISPC_SUPPORT` and `EMBREE_TUTORIALS` explicitly off
on every RID; use the reviewed oneTBB snapshot as its tasking system (no second
TBB); build it as a static library linked into the product CPU device. The SYCL
GPU kernels and their Level Zero headers (MIT), the tutorials and their
third-party libraries, the tests, the superbuild, the documentation, the man
pages, the packaging scripts and the notice inventories of the binary releases
(which include Intel's oneAPI DPC++ EULA notices and the Visual C++
redistributables) are not in the snapshot. Two upstream build files carry
recorded CodeBrix adaptations (the test tree omitted, the documentation install
list). Its `LICENSE.txt` and the sse2neon MIT notice are listed in
THIRD-PARTY-NOTICES.txt.

All nine intended RIDs remain in the continuation plan. This Linux proof does
not validate unbuilt native hosts. Every actual NuGet payload requires its own
unpack/hash/notice/dependency audit before publication.

Jeremy requires intentional RID runtime binaries, applicable unstripped builds
and symbols to be versioned under `native/binaries/<rid>/`, with exact reviewed
provenance. Ordinary intermediate outputs remain ignored. The current source
scanner rejects unreviewed compiled payloads; introduce exact approved-artifact
verification when those delivery binaries are added. There are no release
binaries in that directory yet, and agents still must never commit or push.
