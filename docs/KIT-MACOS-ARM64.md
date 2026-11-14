# Apple Silicon kit: full Cycles-with-Metal functionality at the current pin

Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

A work order for an Apple Silicon Mac. It is carried out on a **branch**, never on `main`, and it is written so that it can
run unattended. Statements marked **READ** were established by reading the source on Linux and have never been built or run
on a Mac; statements marked **UNKNOWN** are what this kit is meant to find out.

## 1. Goal and what it produces

The pinned Cycles source is the released Cycles v5.3.0 (`third_party/cycles-import.json`). On Apple Silicon this library
renders with Cycles' own Metal device, so the kernels of the pinned source are all there; what a consumer can actually use is
decided by the scene host (`native/host`), the C ABI and the managed API (`src/`). The goal:

> Everything the pinned source can do on its Metal device, inside the agreed target, is reachable through this library on
> an Apple Silicon Mac, and is shown by a render.

The target is unchanged: stock Cycles-with-Metal, minus OSL, OpenVDB and NanoVDB, OpenSubdiv, USD, Alembic and Hydra, and
minus path guiding. Embree and Open Image Denoise are in. The dlss denoiser is outside it. Nothing in this kit widens the
target.

The result defines the target of the WebGPU device on the other platforms: once it is known, by render, what Metal gives
for every feature and every refused case, parity has a definite meaning.

Deliverables, all on the branch:

1. The osx-arm64 build validated at the base commit (part A).
2. The Linux ARM64 CPU oracle of the base commit (part B).
3. The feature survey: one film or one verbatim error per row of section 6 (part C).
4. The host work of section 7 as far as it gets, with every new public signature marked provisional (part D).
5. One report (section 9).

## 2. Rules that bind every step

1. **Branch.** Create a branch (for example `macos-arm64-full-functionality`) from the commit of `main` that contains this
   document and record that commit's hash as the **base commit**. Commit and push only that branch. Never commit to, push
   to, merge into or rebase onto `main`. The branch is reviewed, merged and validated on the Linux x64 machine later; a
   merge there is a validation event, because the host is shared by every platform.
2. **The mandatory test rules** at the top of [TESTING-MACOS-ARM64.md](TESTING-MACOS-ARM64.md) apply: .NET SDK 11.0.100
   only (projects keep targeting `net10.0`), the WebGPU runtime only from its package at the pinned version, ISPC 1.30.0.
   Never `sudo`; install nothing the maintainer has not already installed; a missing tool is reported, not installed.
3. **The pinned source is never edited by hand.** Nothing under `third_party/cycles/` changes except through the importer's
   recorded transforms or the overlay (`native/tools/cycles_overlay.py`). A compile error in a pinned file is reported with
   the compiler's text; it is not "fixed" in place.
4. **Licence.** Nothing incompatible with Apache-2.0 enters the repository, code or data, including from memory. Blender
   outside Cycles is GPL: read only the pinned Cycles source in this repository, and copy nothing from Blender, its
   exporter or its add-ons. Something usable only with a rights holder's permission is unusable. No new NuGet package and
   no new third-party source. The illuminant numbers live in `kernel/tables_d65_codebrix.h` and are never retyped, restored
   from elsewhere or discussed in a comment; a file may name that header and its identifiers and nothing more.
5. **Never approximate, never relax.** A request the library cannot honour fails with an error that names the feature. No
   tolerance, comparator, scene or gate expectation is changed to make something pass. A gate follows the pinned kernel.
6. **What this kit does not touch:** the shared WGSL (`native/shaders`), the WebGPU device (`native/cycles`, `native/src`)
   beyond what section 7 names, gate and comparator sources, existing folders under `native/evidence/` (new folders only),
   the notices files, and every other platform's recipe. The WebGPU device is not ported here.
