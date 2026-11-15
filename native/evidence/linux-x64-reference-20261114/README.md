# Linux x64 reference on Cycles v5.3.0, first full pass (2026-11-14)

Copyright (c) 2026 Jeremy Ellis and contributors

The first full Linux x64 pass on Cycles v5.3.0 (`third_party/cycles-import.json`, revision
`b3f6262640f1a2f2d10b55b6ffccbf310ce0f981`): the whole oracle corpus on the CPU oracle and on the WebGPU device, every gate, the oracle
replays, the host gate and the managed tests, run from one queue on 2026-11-13 and 2026-11-14 with every job's fingerprint
recorded in the same run. The x64 CPU films here are the reference for this source: the films of
`native/evidence/linux-x64-reference-20261107/` and `-20261101/` are of the previous Cycles revision and differ legitimately.
Those folders stay as history. A later run writes a new evidence directory; never edit this one.

## Environment (`environment/`)

| Item | Value |
|---|---|
| Machine | Dell Precision 7770, 12th Gen Intel(R) Core(TM) i7-12850HX; GPU Intel Corporation Alder Lake-HX GT1 [UHD Graphics 770] (rev 0c), Mesa 25.0.7-2+deb13u1 (Vulkan); kernel 6.12.117+deb13-amd64 |
| Toolchain | the binaries were built in the manylinux container recorded in `cycles-build.json` and `toolchain.txt` (GCC 14.2.1, glibc 2.28 baseline), the WGSL overlay on this machine with Debian clang 19; this machine: LMDE 7 (gigi) (Debian 13.0), glibc 2.41-12+deb13u4, Mesa 25.0.7-2+deb13u1, .NET SDK 11.0.100 (projects on `net10.0`) (`toolchain.txt`) |
| Source | binaries built at commit `90f3bbdbc445806f2b0e5eb9b1b86d3cb1f6ed2b` (tree clean). HEAD moved to `828f6c2f9ee2c57dd379d472a0252edaf3caa87a` during the pass by a documents-only commit (`README-INDEX.txt`, `docs/KIT-MACOS-ARM64.md`, `docs/TESTING-LINUX-ARM64-ORACLE.md`); the queue's tree state excludes documents and did not change. Fix 001 was applied UNCOMMITTED during the pass: `git diff --binary` SHA-256 `1381e65fc781ae7907463bf3bfaaca7b814c683e62eb22705217d67d90e5eb18`, three paths (`native/DEVICE.md`, `native/cycles/pipelines.cpp`, `native/shaders/wavefront_shadow.wgsl`); the patch is `results/fix-001/changes.patch`. Queue tree state `3fb353bcc52d7151` before the fix, `86f8f0c1fe601395` after (`source.txt`) |
| WebGPU runtime | CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210, `runtimes/linux-x64/native/libcodebrix_wgpu.so` SHA-256 `5a61096ef092ae3fb6638189f1ea9fe7a99350f21376f5ea1ce9b22afce35b2b` (`identity.json`, `ldd.txt`) |
| Identity block | `identity.json` (after fix 001) and `identity-before-fix-001.json`: driver strings, OS build, arithmetic `x64-sse4.2`, host library, runtime and judge hashes; `host-code.json` and `host-code-before-fix-001.json`. The host library is `873a2f8b...` before the fix and `78d8042e...` after; the scene runner `1be28b22...` is the same |
| Binaries | `linux-bin-sha256.txt`: SHA-256 of every built gate, host and tool binary AFTER fix 001 (taken 2026-11-14). The binaries before the fix were overwritten by the rebuild and their hashes were NOT re-made; only the host library and runner hashes above are recorded for them |

## Which tree each job was judged on

The queue judged 618 jobs (610 scenes) on tree state `3fb353bcc52d7151` before fix 001 was applied. The fix changes one shader
entry's declared workgroup size in modules with a heterogeneous medium (`native/DEVICE.md` item 52). After it the fingerprints of
every job were collected again: 555 judged jobs kept their verdict and films because their fingerprint (modules, data and
commands) is identical before and after, and one measurement (the fingerprint collection) was not reopened; 55 scenes, the CPU
films and 6 gates (`attribute_gate`, `mnee_gate`, `nodes_gate`, `normal_gate`, `volume_boundary_gate`, `volume_gate`) were judged again on `86f8f0c1fe601395`, and every job
not yet reached was judged there for the first time. Final verdicts: 555 scenes stand from `3fb353bcc52d7151`, 171 scenes and all
53 gate and tool jobs from `86f8f0c1fe601395`. `results/verdicts.tsv` and `run.json` give the tree state, the time and the number of
judgments of every job; a carried scene keeps its earlier device films, a scene judged again has the later ones.

## Contents

