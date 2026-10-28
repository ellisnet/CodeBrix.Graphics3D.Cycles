# Windows ARM64 device checks (2026-10-27)

Copyright (c) 2026 Jeremy Ellis and contributors

Surface Pro X (Microsoft SQ1, Qualcomm Adreno 680, driver 27.20.2140.0),
Direct3D 12. The record and its conclusions are in
[docs/TESTING-WINDOWS-ARM64.md](../../../docs/TESTING-WINDOWS-ARM64.md),
"Status (2026-10-27)" and findings F1 and F5 to F10, C1. The generated WGSL dumps
(about 300 KB per module) are left out; regenerate them with
`CODEBRIX_CYCLES_WGSL_LOAD`.

| Path | What |
|---|---|
| `build-dependencies-status.json`, `build-host-status.json` | The win-arm64 dependency prefix and host built from HEAD on the device (`native/work/windows/win-arm64`), with the F1 fix in `wavefront_shader_data.wgsl`, linked against the shipped `CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.300.210 `codebrix_wgpu.dll` (SHA-256 `40eb07e0...`) |
| `w1-check-wgpu300/` | `render_oracles.py` run of five scenes (glass-render, deforming-motion-1, glass-and-area-light, mnee/glass-sphere-caustic, light-tree/lamps-and-mesh-lights) on that build: `cpu-arm64-windows` and `wgsl-windows-arm64-d3d12` films, `run.json`, comparisons |
| `control-w1-reverted-wgpu300/` | `linux-gates/glass-render` on the same build with the fix reverted through `CODEBRIX_CYCLES_WGSL_LOAD` (diagnostic): dump, override and CPU passes, `compare.json` (2,631 practical failures) |
| `gltf-client-wgpu300/` | The glTF example client (`--write-sample --sample-sun`, `--compare`) on the same build: 0 practical failures, max 4.8e-7 |
| `f1-narrowing-wgpu290/` | The F1 narrowing on the committed payload's host (runtime 1.0.290.1433), with `CODEBRIX_CYCLES_DEBUG_OUTPUT` and edited WGSL (diagnostic): one directory per captured value (A: `N` before the back-face flip; B1/B2: decoded vertex normal and its packed word; C-*: `cross_normal`, built-in `cross`, `dPdu`, `dPdv`, `wi`; E*: `rounded_cross` parts; F1/F2: without the negative-scale branch, object flags; G: the fix) |
| `tools/` | The diagnostic scripts: `try_workaround.py` (dump, edit, override render, CPU render, compare per scene), `linux_compare.py` (films against the Linux x64 glibc films), `debugfilm.py` (summary of a debug-output film), and the edit sets (`edits-w1.json` the fix, `edits-w2.json` the fix plus the five proposed extra sites, `edits-revert-w1.json` the control). They contain this device's absolute paths. |