7. **A feature switched on is switched on for every platform.** The host is shared: when it starts accepting something, it
   accepts it on the CPU and Metal devices everywhere, and the WebGPU backend **keeps refusing it by name** until that
   device is ported. The capability report (`native/host/host_api.inc`), the render-time rejection list of
   `AGENT-README.txt` and `native/host/ABI.md` change in the same commit and carry the same words (a managed test compares
   the first two).
8. **Old evidence is void.** Every film, reference and hash recorded before the pin moved to v5.3.0 belongs to the previous
   revision. Never compare a v5.3.0 film with one of them as a verdict. No Linux x64 reference of v5.3.0 is committed at
   the base commit: every "compare with the Linux x64 reference" step is skipped and the report says so.
9. **Existing behaviour stays bit for bit.** The CPU-arm64 films of the whole committed corpus are rendered at the base
   commit before any host change (part A) and again at the end of part D. Every film that existed at the base commit must
   be bitwise identical in the two runs. A difference is a finding with the responsible change named, and the change is
   corrected or reverted on the branch.
10. **Repository documents** describe the product for "the maintainer": no personal names, no working notes.
    `AGENT-README.txt` stays consumer-only. Every new file starts with the copyright line and the SPDX identifier.
11. **Stop conditions.** Stop and write the report when: the base build cannot be made to pass without editing a pinned
    file; a licence or provenance doubt appears; a step would need a rule above to bend. Otherwise continue with the next
    row or the next part; one failed row never stops the survey.

## 3. Order of work

| Part | What | Depends on | Section |
| --- | --- | --- | --- |
| A | Build, host gate, oracle corpus on CPU-arm64 and Metal, managed tests, at the base commit | nothing | 4 |
| B | Linux ARM64 CPU oracle of the base commit, in the Debian arm64 container | nothing (may run beside A) | 5 |
| C | Feature survey on Metal | A | 6 |
| D | Host work: new attribute kinds, Gaussian splats, then the host-only gaps | A and C | 7 |
| E | Corpus again, bitwise check of rule 9, the report | D | 8, 9 |

Part A comes first without exception: this platform has never compiled the current pin, and a failure in new host work
cannot be told from a build problem until the base is known to be good.

## 4. Part A: the base

Follow [TESTING-MACOS-ARM64.md](TESTING-MACOS-ARM64.md), steps 1 to 8, with these differences for the current pin.

Before building: delete `native/work/macos/osx-arm64/host/` (build tree, overlay and copied source of the previous pin). The
dependency snapshots did not change with the pin (READ), so an existing dependency prefix stays valid and
`python3 tools/build_macos.py --jobs 8 --skip-dependencies` reuses it; without the flag everything is rebuilt.

Watch-points, in the order the build reaches them (all READ):

- `build_cycles_macos.py` adds an `arm_neon.h` include to a work copy of `src/util/half.h`; the anchor still matches once.
- New host C++ never compiled with Apple Clang here: `src/util/string.cpp` (`newlocale`, `uselocale`, `freelocale` with
  only `<locale.h>` included), `src/util/aligned_malloc.cpp` (`mmap`), the Gaussian-splat, quaternion and
  spherical-harmonics headers (`kernel/geom/gsplat*.h`, `util/math_float3x3.h`, `math_quaternion.h`,
  `math_spherical_harmonics.h`). `integrator/denoiser_dlss.cpp` compiles to an empty unit: no compile command may contain
  `WITH_DLSS`. `-DWITH_EMBREE` must be on the compile line of `src/bvh/embree.cpp`.