| Path | What |
|---|---|
| `oracles/corpus/` | `cpu-x64-linux` and `wgsl-linux-intel` films of every committed oracle scene (724 scenes), the judge's comparison per scene (`comparisons/`, JSON and text), and `run.json` (per scene: verdict, practical and strict counts, largest difference and ratio, tree state, fingerprint, device and reference film hashes, whether the verdict was carried across fix 001). `bsdf/principled-dispersion` has NO `wgsl-linux-intel` film: the WebGPU device refuses it by name (`comparisons/bsdf__principled-dispersion.refusal.txt`) |
| `oracles/bench/` | the same for `bench_01` and `bench_01-hdri`; `oracles/bundle-sha256.txt` identifies the bench bundle (not committed; `native/oracle/scenes/make_bench_bundle.py` regenerates it) |
| `host-gate/` | the full Linux host gate including the GPU steps (`passed`; 60 exports, 786 independent reference films, 3,147 comparisons, 0 practical failures, 54 strict differences, maximum 1.53e-05), without its reload rounds (`omitted-reload-sha256.txt`) |
| `reference/` | the overlay hashes and the libm record (see "Re-made from the 20261101 set") |
| `results/verdicts.tsv` | every job of the pass in the order of its final judgment: verdict, wall seconds, practical and strict counts, tree state, number of judgments |
| `results/validation-record-linux-x64.json` | the validation record (fingerprint, identity and film hashes per job); `results/fix-001/validation-record-before.json` is the record as it stood when the fix was applied |
| `results/journal.jsonl`, `results/packets.json`, `results/packets/` | the queue journal, its packet events and the packets as written |
| `results/queue.json`, `results/expected-results.json` | the queue as run; the four expected results as data the queue compared the judge's output with (never a criterion) |
| `results/gates/` | per gate or tool job: its status or results file where it writes one, and its log |
| `results/gate-linux-halves.json` | the Linux halves of the six gate drivers that otherwise need an attached device |
| `results/cpu-films/` | the comparison of the two CPU renders, before and after fix 001; the CPU film hashes taken before the fix and their comparison with the films here |
| `results/fix-001/` | the fix as applied (patch, state, log) |
| `results/device52-first/`, `results/packet-001-films/` | films kept as records: the first renders moved aside by the rerun rule of `native/DEVICE.md` item 52, and the films of the one failed judgment |
| `results/reset-window-reruns/` | the jobs that ran while the GPU was reset (below) and their later renders |
| `results/size-envelope/` | the size guard and compile envelope measurement |
| `results/dotnet-*.status.json`, `results/oracle-replays-linux.status.json` | `dotnet build` and `test`, Debug and Release; the oracle-scene captures replayed on Linux |

## Results

- CPU reference: this build's own films, rendered twice; 726 of 726 scenes bitwise equal between the two renders, the wall-clock
  pass of `passes/data-passes` (pass-8-0.f32, pass-8-1.f32) excepted. The same holds for the renders before and after fix 001, and
  4,322 of 4,324 film files are bitwise the same before and after the fix; the 2 that differ are that wall-clock pass
  (`results/cpu-films/`). The fix did not move the oracle.
- Oracle corpus and bench, 726 scene jobs against those films: 722 perfect, 4 with their expected result, 0 failed.
  Strict differences in the gating film of each scene, as the journal records them: 31,065 (31,039 in the perfect scenes). Largest practical ratio of a perfect scene: 0.746 of the
  tolerance (`volumes/heterogeneous-in-heterogeneous-world`); largest absolute difference of a perfect scene 0.000103 (`combinations/cov-subsurface-skin-legacy-linking-catcher`).
