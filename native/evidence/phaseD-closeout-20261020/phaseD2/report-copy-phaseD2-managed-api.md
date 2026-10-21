# Phase D2 - managed API (typed facade, generated model, public interop tier)

Agent: Phase D2 (Opus 5.5), worktree `/home/jeremy/ClaudeHome/cycles-worktrees/d2-managed-api`
(detached at 00a8f4aa, seeded from main incl. uncommitted D1; seed manifest
`/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/baseline-phaseD2.json`). Linux x64 only, CPU
reference backend for tests. No git operations, no main/D3 changes, no native/WGSL/CMake/gate/
evidence/third_party edits, no new NuGet references, no sub-agents.

Sealed handoff: `native/work/d2/handoff/` (changes.patch, changes.json, source/, baseline/,
validation/, handoff.json).

## Timeline

| Time | Event |
|---|---|
| [06:00:35] | Start; read AGENT/MAINTAINER/README, D1 report, managed sources, C header, host .inc files, fixtures, tests, plan 4618-4731, family rules. |
| [06:03:31] | Found the native host NOT built in this worktree (`native/work/` held only the dependency prefix). Started `python3 native/tools/build_cycles.py --jobs 6` (30 min budget). |
| [06:05:35] | Host library present; captured the registry through the existing `CyclesHost.RegistryJson()` wrapper into `native/work/d2/registry.json` (184 types, 1,277 sockets, 816 enum entries, ABI 1). |
| [06:09:03] | Generator `tools/managed/generate_model.py` written; native build finished (exit 0). |
| [06:11:27] | `Interop.Native` made public with XML docs (signatures unchanged; 3 exports added: abi_version, set_strings, node_type). |
| [06:16:52] | Facade, node bases, rendering/diagnostics types, `CyclesAbi` written; model generated (185 files). |
| [06:17:18] | Release build 0 warnings / 0 errors; host fixtures (HostLifecycle, ImageModes, ColorReload) compile unchanged. |
| [06:20:37] | First test run 117/119; fixed the test's PascalCase rule and made cached singletons honour disposal. |
| [06:20:50] | 119/119 pass. |
| [06:21] | Host gate started (`native/work/d2/host-gate`). |
| [06:23:52] | Host gate PASS (38 exports, 786 independent reference films, 3,139 comparisons; managed lifecycle, image modes, 6 colour reload rounds / 1,560 films). |
| [06:24:19] | Every AGENT-README/README example compiled and run against the built host (`native/work/d2/doc-examples`). |
| [06:25:39] | Validation: non-incremental Release + Debug builds 0 warnings; tests 119/119 in Release twice and Debug once; 75 pass / 44 skip / 0 fail with the native host hidden. |
| [06:26:17] | pack OK, audit.py passed (0 errors), notices --check OK, compatibility + licensing guard unittests OK, generator --check up to date, git diff --check clean. |
| [06:27-06:30] | Report and sealed handoff. |

## Public API surface

Namespaces (sub-folders = sub-namespaces):