- **The overlay and the Metal kernel closure.** Exactly ONE overlay header is also a header of the Metal kernel closure:
  `util/types_gpu_compressed.h`. The recipe accepts it after proving it is the pinned text plus one recorded addition
  (`STOCK_EQUIVALENT` in `native/tools/build_cycles_macos.py`). Check that the build passes `stage_kernels`, that
  `native/work/macos/osx-arm64/host/kernel-sources.json` lists that file under `overlaidInClosure`, that `WITH_WEBGPU` is
  in no compile command (this profile uses Cycles' own half-precision integrator state), and that the staged kernel
  sources are the pinned files.
- **Dispersion is on.** `kernel/features.h` and `kernel/util/colorspace.h` are the pinned files, and `kernel/tables.h`
  includes `kernel/tables_d65_codebrix.h`. That header must be in the staged Metal kernel closure. A Metal compile error
  naming `cie_d65_spd`, `sample_wavelength` or `wavelength_to_rgb_d65` means the closure is incomplete: report it.
- Every Metal kernel recompiles once after the pin change; allow for it in the host gate's budget.
- `TESTING-MACOS-ARM64.md` step 5 (compare with the previous macOS evidence) has nothing valid to compare with: skip it.
- Step 7: the Metal reference films under `tests/CodeBrix.Graphics3D.Cycles.Tests/References/metal-osx-arm64` are void.
  Review the CPU-against-Metal comparison of the same scenes first, then record them again with the command of that step.
  Until then every Metal-authority test fails or compares against void films; that is expected, not a regression.
- Metal changes of the release a result may show: 18 integrator-state fields are stored in half precision (ray time and
  differential, pass weights, subsurface albedo and anisotropy), so Metal films sit a little further from the CPU films
  than before; CPU-against-Metal pairs are reported, never judged. With `CYCLES_METALRT=1`, point hits skip a second
  intersection: run one point-cloud scene that way.

Part A is done when: the build has 0 warnings in our own code; the macOS host gate passes; the whole committed corpus
(count it: `find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" | wc -l`, plus the two
bench documents) is rendered on CPU-arm64 and on Metal with every film repeat-identical or recorded; the managed build has
0 warnings and its tests pass with freshly recorded Metal references; the licence audit passes. The CPU-arm64 run is the
**base run** of rule 9. Staging the payload (step 9 of the testing document) is the maintainer's decision and is NOT done
by this kit.

## 5. Part B: the Linux ARM64 CPU oracle

Follow [TESTING-LINUX-ARM64-ORACLE.md](TESTING-LINUX-ARM64-ORACLE.md) at the base commit, including its section 10 (what
the first run at this pin has to show). It writes a new folder `native/evidence/linux-arm64-cpu-oracle-<date>/`, committed
on the branch.

## 6. Part C: the survey

For every row: author the smallest scene that asks for the feature (a `cbcOracleScene` document written by the family's
`author_*.py` script under `native/oracle/scenes/`, or a scratch document under `native/work/` when the host refuses it at
the base commit), render it on CPU-arm64 and on Metal, and record one of: both films and their comparison
(`native/oracle/compare.py`, reported, never judged); or the error text, verbatim, with where it comes from (host, Cycles,
the Metal compiler).

Where the host refuses a row by name, the refusal is lifted **only in a survey build**: a scratch copy of the change, kept
as a patch file in the survey's evidence folder, never committed as product code. A row whose Metal render is sound becomes
a candidate for part D or for the maintainer's ruling; the survey itself decides nothing.

Find every refusal the host and the devices can raise with:

```sh
git grep -h -o -E '"WebGPU [^"]{5,110} not implemented[^"]{0,60}"' -- native/cycles native/host | sort -u
git grep -n -E 'Unsupported feature|not supported together|requires tabulated|not implemented by the WebGPU|is not available|cannot be' -- native/cycles native/host
```

Rows whose text begins "WebGPU" are refusals of the WebGPU device and do not apply to this profile: each such feature must
simply RENDER on Metal, and the survey shows it.

| # | Feature or case | State at the base commit (READ) | What to establish |
| --- | --- | --- | --- |
| 1 | Dispersion (Principled Transmission Dispersion Scale, Abbe Number); scene `bsdf/principled-dispersion` | Renders on the CPU device; refused only on the WebGPU backend | It renders on Metal; CPU-against-Metal comparison |
| 2 | Boolean Math, Integer Math, Get Vector Component with LINKED inputs | Refused only by the WebGPU device | Each renders on Metal; cover every Integer Math operation at its edges (division and modulo by zero, power, GCD, LCM, overflow) |
| 3 | Glass BSDF with the multiscatter GGX distribution | No handler and no named refusal on the WebGPU device | What CPU and Metal give |
| 4 | Anisotropic transmission (Glass BSDF Anisotropy, Rotation, Tangent; Principled on the transmission lobe), caustics through such a caster included | Ported on the WebGPU device | It renders on Metal |
| 5 | Missing and Missing Alpha image inputs; Bump and the Tangent output on points; emissive curves and points with shadow linking | Ported on the WebGPU device | It renders on Metal |
| 6 | Curves and points with a Both displacement shader (native/DEVICE.md open item 87) | Refused; the pinned code reads an uninitialised value on the CPU | What Metal gives, twice, and whether the two Metal films agree with each other |
| 7 | Degenerate area lights (every sub-case the host refuses), zero-extent portals | Refused by name | CPU and Metal behaviour of each sub-case with the refusal lifted in a survey build |
| 8 | Shading bakes of zero-area texels; baking a subdivision surface | Refused by name | As row 7 |
| 9 | The denoiser upscale factor | Refused | Whether Open Image Denoise on this profile honours it; with and without a shadow catcher (the release forces it to 1 with one) |
| 10 | The Radial Tiling node with at most two sides and a roundness strictly between 0 and 1; Velvet subnormal paths | See `TESTING-MACOS-ARM64.md` section 9 | Repeat that section at the current pin |
| 11 | BVH time steps (`num_bvh_time_steps`) | Not exposed by the host | Whether the setting changes a Metal film with MetalRT and with BVH2 (`CYCLES_METALRT=0` and `=1`) in a survey build that sets it |
| 12 | Gaussian splats (a point cloud with `render_as` `gaussian_splats`) | Refused by the host on every backend | Left to part D; the survey records only that the refusal reads the same as on Linux |
| 13 | Quaternion and spherical-harmonics attributes | The host has no value kind for them | Left to part D |
| 14 | The dlss denoiser; a `denoiser_passes` bit outside the seven guides | Refused by the host | The refusals read the same as on Linux; nothing else |
| 15 | Tiled rendering, UDIM tiles, progressive results, the texture limit, the texture cache, BVH spatial split | Not exposed, or switched off in the host | Listed for part D item 3; the survey records which of them Cycles' Metal device supports at all, by reading `device/metal` and `scene/` |

## 7. Part D: host work

In this order. Each item is finished (built with 0 warnings, tested, documented, committed on the branch) before the next
one starts; an item that cannot be finished is left as a clean, reverted tree and a written account.

**Every new public signature is PROVISIONAL.** New C ABI exports, value kinds, enumeration members and managed API members
are the maintainer's to approve. Write each one in the report with its reason and the alternative considered, mark it
provisional in `native/host/ABI.md`, and prefer the smallest surface that follows the existing patterns of the ABI.

1. **Quaternion and spherical-harmonics attributes.** A quaternion value kind and the spherical-harmonics attribute in
   the C ABI, the generated model and the managed API, authored on ordinary points and meshes and read through an Attribute
   node. READ: the device-side names are `NODE_ATTR_QUATERNION` and `NODE_ATTR_SPHERICAL_HARMONICS_REST`, with the global
   arrays `attributes_quaternion` and `attributes_spherical_harmonics_rest`; the fetch paths are in `kernel/geom/attribute.h`
   and `kernel/svm/attribute.h`. The WebGPU backend refuses a scene that uses either, by name.
2. **Gaussian splats on the CPU and Metal devices.** The host accepts a point cloud with `render_as` `gaussian_splats` and
   supplies or requires the attributes a splat needs. READ: `create_missing_gsplat_attributes` has no caller in standalone
   Cycles, so the host must decide; read `scene/pointcloud.cpp` at the pin for which attributes are required, and decide
   from the pinned Cycles source alone (rule 4). The static transform is never applied to splat clouds. The default splat
   shader (`default_gsplat`) does not switch on attribute specialisation: the attribute feature is taken from referenced
   shaders only. The quaternion functions of `util/math_float3x3.h` carry a second notice
   (`third_party/licenses/CeresSolver-BSD-3-Clause.txt`); code adapted from them keeps it. The host refusal narrows to the
   WebGPU backend, in the three places of rule 7. New scenes go under `native/oracle/scenes/gsplat/`, written by a new
   family author script in the style of the existing ones: static, with motion, with and without spherical harmonics,
   beside meshes, volumes and transparent shadows. Each is rendered on CPU-arm64 twice (bitwise) and on Metal.
3. **The host-only gaps**, each only after items 1 and 2, each as a separate commit, and each only if Cycles' Metal device
   supports it (survey row 15): UDIM tiles; progressive results; tiled rendering; the texture limit and the texture cache;
   BVH spatial split; BVH time steps (only if survey row 11 shows an effect). For each: a short design in the report
   first, then the implementation, a host-gate case, a managed test and the documentation.
4. **Rows of the survey that render soundly on Metal but are refused by the host** (rows 6 to 9) are NOT lifted here.
   They wait for the maintainer's ruling; the survey films are the evidence for it.

Counts that fixtures assert and that these items move: the number of C ABI exports, the registry's node-type and socket
counts in the generated model, and the capability report's lists. Find every literal before changing one
(`git grep -n` for the current numbers in `native/host`, `native/tools` and `tests/`), and update host fixtures and managed
tests in the same commit. Gate scripts for platforms that cannot be run here are updated by reading and listed in the
report as unverified.

What cannot be done on this machine and is listed in the report instead: the Linux x64 build and its gates; the x64 CPU
films of any new scene (the x64 CPU kernel on the Linux x64 machine is the reference for every WGSL judgement); anything
that needs the WebGPU device.

## 8. Part E: closing checks

1. Clean build, 0 warnings in our own code; the macOS host gate; the managed build and tests.
2. The whole committed corpus again on CPU-arm64 and Metal. Rule 9: every CPU-arm64 film that existed in the base run is
   bitwise identical. New scenes are listed with their films.
3. `python3 tools/licensing/audit.py` passes. `git diff --check` against the base commit is clean.
4. `git diff --stat <base commit>` is quoted in the report; nothing appears under `third_party/cycles`, `native/shaders`,
   an existing `native/evidence` folder or another platform's recipe unless the report explains the line.

## 9. The report and what comes back

Everything comes back on the branch. The survey's films, patches and comparisons, and the report, go in ONE new folder
`native/evidence/macos-arm64-survey-<date>/` with a `README.md` and a `SHA256SUMS.txt`; the base and closing oracle runs
follow section 7 of `TESTING-MACOS-ARM64.md` (the oracle bundle). Absolute host paths are written as `<repo>` and `<home>`.

The report (`README.md` of that folder), facts first and inferences marked:

1. The base commit, the branch, the machine (model, macOS, Xcode, Metal), tool versions, wall times.
2. Part A: every step with its verdict; every compiler or gate message, verbatim; the scenes that failed or were refused.
3. Part B: the folder written, and the answers to section 10 of the ARM64 oracle document.
4. Part C: the survey table with one line per row: rendered on CPU, rendered on Metal, the comparison counts, or the error.
5. Part D: per item, what was done, the provisional signatures with their reasons, the tests added, what is unverified on
   the other platforms, and what was left.
6. Part E: the bitwise check of rule 9, with every film that moved.
7. Questions for the maintainer, in prose.
