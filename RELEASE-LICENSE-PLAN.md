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

- `third_party/cycles` is a filtered 944-file snapshot of the pinned upstream
  revision. The excluded atomic, dependency-target and maintenance files are
  absent. Independent Apache-2.0 atomic and build adapters replace required APIs.
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
`python3 tools/licensing/notices.py --check`. The source audit verifies imported
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

All nine intended RIDs remain in the continuation plan. This Linux proof does
not validate unbuilt native hosts. Every actual NuGet payload requires its own
unpack/hash/notice/dependency audit before publication.

Jeremy requires intentional RID runtime binaries, applicable unstripped builds
and symbols to be versioned under `native/binaries/<rid>/`, with exact reviewed
provenance. Ordinary intermediate outputs remain ignored. The current source
scanner rejects unreviewed compiled payloads; introduce exact approved-artifact
verification when those delivery binaries are added. There are no release
binaries in that directory yet, and agents still must never commit or push.
