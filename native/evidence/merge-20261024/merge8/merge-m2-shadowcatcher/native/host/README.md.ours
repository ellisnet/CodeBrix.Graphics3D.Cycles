# Native scene host and thin .NET interop

Copyright (c) 2026 Jeremy Ellis and contributors

This maintained implementation exposes native Cycles scene authoring and Session
control through 48 C functions in [cycles_host.h](cycles_host.h); the consumer
reference (every export, struct, enum, error code, threading and ownership rule,
the additive-only rule and the determinism statement) is [ABI.md](ABI.md). The .NET 10
wrapper is in [src/CodeBrix.Graphics3D.Cycles](../../src/CodeBrix.Graphics3D.Cycles).
The ABI remains experimental. These sources are part of the full nine-RID effort;
they are not a released NuGet package or a claim of full GPU feature coverage.

Build on the current Linux x64 development host with CMake 3.24 or newer, the
reviewed dependency prefix built by `tools/dependencies/build.py`, and the pinned,
unchanged CodeBrix.WebGpu.Native package:

```sh
python3 native/tools/build_cycles.py --jobs 6
dotnet build src/CodeBrix.Graphics3D.Cycles/CodeBrix.Graphics3D.Cycles.csproj -c Release
```

The native host is `native/work/cycles-build/lib/libcodebrix_cycles_host.so`.
Repeated `--target NAME` options select specific CMake targets; the default builds
the host, its reference/test executables and the existing kernel/render gates.

Maintained Android cross-build recipes use NDK `29.0.14206865`, API 33 and
`c++_shared`, with 16-KiB ELF segment alignment:

```sh
python3 tools/build_android.py --rid android-arm64 --jobs 4
python3 tools/build_android.py --rid android-x64 --jobs 4
```

The default NDK location is `~/Android/Sdk/ndk/29.0.14206865`;
`CODEBRIX_ANDROID_NDK` may select another installation of that same reviewed
toolchain revision. Each RID has a separate `native/work/android/<rid>/` tree.
The dependency recipe builds verified local snapshots, records installed library
hashes, and confines installation to that RID's prefix. `--skip-dependencies`
reuses only a matching verified prefix. The Cycles copy applies the existing
cross-architecture detection, Android libc and ARM NEON include adaptations;
the pinned snapshot is unchanged. WGSL generation is local to each target, and
the maintained CMake injection selects that RID's unchanged shipped WebGPU
binary. These commands do not install an application or establish device support.

The osx-arm64 product profile uses Cycles' own Metal device instead of WebGPU,
selected with `CBC_HOST_METAL`, and contains no image file readers. Its recipe,
validation and decisions are in [MACOS_ARM64.md](MACOS_ARM64.md).

The .NET project contains only ownership and marshalling code and has no glTF
dependency. Its package build carries no native runtime assets yet and is not
published pending runtime packaging.
Scene compilation, BVH construction, image handling, color management, scheduling
and rendering remain native. The optional glTF/GLB client uses
CodeBrix.Graphics3D.Gltf2 and converts its data into this format-neutral API.

The C and .NET callers select the platform's GPU backend (`CBC_HOST_GPU`:
Cycles' Metal device on osx-arm64, the WGSL device on WebGPU on every other RID,
osx-x64 included), name WebGPU or Metal explicitly, or select the explicit CPU
reference backend. A requested GPU render never falls back to CPU shading. Native node/property
registry visibility does not mean a corresponding GPU kernel is implemented.
Current GPU feature boundaries remain documented in [DEVICE.md](../DEVICE.md).

Hosts own opaque integer handles. Input arrays and image pixels are copied;
rendering returns scene-linear RGBA with a lower-left origin. Failed or cancelled
renders preserve the caller's output. Destruction and managed `Dispose` cancel
active work and wait for cleanup. Color configuration is process-wide: select an
OCIO configuration before creating hosts or after all host lifetimes and in-flight
calls finish. The generated Cycles overlay invalidates derived color caches by
full configuration identity. The pinned third-party snapshot remains unchanged.

## Phase D ABI additions (maintainer notes)

The P14 gaps were closed additively in ABI 1 (`host_api.inc`, included at the
end of `cycles_host.cpp`). Where the native registry already covers a setting,
it stays on the generic socket layer; new exports exist only for lifetime,
buffers, callbacks and queries.