- The four expected results (not perfect, each exactly as recorded beforehand, anything else in these scenes would have been a finding):
  `passes/cryptomatte` (`native/DEVICE.md` item 44: one pixel of the passes CryptoMaterial00 and CryptoMaterial01, the film itself has 0 practical failures);
  `hair/chiang-absorption-thick` (item 189: 1 value, 1.84 times the tolerance);
  `combinations/cov-camera-motion-heterogeneous-linking-catcher` (item 190: one pixel, 3 values, of the film and of the Shadow Catcher Matte pass);
  `bsdf/principled-dispersion`, refused by the WebGPU device with the exact text `WebGPU dispersion not implemented` (the runner's line: `codebrix_cycles_oracle: cbc_host_render returned 2: WebGPU dispersion not implemented`; dispersion renders on the CPU device; its CPU film is here).
- The one failure of the pass: `volumes/light-path-heterogeneous-equiangular`, judged `failed` at 2026-11-13T19:40:16 on `3fb353bcc52d7151` (packet `001-linux-t05`): the first render failed in repeat 1
  (repeat film-1.f32: 3 practical (worst ratio 2.26; compare.py gates film-0 only, every repeat must pass)) and was moved aside by the item 52 rule, the second render failed again in repeat 1 (practical failures: 0 in film-0.f32, 3 in film-1.f32;
  worst ratio 1.58). In the same batch the first render of `volumes/light-path-heterogeneous-point` failed (10 practical failures, ratio 6.96) and its second render was perfect.
  Cause: the `native/DEVICE.md` item 52 class (intermittent single wrong paths in a heterogeneous medium on this driver). Fix 001 followed; after it both scenes
  were judged again on `86f8f0c1fe601395` and pass every repeat (practical failures: `volumes/light-path-heterogeneous-equiangular` 0 in film-0.f32, 0 in film-1.f32; `volumes/light-path-heterogeneous-point` 0 in film-0.f32, 0 in film-1.f32).
- Gate and tool jobs: 52 of 53 perfect, 1 recorded and not judged (`dotnet_release`).
  Oracle replays: 21 of 21.
- Managed solution, Debug: 332 of 332 tests passed, 0 failed, 0 skipped, 0 warnings, the GPU classes included (SDK 11.0.100).
  Release: RECORDED, NOT JUDGED: 332 of 332 tests passed, 1 warning, which is the pack driver's "payload is STALE": the committed
  `native/binaries/linux-x64` payload was staged from the previous Cycles revision and can only be restaged once this folder exists, so Release is judged after the restage.
- Compile envelope and size guard (`results/size-envelope/`): job verdict `perfect` (tool `passed` True); built entries {"passed": true, "rows": 10126, "builtRows": 5503, "unbuiltRows": 4623, "builtOverLimit": 0, "unbuiltOverLimit": 12}; size guard 940,000 B: 37 rows at or above it, 7 at or above 1,110,000 B; densest entry at or above 100,000 B: {"densest": 14.483, "densestEntry": "intersect_subsurface", "densestModule": "q6/0214-surface.wgsl", "densestBytes": 830426, "densestInstructions": 57339, "low": 750000, "high": 900000}; the ratio rule holds: True. `unspecialized_compile_smoke`: `perfect`.

## The GPU resets of 2026-11-13

At 20:58:17, 20:58:46, 20:59:03 the kernel reset GPU contexts three times (`results/reset-window-reruns/kernel-log.txt`). The cause was a diagnostic of the fix investigation running in its own
processes beside the queue; the kernel reset those processes' contexts only, and the queue did not stop. The 12 queue jobs that
started between 20:55:55 and 21:00:06 were all judged perfect: `combinations/cov-world-volume-subsurface-caustics`, `combinations/enclosure-emissive-panel`, `combinations/enclosure-emissive-panel-mix`, `combinations/enclosure-emissive-panel-oren-nayar`, `combinations/enclosure-emissive-panel-translucent`, `combinations/glass-fast-gi-additive`, `combinations/glass-receiver-transparent-lighting-4`, `combinations/link-ao-node`, `combinations/link-fast-gi-additive`, `combinations/link-transparent-lighting-4`, `combinations/link-transparent-lighting-5`, `combinations/moving-transparent-fast-gi`. Of these,
3 were running at the instant of a reset (`combinations/enclosure-emissive-panel`, `combinations/glass-fast-gi-additive`, `combinations/glass-receiver-transparent-lighting-4`). `results/reset-window-reruns/jobs.json`
lists them with their times; none of them was judged again by the queue after fix 001 (all carried by fingerprint); they are rendered once more after the queue has ended and the results are recorded in that folder.

## Re-made from the 20261101 set

- Overlay hashes: RE-MADE, `reference/linux-overlay-hashes.json` (the overlay of the tree with fix 001, the one the binaries after the fix embed; `native/tools/build_cycles_macos_x64.py --write-overlay-hashes`).
- libm record: RE-MADE, `reference/libm-linux-x64-glibc.json` (`native/tools/libm_probe.py linux`, this machine's glibc; it does not depend on the Cycles revision).
- Managed reference films (`managed-linux-x64/` of the 20261101 set): NOT re-made. They are written only by a Release test run with `CODEBRIX_CYCLES_REFERENCE_EXPORT` set; this pass ran the managed tests without it, and Release is not judged until the payload is restaged. The 20261101 films are of the previous Cycles revision and are no reference for v5.3.0.
- Host-gate reload rounds: NOT included; `host-gate/omitted-reload-sha256.txt` identifies them.

## Not in this set

- `ao_render` specialization: not queued in this pass (the specialization check ran for `glass_render` and `principled_render`).
- The managed reference films (above); the hashes of the binaries as they were before fix 001.
- The per-job fingerprint collection files (the validation record carries every fingerprint); the second CPU render (bitwise equal to the first, `results/cpu-films/`); the module dumps the envelope reads.
- Other platforms: no device, Android, macOS, Windows or Linux ARM64 result is here, and none has been made on Cycles v5.3.0.

`SHA256SUMS.txt` lists every file (`sha256sum -c SHA256SUMS.txt`). Absolute host paths inside copied reports are written as
`<run>`, `<session>`, `<tools>`, `<bench-bundle>`, `<repin>`, `<repo>`, `<nuget>`, `<home>`; role labels of the working session inside
copied journal and packet texts are written as "operator", "a fixer" and "the queue tooling". Nothing else in a copied file was changed.
