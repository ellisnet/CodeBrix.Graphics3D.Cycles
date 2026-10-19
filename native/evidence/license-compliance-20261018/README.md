# Repository publication review — October 18, 2026

Copyright (c) 2026 Jeremy Ellis and contributors

This checkpoint records the source, attribution, build and rendering review
under Jeremy's Apache-2.0 compatibility policy. The outcome is recorded in
`validation.json`; individual reports retain their actual findings and limits.
It covers the proposed repository and the bounded Linux native rendering proof,
including Android API 33 GPU command replay. It does not certify unbuilt native
RIDs, a future NuGet payload, or full Cycles functionality.

## Source and notices

The repository contains the filtered 944-file Cycles snapshot and 18 dependency
snapshots comprising 18,370 files. Their exact origins, revisions, exclusions,
changes and selected license bases are recorded in the two import manifests.
The dependency deep scan matches every imported SHA-256, with no scan errors.
The review records resolve scanner ambiguities against the actual retained
grants, including documentation-only licenses and explicitly selected permissive
alternatives. Third-party code retains its original licenses.

Non-dependency-source files were scanned by exact-content deduplication, with
separate scans for subsequent changes. The monolithic scan timed out on two
large generated documents. The original failure report is retained; overlapping
chunk scans cover those metadata bytes, and deterministic generation verifies
the final notices against separately reviewed, hash-pinned inputs. No scanner
timeout is silently treated as a passing source-license finding.

Root notices cover 33 attribution groups, full component licenses, embedded
source notices, test-geometry provenance and applicable external runtime/SDK
exceptions. Nineteen source-comment supplements cover Cycles and all 18
dependencies. Historical notice inventories and old blocker descriptions remain
documentary evidence; they are not permission to import excluded implementations.

## Offline build and runtime evidence

The fresh build used private network and mount namespaces, only loopback
networking, no existing build directories, disabled compiler caching, and hidden
personal source/build locations. The recorded input manifest identifies the
exact publication snapshot. Later notice/audit-document changes are listed
separately; renderer, dependency, translator and build-recipe inputs must match.

Allowed external inputs are compilers/build tools, platform SDKs, the documented
system runtimes and the unchanged shipped WebGPU package. Actual compilation
databases, object header dependencies, static archives, linker commands and
resolved ELF dependencies are recorded. Direct compiler invocations also have
dependency-only header replay and ELF/runtime checks. Android evidence covers
API 33 replay executables, not complete native Android Cycles hosts.

The fresh host renders both proof scenes repeatedly. Its CPU films, GPU films
and scheduling commands are byte-for-byte identical to the original frozen
baseline. The current checkout additionally replays both complete command
streams on Linux and all three Android devices. The practical numerical
tolerances are unchanged; strict numerical parity and full Cycles parity are
still false. The independent atomic test checks 1,024 original CPU film values.

## History and development outputs

The existing Git history contains only the initial `.gitignore`, `LICENSE` and
`README.md`. Earlier frozen evidence hashes remain unchanged. Superseded build
trees and 43 older development executables/libraries are preserved in the
external quarantine, with recorded paths and hashes. Current audited builds,
test data and render images remain available. No commit, push, staging or
history rewrite was performed.

Intentional future RID delivery binaries, applicable unstripped versions and
symbols belong in `native/binaries/<rid>/` with exact artifact review. Ordinary
Visual Studio/.NET outputs and build intermediates remain ignored. No release
binary is being added by this checkpoint.

## Rechecking

Run `python3 tools/licensing/audit.py`,
`python3 tools/licensing/collect_attributions.py --check`, and
`python3 tools/licensing/notices.py --check` from the repository root.
The saved command/build records identify the binary and rendering invocations.
Files ending in `.json.gz` or `.log.gz` are ordinary gzip-compressed evidence,
not compiled artifacts. `manifest.json` hashes every saved checkpoint file.
