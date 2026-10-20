# Native scene host and thin .NET interop

Copyright (c) 2026 Jeremy Ellis and contributors

This maintained implementation exposes native Cycles scene authoring and Session
control through 36 C functions in [cycles_host.h](cycles_host.h). The .NET 10
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

The .NET project contains only ownership and marshalling code, has no glTF
dependency, and currently disables package creation pending runtime packaging.
Scene compilation, BVH construction, image handling, color management, scheduling
and rendering remain native. The optional glTF/GLB client uses
CodeBrix.Graphics3D.Gltf2 and converts its data into this format-neutral API.

The C and .NET callers select WebGPU or the explicit CPU reference backend.
A requested WebGPU render never falls back to CPU shading. Native node/property
registry visibility does not mean a corresponding GPU kernel is implemented.
Current GPU feature boundaries remain documented in [DEVICE.md](../DEVICE.md).

Hosts own opaque integer handles. Input arrays and image pixels are copied;
rendering returns scene-linear RGBA with a lower-left origin. Failed or cancelled
renders preserve the caller's output. Destruction and managed `Dispose` cancel
active work and wait for cleanup. Color configuration is process-wide: select an
OCIO configuration before creating hosts or after all host lifetimes and in-flight
calls finish. The generated Cycles overlay invalidates derived color caches by
full configuration identity. The pinned third-party snapshot remains unchanged.

Run the maintained Linux validation from the repository root:

```sh
python3 native/tools/host_gate.py --output native/work/host-validation-v1
```

Use a new output directory for each run. The command rebuilds the targets,
generates 786 native CPU reference films in fresh processes, checks exact exports
and retained registry metadata, exercises C and managed lifetimes, runs six color
configuration rounds through both APIs, stresses concurrent native lifetimes,
and executes the additional C WebGPU lifecycle test. Reference executables compile
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
