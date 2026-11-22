# Apple Silicon kit: what is outstanding on the Mac

Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

The current work order for an Apple Silicon Mac. Read this file first, then
[TESTING-MACOS-ARM64.md](TESTING-MACOS-ARM64.md), which holds the commands and the judging rules this kit refers to by step
number. The two earlier work orders were carried out and integrated; they are kept as records in
[KIT-MACOS-ARM64-history.md](KIT-MACOS-ARM64-history.md) and are **not** lists of work to do.

This kit is rewritten whenever work on another platform leaves something that only an Apple Silicon Mac can do. If every
item below is already done on `main` (section 5 says how to tell), there is nothing outstanding: say so and stop.

## 1. Why there is work

The last visit (report: `native/evidence/device-macos-arm64-20261117/README.md`) built the product at its base commit with
0 warnings, passed the macOS host gate, rendered the 18 oracle scenes that were new since the previous macOS oracle
evidence on the three macOS backends (`native/evidence/cycles-oracles-macos-20261117/`), and recorded the three Metal
references of the integer and component node test. Every other committed oracle scene of that time, the integer and
component node, degenerate area light, zero-extent portal and zero-area bake scenes among them, had been rendered on the
three backends before, with no failure (`native/evidence/cycles-oracles-macos-20261115/`).

What that visit left, by the scope the maintainer set for it, and what has changed since:

- **The osx-arm64 payload is stale.** Shared host sources (`native/host/host_api.inc` among them) changed after the payload
  under `native/binaries/osx-arm64` was staged, and it was not restaged. Such files are in the Cycles-with-Metal source
  list (`tools/licensing/metal_sources.py`): a Release build prints the pack driver's "payload is STALE" warning for
  osx-arm64 and lists the files.
- **The oracle run over the whole corpus has not been repeated since those host sources changed.** Only the 18 new scenes
  were rendered at the last visit. Whether every earlier CPU-arm64 film is still bitwise what the previous evidence holds
  has therefore not been checked at a commit with the changed host sources, and the bench documents were not rendered.
