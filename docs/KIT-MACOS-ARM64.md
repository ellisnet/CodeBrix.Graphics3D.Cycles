# Apple Silicon kit: what is outstanding on the Mac

Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

The current work order for an Apple Silicon Mac. Read this file first, then
[TESTING-MACOS-ARM64.md](TESTING-MACOS-ARM64.md), which holds the commands and the judging rules this kit refers to by step
number. The first work order (base validation at the current pin, the feature survey, the host work for the new attribute
kinds and Gaussian splats) was carried out and integrated; it is kept as a record in
[KIT-MACOS-ARM64-history.md](KIT-MACOS-ARM64-history.md) and is **not** a list of work to do.

This kit is rewritten whenever work on another platform leaves something that only an Apple Silicon Mac can do. If every
item below is already done on `main` (section 5 says how to tell), there is nothing outstanding: say so and stop.

## 1. Why there is work

Since the osx-arm64 payload under `native/binaries/osx-arm64` was staged, `main` has changed in ways that reach this
platform:

- **Shared host sources changed** (`native/host/host_api.inc` among them): refusals of the WebGPU backend were lifted and
  the capability report shortened. Such files are in the Cycles-with-Metal source list
  (`tools/licensing/metal_sources.py`), so the staged osx-arm64 payload is **stale**: a Release build prints the pack
  driver's "payload is STALE" warning for it and lists the files.
- **Three managed render cases have no recorded Metal reference.** The test
  `gpu_renders_the_integer_and_component_nodes_like_the_cpu` (`tests/CodeBrix.Graphics3D.Cycles.Tests/GpuRenderTests.cs`)
  renders a graph with a linked Boolean Math, Integer Math or Get Vector Component node. On the WebGPU backend its film is
  judged against the CPU film. On Cycles' Metal device the authority is a recorded Metal reference, and none exists for
  these three films, so on Metal the test only checks that the render is finite.
- **Oracle scenes were added** under `native/oracle/scenes/` (integer and component nodes, glass with the multiscatter
  distribution, open volume meshes, degenerate area lights, a Bump node read by an emission, and others). None has been
  rendered on CPU-arm64 or on Metal.

Nothing in the WebGPU device or the shared WGSL is this platform's work.

## 2. Rules

1. **Branch.** Work on a branch made from the commit of `main` that contains this document, never on `main`. Record that
   commit's hash as the **base commit**. `git status --porcelain` prints nothing before the first step.
2. **The mandatory test rules** at the top of [TESTING-MACOS-ARM64.md](TESTING-MACOS-ARM64.md) apply: .NET SDK 11.0.100
   only (projects keep targeting `net10.0`), 0 warnings, the WebGPU runtime only from its package at the pinned version.
   Never `sudo`; install nothing the maintainer has not already installed; a missing tool is reported, not installed.
3. **The pinned source is never edited by hand.** Nothing under `third_party/cycles/` changes. A compile error in a pinned
   file is reported with the compiler's text.
4. **Licence.** Nothing incompatible with Apache-2.0 enters the repository, code or data, including from memory. No new
   NuGet package and no new third-party source. Run `python3 tools/licensing/audit.py` before calling anything finished
   and read its `"passed"` field.
5. **Never approximate, never relax.** No tolerance, comparator, scene or gate expectation is changed to make something
   pass. The judge of every film is the x64 CPU film where one is committed; Metal films are judged by the reference
   policy of the testing document, section 5. A difference between Metal and the CPU is reported, never hidden.
6. **What this kit does not touch:** `native/shaders`, `native/cycles`, `native/src`, the host sources, gate and comparator
   sources, existing folders under `native/evidence/` (new folders only), the notices files, every other platform's recipe
   and payload. If one of the steps below seems to need such a change, stop and report.
7. **Repository documents** describe the product for "the maintainer": no personal names, no working notes.
8. **Stop conditions.** Stop and write the report when the base build cannot be made to pass without breaking a rule
   above, or a licence or provenance doubt appears. One failed scene never stops a run: record it and continue.

## 3. The work, in order

### A. Build and validate at the base commit

