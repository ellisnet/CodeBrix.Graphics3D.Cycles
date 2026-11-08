# Linux x64 reference, device checkpoint 1 baseline pass (2026-11-07)

Copyright (c) 2026 Jeremy Ellis and contributors

The Linux x64 half of device checkpoint 1: the first full baseline pass on the Linux x64 machine at commit
`c3783adfa91e0f8310f3cc766e0a55f492e21ee0` plus fix 001 (`native/shaders/wavefront_hair_huang.wgsl`, the Huang draw order per
oracle ISA; native/DEVICE.md open item 82), with every job's fingerprint recorded in the same run and the pipeline cache on
(native/DEVICE.md, "Device checkpoint 1"). The x64 CPU films here are the reference the HP's films of this commit are judged
against. This set **supersedes** `native/evidence/linux-x64-reference-20261101/` for this commit; that folder stays as history.
A later run writes a new evidence directory; never edit this one.

## Environment (`environment/`)

| Item | Value |
|---|---|
| Machine | Dell Precision 7770, Intel Core i7-12850HX; GPU Intel UHD Graphics (ADL-S GT1), Mesa 25.0.7-2+deb13u1 (Vulkan) |
| Toolchain | Debian 13, GCC 14.2.0-19, Debian clang 19.1.7 (3+b1), CMake 3.31.6, glibc 2.41 (2.41-12+deb13u4), Python 3.13.5, .NET SDK 10.0.403, kernel 6.12.115 (`toolchain.txt`) |
| Source | commit `c3783adfa` plus fix 001 (`source.txt`, with the SHA-256 of the fix's `git diff --binary`) |
| WebGPU runtime | CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210, `runtimes/linux-x64/native/libcodebrix_wgpu.so` SHA-256 `5a61096e...` (`identity.json`, `ldd.txt`) |
| Identity block | `identity.json`: driver strings, OS build, arithmetic `x64-sse4.2`, host library, runtime and judge hashes, the block every carried-forward verdict is tied to; `host-code.json` |
| Binaries | `linux-bin-sha256.txt` (SHA-256 of every built gate, host and tool binary the pass used) |

## Contents

| Path | What |
|---|---|
| `oracles/corpus/` | `cpu-x64-linux` and `wgsl-linux-intel` films of every committed oracle scene (502 scene documents), the judge's comparison per scene (`comparisons/`), and `run.json` (per scene: verdict, practical and strict counts, largest difference, fingerprint, device and reference film hashes, whether the verdict was carried across fix 001) |
| `oracles/bench/` | the same for `bench_01` and `bench_01-hdri`; `oracles/bundle-sha256.txt` identifies the bench bundle (not committed; `native/oracle/scenes/make_bench_bundle.py` regenerates it) |
| `host-gate/` | the full Linux host gate including the GPU steps (`passed`; 58 exports, 786 independent reference films, 3,146 comparisons, 0 practical failures, 150 strict differences, maximum 1.53e-05). The `reload/` and `managed-reload/` rounds are omitted, identified by `omitted-reload-sha256.txt` |
| `results/verdicts.tsv` | every job of the pass: scene and gate jobs, final verdict, wall seconds, practical and strict counts, the queue's tree state |
| `results/validation-record-linux-x64.json` | the validation record (fingerprint and identity per job). Its verdicts of `curve_mixed_gate` and `size_envelope` read `failed`: the queue rejudged both (a queue-tool misread and the run-to-run rule) in its journal, not in the record; `verdicts.tsv` and `journal.jsonl` carry the final verdicts |
| `results/journal.jsonl`, `results/packets.json` | the queue journal and its packets (three queue-tool faults, rejudged or requeued; no product change) |
| `results/gates/` | the status file of every gate job; `results/gate-linux-halves.json` the nine gate drivers' Linux halves |
| `results/size-envelope/` | the size guard and compile envelope measurement (`status.json`, `newly-warning.json`, `ratio-all.json`, the four corpus envelope shards, the three superset groups) |
| `results/dotnet-*.status.json`, `results/oracle-replays-linux.status.json` | `dotnet build`/`test` Release and Debug; the 20 oracle-scene captures replayed on Linux |

## Results

- Oracle corpus: 504 of 504 scene jobs perfect against the x64 CPU films of this commit (0 practical failures, 296,113 strict
  differences, largest 9.17e-05); 401 verdicts carried across fix 001 by an identical fingerprint, 103 rendered on the fixed tree.
- Gate jobs: 43 of 43 perfect; dotnet Release and Debug 274 of 274, 0 warnings; 20 of 20 oracle replays.
- Compile envelope: all 5,747 distinct corpus entries and the three superset groups within 60,000 Intel instructions (largest
  57,339, `intersect_subsurface`). Size guard 940,000 B: 57 rows at or above it, 2 at or above 1,110,000 B; densest entry at or
  above 100,000 B 14.48 instructions per 1,000 B, so the guard stands.

## Not in this set

- The overlay hashes, the libm record and the managed reference export of the 20261101 set were not re-made in this pass.
- Device results: `native/evidence/tablet-baseline-*/`, `native/evidence/hp-baseline-*/`.

`SHA256SUMS.txt` lists every file (`sha256sum -c SHA256SUMS.txt`). Absolute host paths inside copied reports are written as
`<run>`, `<repo>`, `<home>`.