- `CodeBrix.Graphics3D.Cycles` (root)
  - `CyclesScene` - typed facade entry point; owns a `CyclesHost`; `IDisposable` + `IAsyncDisposable`.
  - `CyclesBackend` - `Gpu` (Metal in osx-arm64 processes, WebGPU elsewhere) / `CpuReference`.
  - `CyclesImage` - caller pixels uploaded to a scene; `Dispose` releases the caller reference.
  - `CyclesVersion` - static: `Abi` (from `cbc_host_abi_version`), `ExpectedAbi`, `IsAbiCompatible`, `NativeLibraryVersion`/`CyclesRevision` (null until D3's version export), `ManagedVersion`.
  - `CyclesException` (`NativeStatus NativeStatus`) and `NativeFailure : CyclesException` (kept, `int Status`).
  - `NativeStatus`, `ValueKind`, `PropertyFlags` - enums equal to `CBC_*` constants (added to the constant-matching test).
  - `Transform3x4` - Cycles 3x4 row-major transform with `Matrix4x4` conversion.
  - Unchanged and kept in place for the fixtures: `CyclesHost` (gains `SetStrings`, `NodeTypeName`), `Node`, `Image`, `PropertyInfo` (gains `Kind`, `PropertyFlags`), `Backend`, `AttributeValue/Domain/Flags`, `ImageFormat`, `PipelineTier(Flags)`.
- `.Rendering` - `RenderSettings`, `FilmBuffer` (raw scene-linear float32 RGBA, bottom row first, `Width/Height/Stride`, `Pixels`/`Span`/`GetRow`), `RenderProgress`, `RenderStage`, `RenderPass` (`Combined`).
- `.Diagnostics` - `ICapabilityReport`, `CapabilityReport` (parsed from `cbc_host_device_report_json`).
- `.Nodes` - `CyclesNode` (base: `Scene`, `Handle`, `TypeInfo`, `Describe`, protected typed accessors), `ShaderGraphNode` (`Graph`), `ShaderInput`, `ShaderOutput`, `NodeTypeInfo`, `SocketInfo`, `EnumEntry`, `NodeCategory`, `NodeCreation`.
- `.Generated` - `NodeType` catalogue (`All`, `Find`, `Of`, `RegistryAbiVersion`).
- `.Generated.Scene` (12), `.Generated.Geometry` (5), `.Generated.Lights` (6), `.Generated.Shaders` (161) - one class per node type, 146 enums, 896 typed properties, 633 shader socket accessors.
- `.Interop` - `Native` (public raw P/Invokes + ABI structs, exact signatures) and `CyclesAbi` (one method per C function: raw `ulong` handles, native names, `NativeStatus` results, spans, `LastError()`).

Facade members: `Camera/Film/Integrator/Background`, `Create<T>()`, `CreateShader()`, `CreateLight<T>()`, `CreateMesh(xyz, indices, shader)`, `CreateObject(geometry, transform)`, `CreateImage<T>(w, h, format, ReadOnlySpan<T>)` (T = component type or byte, size-checked), `Wrap(Node)`, `GetCapabilities()`, `Render(settings, Span<float>)`, `Render(settings)`, `RenderAsync(settings, progress, ct)`, `RenderAsync(settings, Memory<float>, progress, ct)`, `ProgressInterval`, `Cancel`, `IsRendering`, `Dispose`, `DisposeAsync`. Generated extras: `ShaderNode.Output/Add<T>/Connect`, `GeometryBaseNode` attribute methods, `MeshNode.SetTriangles`, `ImageTextureNode`/`EnvironmentTextureNode.Bind/Unbind`.

Every behaviour maps onto an existing export: RenderAsync = `cbc_host_render` on a thread-pool thread + `cbc_host_cancel` (on token fire and on each poll until the native render stops, so a token fired before the native render starts still cancels) + `cbc_host_is_rendering`; progress is polled (documented; `Fraction`/`CompletedSamples` null while running).

## Generator design

- `tools/managed/generate_model.py` (Python 3, stdlib only, deterministic, offline). Input `src/CodeBrix.Graphics3D.Cycles/Generated/registry.json` (formatted snapshot of the live registry; live capture kept at `native/work/d2/registry.json`). Modes: default (write), `--check` (exit 1 if stale/unexpected files), `--registry X --update-registry`.
- Class per type: `<PascalCase>Node`, rename table for lights/pointcloud/`geometry_base` -> `GeometryBaseNode` (the shader node `geometry` takes `GeometryNode`). Inheritance follows registry `base` (lights derive from `GeometryBaseNode`, `volume` from `MeshNode`); types without a factory are abstract; leaves sealed; inherited sockets are not redeclared; `new const NativeTypeName` on derived types.
- Properties: readable+writable, non-internal inputs; socket type -> float/int/bool/uint/ulong/string/Vector3/Vector2/Transform3x4/enum/typed node/arrays. Member-name collisions are resolved with `Value`/`Socket` suffixes against a reserved set and ancestors.
- Shader nodes: `<UiName>Input` per linkable input and `<UiName>Output` per output (UI names are what `cbc_host_connect` matches).
- Categories: Scene, Camera, Film, Integrator, Background, Geometry, Light, Shader, Output; creation: Singleton, Scene (the `cbc_host_create_node` list), ShaderGraph, GraphOutput, Unavailable.
- `NodeType.cs` embeds every type and socket (name, UI name, type, value kind, components, flags, node type, enum entries) with a factory.
- Output: copyright/SPDX header + `<auto-generated>` banner, XML docs from registry name/UI name/type on every public member. Verified CS1591-clean with the banner removed as well.

## Tests (119 total, all pass; 44 skip with a reason when the native host is absent)

New: `CyclesSceneTests` (26 cases: backend resolution x2; typed render finite + bit-identical across two renders and two scenes; typed == by-name bitwise; RenderAsync == sync + progress stages; prompt cancellation (<10 s, destination untouched, scene reusable); pre-cancelled token; all 8 ImageFormats upload+bind+render (theory); wrong component type; wrong byte count -> `CyclesException(Invalid)`; Wrap identity; typed node references; Create rejects non-scene types; cross-graph Connect rejected; CPU capability report; CyclesVersion; dispose semantics; DisposeAsync cancels an active render; image dispose keeps bound pixels), `CyclesAbiTests` (interop tier == facade bitwise; status codes + LastError; size-query protocol + node_type; ABI), `NodeTypeTests` (catalogue == snapshot counts/names; catalogue == live registry; one class per type; generator `--check`; every generated property read -> write-back -> read through the native host on every creatable node, with coverage of every registry property type), `Transform3x4Tests` (4), `CyclesExceptionTests` (2). `NativeHeaderConstantMatching` extended with 31 rows (NativeStatus, ValueKind, PropertyFlags, CBC_HOST_ABI). Helper: `TestScenes.cs` builds one scene through all three tiers.

## Validation

| Check | Result | Output |
|---|---|---|
| `dotnet build --no-incremental -c Release CodeBrix.Graphics3D.Cycles.slnx` | 0 warnings, 0 errors | validation/build-release.txt |
| same, Debug | 0 warnings, 0 errors | validation/build-debug.txt |
| `dotnet test --solution ... -c Release` (x2) | 119/119 pass | validation/test-release-1.txt, -2.txt |
| `dotnet test --solution ... -c Debug` | 119/119 pass | validation/test-debug.txt |
| tests with native host hidden | 75 pass, 44 skipped, 0 failed | validation/test-without-native.txt |
| `dotnet pack -c Release` (not published) | OK (dll, xml, README, AGENT-README, notices, icon) | validation/pack.txt, pack-contents.txt |
| `python3 native/tools/host_gate.py --output native/work/d2/host-gate` | PASS, 38 exports, 786 films, 3,139 comparisons | validation/host-gate-status.json |
| `python3 tools/licensing/audit.py` | passed, 0 errors | validation/audit.json |
| `python3 tools/licensing/notices.py --check` | OK (34 components) | validation/notices-check.txt |
| compatibility guard (`test_inventory`) | OK | validation/compat-guard.txt |
| licensing guards (`test_guards`) | OK | validation/licensing-guards.txt |
| `generate_model.py --check` | up to date (185 files) | validation/generator-check.json |
| `git diff --check` | clean | validation/git-diff-check.txt |
| fixtures HostLifecycle/ImageModes/ColorReload | compile unchanged; host gate runs them | host gate |
| doc examples (AGENT-README + README) | compiled and ran on CPU | native/work/d2/doc-examples |

## Deviations and notes

- The native host was not built in the worktree despite the brief; I built it with `build_cycles.py --jobs 6` (exit 0, about 6 min). The host gate then rebuilt it again.
- `CyclesHost` kept in the root namespace (fixtures and the Android app compile against it); it is documented as part of the interop tier together with `Interop.CyclesAbi` and `Interop.Native`. The status-code layer is a static class with raw handles (exact C mirror) rather than a handle-owning object.
- `NativeFailure` kept (sealed, `int Status`) and now derives from the new `CyclesException`, so existing catch sites still work.
- Internal sockets (`surface_mix_weight`, `volume_mix_weight`, a few `normal` inputs) and closure inputs get no typed property (closures get `ShaderInput` accessors); they stay in the catalogue and on the by-name tier.
- Types the host cannot create (`buffer_params`, `buffer_pass`, `denoise_params`, `scene_attributes`, base types) are generated and catalogued with `NodeCreation.Unavailable`.
- `RenderPass` has only `Combined`; `CyclesVersion.NativeLibraryVersion/CyclesRevision` return null; progress is polled. These are the documented extension points, not throwing stubs.
- No WebGPU test was added (Linux CPU-only scope); a WebGPU scene was created successfully once in the doc-example run.
- native/host/README.md still says "36 C functions" (it is 38 exports); not edited because native/ is off limits for D2.
- The MAINTAINER-README registry-capture step describes a small resolver program rather than a checked-in tool (no extra project added to the repo).

## What D4 must wire when D3's exports land

Names from the D3 worktree header (read-only snapshot, 05:49); confirm them at merge.

| D3 export | Managed wiring |
|---|---|
| `cbc_host_version` (`CBCVersionInfo`: library_version, cycles_revision) | Add struct + P/Invoke to `Interop.Native`, `CyclesAbi.Version(...)`; fill `CyclesVersion.NativeLibraryVersion` and `CyclesVersion.CyclesRevision` (currently null). |
| `cbc_host_resolve_backend`, `cbc_host_backend`, `CBC_HOST_GPU` | Add `Backend.Gpu` value (and mapping row in NativeHeaderConstantMatching); make `CyclesScene.ResolveBackend(CyclesBackend.Gpu)` ask the native host instead of the managed osx-arm64 rule; `CyclesScene.Backend` from `cbc_host_backend`. |
| `cbc_host_capabilities_json` | Parse into `CapabilityReport` (new members: compiled backends, image formats, pass types, limits, determinism reference); keep the device-report fields; point the AGENT-README determinism paragraph at the native statement. |
| `cbc_host_delete_node` | `CyclesScene.Delete(CyclesNode)` / `CyclesNode.Delete()`; remove the wrapper from the scene's node map; `CyclesHost.DeleteNode`, `CyclesAbi.DeleteNode`. |
| `cbc_host_image_update` | `CyclesImage.Update<T>(w, h, format, ReadOnlySpan<T>)` (update `Width/Height/Format`); host/ABI wrappers. |
| `cbc_host_motion_positions` | `MeshNode.SetMotionPositions(uint steps, ReadOnlySpan<float>/ReadOnlySpan<Vector3>)` via the generator EXTRAS table; object/camera motion already work through the generated `Motion` transform-array properties. |
| `cbc_host_set_event_callback` (`CBCEvent`: STATUS/PROGRESS/ERROR, samples_done/total, elapsed, message) | Replace polling in `RenderAsync` with a pinned `UnmanagedCallersOnly` callback: fill `RenderProgress.CompletedSamples`/`Fraction`, return nonzero on token cancellation, surface status/error messages (e.g. an event/log `IProgress` or event). Keep `ProgressInterval` polling as the fallback. |
| `cbc_host_pass_floats`, `cbc_host_render_passes_json` | Add `RenderPass` values / a pass-name record, a `RenderSettings` passes member, and `FilmBuffer` per pass with `Components` (1/3/4) and `Stride = Width * Components`; `CyclesScene.ReadPass(name)` returning copied floats. |
| Registry growth (lights/cameras/integrator/film settings) | Recapture the registry, `generate_model.py --registry ... --update-registry`, rebuild; extend the generator's `SCENE_CREATABLE`/`SINGLETONS` lists if creation rules change. |