| P14 gap | Resolution |
| --- | --- |
| Version | `cbc_host_version` (`CBCVersionInfo`); `targets.cmake` stamps `CBC_HOST_LIBRARY_VERSION` (`1.0.<UTC yyyyMMdd>.<CODEBRIX_HOST_BUILD_NUMBER>`) and reads `CBC_HOST_CYCLES_REVISION` from `third_party/cycles-import.json`; the symbol-version marker and `cbc_host_abi_version` are unchanged |
| Backend-neutral selection | `CBC_HOST_GPU` (Metal in `WITH_METAL` builds, which are osx-arm64 only; else WebGPU), `cbc_host_resolve_backend`, `cbc_host_backend` |
| Capability queries | `cbc_host_capabilities_json` (binary and host level; embeds `device_report()`, the C3 tier report) |
| Lights | generic layer: the five light node types `cbc_host_create_node` already accepted, their sockets and an object holder; no new export |
| Cameras, integrator, film | generic layer on the singletons (projection types, panorama type, `motion`, `shuttertime`, exposure, filters, bounces) |
| Object/camera motion | generic `motion` transform-array sockets |
| Deformation motion | `cbc_host_motion_positions` (position attribute motion steps; Cycles regenerates motion normals); refuses meshes already baked by the static BVH (`transform_applied`) |
| Deletion | `cbc_host_delete_node` with a reference scan over every scene node's node sockets; shader deletion drops its graph handles (Cycles keeps the shader allocated, as `Scene::delete_node(Shader)` does) |
| Image updates | `cbc_host_image_update` swaps the `MemoryImageData` behind a handle and clears every bound slot's `ImageHandle`, so the next render re-adds the image |
| Callbacks | `cbc_host_set_event_callback`; `Progress::set_update_callback` drives status/progress events under a per-host event mutex; a thread-local flag makes `boundary()` refuse reentry; nonzero return sets the cancellation flag polled by the existing cancel callback |
| Passes/AOVs | `HostOutput` copies every named caller pass from the same complete tile as Combined; `cbc_host_pass_floats`, `cbc_host_render_passes_json` |

Known limits found while closing them (see ABI.md): the WebGPU device writes
only Combined, only supports the perspective camera and needs the integrator's
denoising, adaptive sampling, guiding and light tree switched off.

Phase E3 closed three of the original limits. Edits after a render apply as for
a scene built with them (`host_edits.inc`: the host keeps the authored
attributes of geometry the static BVH path may bake and restores them before a
geometry edit or a render whose bake would differ; baked volumes are refused with
`CBC_NOT_READY`). A render after a device error recovers an intact WebGPU device
(`recover_device` in `cycles_host.cpp`, `WebGPUDevice::recover`); a device that
cannot recover keeps the host faulted ("Host faulted"). The Intel Mesa 25.0.7
compiler crash while creating `shade_surface_light` when a scene adds lights in
one device lifetime is the known intermittent `brw_opt_combine_constants`
driver bug (an out-of-bounds read with a garbage register number), not a WGSL
error. Spot after point never recompiles (no specialization bit), and the GPU
ABI test now renders point, spot and sun lights. Adding an area light recompiles
`shade_surface_light` with `LIGHT_AREA`; running the area light's four arcsine
series as one loop (`wavefront_emitters.wgsl`) took that compile from 3 of 4
crashes to 0 of 7 in cold runs, but a later recompile in the same device
lifetime (object motion after an area light) still crashed 2 of 4 times, so the
GPU ABI test keeps the area light on the CPU and area lights stay covered by the
broad- and transparent-lighting gates (phaseE3 report).

`native/tests/host/scene_edit_consumer.c` (`codebrix_host_scene_edit_test`) and
`native/tools/scene_edit_gate.py` check the edits (13 scenarios, CPU bit for bit
against a fresh host, WebGPU with the practical criterion, each sequence replayed
in a new host) and the recovery after a rejected depth pass and orthographic
camera:

```sh
python3 native/tools/scene_edit_gate.py --output native/work/scene-edit-v1
```

`native/tests/host/abi_consumer.c` (`codebrix_host_abi_test`) exercises every
addition, malformed input, lifetime and struct-size rejection on the CPU
reference and through `CBC_HOST_GPU`; `native/tests/host/example.c`
(`codebrix_host_example_test`) is the ABI.md example. `targets.cmake` also builds
the oracle runner as `codebrix_cycles_oracle` for native (non-cross) builds.

Run the maintained Linux validation from the repository root:

```sh
python3 native/tools/host_gate.py --output native/work/host-validation-v1
```

Use a new output directory for each run. The command rebuilds the targets,
generates 786 native CPU reference films in fresh processes, checks exact exports
and retained registry metadata, exercises C and managed lifetimes, runs six color
configuration rounds through both APIs, stresses concurrent native lifetimes,
executes the additional C WebGPU lifecycle test, runs the ABI test on both
backends (comparing its GPU films with the CPU films) and runs the example. Reference executables compile
the original upstream colorspace implementation directly; link maps verify that
the corrected archive implementation is not used by those oracles. No fixture
assets or implementation sources are downloaded.

Practical comparisons use `5e-5 + 2e-4 * abs(reference)`; strict diagnostics use
`1e-7 + 1e-7 * abs(reference)`. This particular Linux host corpus requires both,
and CPU/API comparisons additionally require bit identity. GPU repeat differences
are reported separately. The original native Session bitwise acceptance failure
remains preserved in prior evidence; this additional lifecycle test does not
change that assertion or establish deterministic GPU accumulation.

The report records source, runtime, executable, library and reference hashes.
Fresh-checkout verification, Android device/application validation of these
maintained builds, the other RIDs, release payload review and intentional binary/symbol
retention remain required before shipping. Experimental outputs under `work/`
are ordinary build products, not the intentional RID deliverables described in
the repository's standing instructions.