Steps 1 to 6 of the testing document (section 4), unchanged and in its order: the identity record, the build, the macOS
host gate, the oracle run on its three backends over the whole committed corpus, the comparison with the previous macOS
oracle evidence, and the oracle bundle.

What to establish beyond a pass:

- every film that existed in the previous macOS evidence: whether its CPU-arm64 film is bitwise what it was. A film that
  moved is a finding, with the scene named; do not explain it away;
- every scene that is new since that evidence: its CPU-arm64 film, its Metal film, and their comparison
  (`native/oracle/compare.py`, reported). A scene Metal refuses or renders non-finite is a finding with the verbatim text;
- for the degenerate area lights, the zero-extent portals and the zero-area bake scenes in particular: say for each scene
  whether Metal renders it and how far it is from the CPU film. The WebGPU device's remaining refusals are decided by
  what Metal does here.

### B. The three Metal references

1. Managed Release build and tests (step 7 of the testing document) at the base commit, WITHOUT the recording variable:
   record the result. The three cases named in section 1 pass on Metal by the finite check alone.
2. In `gpu_renders_the_integer_and_component_nodes_like_the_cpu`, remove the Metal-only branch so that both backends go
   through `Judge` with the names `integer-and-component-nodes-boolean-math`, `integer-and-component-nodes-integer-math`
   and `integer-and-component-nodes-get-vector-component`, and correct the comment above it. This is the only source
   change this kit asks for. It comes before the recording because a recording run keeps a film only for a name that
   reaches the reference judge.
3. Rebuild, then record, as step 7's "Recording the Metal references" describes:

   ```sh
   CODEBRIX_CYCLES_RECORD_METAL_REFERENCES=1 dotnet test --project tests/CodeBrix.Graphics3D.Cycles.Tests \
     --configuration Release --no-build --filter-class "*GpuRenderTests"
   ```
4. After recording, review `git diff` of `References/metal-osx-arm64/manifest.json`: exactly three new entries are
   expected, each with its CPU comparison. **Any other film that changed is a finding**: report its name and both
   comparisons, and do not accept it silently (the scenes and settings of the existing references did not change).
5. Run the tests again without the variable, Release and Debug: every test passes, 0 warnings.

### C. Staging the payload: only on the maintainer's decision

Step 9 of the testing document. Do NOT stage unless the maintainer has said so for this visit. If not staged, the report
says that the osx-arm64 payload is still stale and names the stale files the pack driver lists.

### D. Closing checks

`git diff --check` is clean; `python3 tools/licensing/audit.py` passes (step 8 of the testing document); the
`nugets/Release/<version>/` folder a Release build wrote is deleted; the branch holds nothing this kit did not ask for.

## 4. The report and what comes back

Everything comes back ON THE BRANCH, as the earlier Mac runs did: the oracle bundle and the device records in NEW folders
under `native/evidence/` named as sections 6 and 7 of the testing document say, each with a `README.md` and a
`SHA256SUMS.txt`; the three reference films and the manifest under `tests/.../References/metal-osx-arm64`; the one test
change of step B.2. Absolute host paths are written as `<repo>` and `<home>`; no personal names. The maintainer commits;
nothing is pushed or merged by the agent.

The report is the `README.md` of the new device-records folder, facts first and inferences marked:

- the base commit, the branch, the machine, the OS and toolchain versions the testing document asks for, wall times;
- part A: counts (scenes rendered, bitwise equal to the previous evidence, moved, new, failed), the list of every moved,
  failed or refused scene with the verbatim text, and the table for the degenerate-light, portal and zero-area bake scenes;
- part B: the three manifest entries, any other film that changed, the test results before and after (total, passed,
  skipped: the WebGPU-only tests skip on this platform);
- part C: staged or not, and why;
- everything that did not run, with the reason; every question for the maintainer, in prose.

The status paragraph at the top of the testing document is updated with this visit's facts only if the payload was
staged; otherwise it is left as it is and the report says so.

## 5. How to tell that nothing is outstanding

All of these hold on the commit being read: `References/metal-osx-arm64/manifest.json` holds the three names of step B.2;
the test named in section 1 has no Metal-only branch; a Release build prints no "payload is STALE" line for osx-arm64.