- **The consumer test of the MacOSArm64 package** (step 7 of the testing document) did not run.
- **The Metal reference recorder changed and its recording path has never run.** At the last visit the recording command
  rewrote every reference the run reached, not only the missing ones, and the rewritten films had to be put back by hand.
  The recorder (`GpuReferences` in the test project) now writes only the names that have no reference when the recording
  variable is `1`, and re-records every name only when it is `all` (testing document, step 7, "Recording the Metal
  references"). Its rules are tested on every platform in a scratch folder (`GpuReferencesTests`), but the recording of
  real Metal films only runs here: this is **TO CONFIRM ON THE MAC** (part B).
- **Scenes and managed render tests added since the last visit** are listed in the section "Added by the port work of this
  cycle" below. A scene in that list has never been rendered on CPU-arm64 or on Metal, and a managed render test in it has
  no recorded Metal reference.

Nothing in the WebGPU device or the shared WGSL is this platform's work.

## Added by the port work of this cycle

New oracle scenes (part A: each needs its CPU-arm64 and Metal films and their comparison; the comparison with the
previous evidence decides what is new, this list names what this cycle added):

- `bsdf/`: the Glass BSDF node with a thin film (`glass-thin-film`, `glass-thin-film-linked`) and the dispersive Principled
  BSDF beyond the scene the Mac rendered before (`principled-dispersion-rough`, `principled-dispersion-linked`,
  `principled-dispersion-zero-link`).
- `mnee/dispersion-caustic`, `mnee/dispersion-rough-caustic`; `volumes/dispersion-in-medium`;
  `shadow-catcher/catcher-dispersion`; `denoise/principled-dispersion-on` and its control `denoise/principled-dispersion-off`.
- `surface-nodes/`: the rotated tangent of a Glossy or Glass BSDF (`tangent-node-rotation`, `tangent-rotation-linked`).
- `sky/`: a sky with a sun disc that no background light samples (`sun-disc-unsampled`, `sun-disc-unsampled-defaults`).
- `images/tiled-*`: tiled images (the texture cache) through the flat, box, sphere, tube and environment projections, in
  surface, shadow, light, displacement, background and volume evaluations. These scenes pin one render thread
  (`render.threads`), which the Metal backend ignores.
- `gsplat/`: the scenes that combine Gaussian splats with another feature (`beside-light-tree`, `beside-light-linking`,
  `beside-shadow-linking`, `shadow-linking-emissive`, `beside-shadow-catcher`, `shadow-catcher-splats`, `bake-combined`,
  `bake-diffuse`, `denoising-features`, `data-passes`, `motion-pass`, `denoise-on`, `beside-ao-node`, `beside-raycast`,
  `beside-caustics`).
- `lamps/area-overflow-*`: area lights whose axis length overflows (rectangle, ellipse with a spread, normalized, with a
  narrow spread in a volume, in a volume, under the light tree). What Metal does with the infinite axis is not known: report
  it as it comes.
- `bake/zero-area-flat-*`: bakes of texels on zero-area triangles of a flat-shaded target.
- Single scenes: `bsdf/sheen-microfiber` (the Sheen BSDF's microfiber distribution); `curves-b/shadow-limit-negative` and
  `curves-b/shadow-limit-wide` (transparent shadows at a negative and at a large transparent bounce limit);
  `displacement/unlinked` (a True displacement method with nothing linked to the Displacement output);
  `light-motion/motion-blur-static` (motion blur on with every mesh and the area lamp flagged for it and nothing moving);
  `subsurface/principled-thin-wall-linked`.

Managed render tests (part B):

- **Needs a new Metal reference:** `dispersion_renders_on_the_gpu_backend` (reference name `dispersion`). Until it is
  recorded it fails on Metal with "No recorded Metal reference" (step B.1).
- **Changed in their WebGPU branches; must still pass on Metal against their existing references:**
  `quaternion_and_spherical_harmonics_attributes_render_on_the_gpu_backends` and
  `quaternion_attributes_on_curves_render_on_the_gpu_backends_like_the_cpu` (reference
  `attribute-quaternion-spherical-harmonics`), `gaussian_splats_render_on_the_gpu_backends` (reference `gaussian-splats`)
  and `the_splat_bounding_radius_reads_on_the_gpu_backends_as_on_the_cpu`, `tiled_images_render_on_the_gpu_backend`
  (reference `tiled-image`) and `tiled_images_at_every_metal_tile_size_select_mip_levels_and_render_like_plain_images`.
  A failure of one of them is a finding.

Points of the ARM64 arithmetic to confirm, information for the comparison of part A (`native/DEVICE.md` item 246): the
rules of a bake texel on a zero-area triangle that depend on the oracle's ISA (the sign of the NaN normal and the
float-to-int conversion of `ray_offset`) were checked against the ARM64 CPU film of `bake/zero-area-ao` only by the
device's arithmetic switch on another machine; the CPU-arm64 films of `bake/zero-area-ao` and the new
`bake/zero-area-flat-ao` are the films such a check reads, so a moved or non-finite CPU-arm64 film of either is a finding.

The recorder's two confirmations (part B, steps 2 and 4) and the rest of this kit stand as written.

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
   sources, the test sources, existing folders under `native/evidence/` (new folders only), the notices files, every other
   platform's recipe and payload. If one of the steps below seems to need such a change, stop and report.
7. **Repository documents** describe the product for "the maintainer": no personal names, no working notes.
8. **Stop conditions.** Stop and write the report when the base build cannot be made to pass without breaking a rule
   above, or a licence or provenance doubt appears. One failed scene never stops a run: record it and continue.

## 3. The work, in order

### A. Build and validate at the base commit

Steps 1 to 6 of the testing document (section 4), unchanged and in its order: the identity record, the build, the macOS
host gate, the oracle run on its three backends, the comparison with the previous macOS oracle evidence, and the oracle
bundle. The oracle run covers the **whole committed corpus and the bench documents**: the part the last visit left out is
the reason for it, so it is not narrowed to the new scenes unless the maintainer says so for this visit (if it is
narrowed, the report says which scenes did not run).

"The previous macOS oracle evidence" is two folders: `native/evidence/cycles-oracles-macos-20261115/` (the corpus of its
time) and `native/evidence/cycles-oracles-macos-20261117/` (the 18 scenes added after it). A scene in neither is new.

What to establish beyond a pass:

- every film that exists in the previous macOS evidence: whether its CPU-arm64 film is bitwise what it was. A film that
  moved is a finding, with the scene named; do not explain it away;
- every scene that is new since that evidence (the section "Added by the port work of this cycle" names the ones this
  cycle added; the comparison with the evidence is what decides): its CPU-arm64 film, its Metal film, and their
  comparison (`native/oracle/compare.py`, reported). A scene Metal refuses or renders non-finite is a finding with the
  verbatim text. What the WebGPU device may keep refusing is decided by what Metal does with these scenes.

### B. The Metal reference recorder and the new references

1. Managed Release build and tests (step 7 of the testing document) at the base commit, WITHOUT the recording variable:
   record the result. A managed render test that the section "Added by the port work of this cycle" names fails on Metal
   here with "No recorded Metal reference": that is expected, and it is the only failure that is. Any other failure is a
   finding.
2. **TO CONFIRM ON THE MAC: the recording variable set to `1` writes only missing names.** Record:

   ```sh
   CODEBRIX_CYCLES_RECORD_METAL_REFERENCES=1 dotnet test --project tests/CodeBrix.Graphics3D.Cycles.Tests \
     --configuration Release --no-build --filter-class "*GpuRenderTests"
   ```

   Then `git status --porcelain tests/CodeBrix.Graphics3D.Cycles.Tests/References/metal-osx-arm64` and `git diff` of
   `manifest.json`. Confirmed means all of: every film file that was committed is byte-identical (no ` M` line for an
   `.rgba.f32` file); the manifest diff holds added entries only, one per missing name, and no changed line inside an
   existing entry; the new film files are exactly those names. If no name was missing, confirmed means that `git status`
   prints nothing for that folder. **Any existing film or entry that changed is a finding**: report its name and both
   comparisons, and stop part B (do not put files back by hand and carry on as if it had passed).
3. Review each new entry (its CPU comparison is reported, not required) and run the tests again without the variable,
   Release and Debug: every test passes, 0 warnings.
4. **TO CONFIRM ON THE MAC: the setting `all` re-records every name.** This step proves the second setting and must leave
   the references as step 3 left them. Copy the references folder to a scratch folder outside the repository, run the
   command of step 2 with `CODEBRIX_CYCLES_RECORD_METAL_REFERENCES=all`, and record: how many manifest entries have a new
   `recordedUtc` (expected: every name the run judged), how many films changed, and for each changed film its comparison
   with the committed film (the last visit measured at most 9.98e-5, within the practical criterion). Then copy the
   scratch folder back over the references folder and check that `git status` for it shows exactly what it showed after
   step 3. The re-recorded set is **not** kept: there is no new pin.
5. A value that is neither setting (for example `true`) must fail the judged tests with a message naming the two
   settings: run one test of `GpuRenderTests` that way and quote the message.

### C. The consumer test of the package

The consumer test of step 7 of the testing document (the MacOSArm64 package restored into a project outside the
repository, both render modes, the negative control), with its layout and results recorded.

### D. Staging the payload: only on the maintainer's decision

Step 9 of the testing document. Do NOT stage unless the maintainer has said so for this visit. If not staged, the report
says that the osx-arm64 payload is still stale and names the stale files the pack driver lists.

### E. Closing checks

`git diff --check` is clean; `python3 tools/licensing/audit.py` passes (step 8 of the testing document); the
`nugets/Release/<version>/` folder a Release build wrote is deleted; the branch holds nothing this kit did not ask for.

## 4. The report and what comes back

Everything comes back ON THE BRANCH, as the earlier Mac runs did: the oracle bundle and the device records in NEW folders
under `native/evidence/` named as sections 6 and 7 of the testing document say, each with a `README.md` and a
`SHA256SUMS.txt`; the new reference films and the manifest under `tests/.../References/metal-osx-arm64`. This kit asks for
no source change. Absolute host paths are written as `<repo>` and `<home>`; no personal names. The maintainer commits;
nothing is pushed or merged by the agent.

The report is the `README.md` of the new device-records folder, facts first and inferences marked:

- the base commit, the branch, the machine, the OS and toolchain versions the testing document asks for, wall times;
- part A: counts (scenes rendered, bitwise equal to the previous evidence, moved, new, failed), the list of every moved,
  failed or refused scene with the verbatim text, and the CPU-to-Metal table of the new scenes;
- part B: the test results before and after (total, passed, skipped: the WebGPU-only tests skip on this platform); for
  step 2, the `git status` and manifest diff that confirm or refute "only missing names", and the new entries; for step 4,
  the counts and comparisons of the re-recording and the check that the references were left as step 3 left them; for
  step 5, the message;
- part C: the layout and the render results per mode;
- part D: staged or not, and why;
- everything that did not run, with the reason; every question for the maintainer, in prose.

The status paragraph at the top of the testing document is updated with this visit's facts only if the payload was
staged; otherwise it is left as it is and the report says so.

## 5. How to tell that nothing is outstanding

All of these hold on the commit being read: the section "Added by the port work of this cycle" names nothing that lacks a
film in the macOS oracle evidence or a reference in `References/metal-osx-arm64/manifest.json`; an evidence folder later
than `native/evidence/device-macos-arm64-20261117/` reports the whole-corpus oracle run, the consumer test and the two
recorder confirmations of part B; a Release build prints no "payload is STALE" line for osx-arm64.
