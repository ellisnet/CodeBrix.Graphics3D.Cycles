# Native Cycles → WebGPU scene-to-image gate

**Passed on Linux x64 and Android ARM64/x64, including both API 33 devices.**
The target is now full Cycles parity; this experiment establishes a foundation,
not that final result. See [the parity ledger](../PARITY.md) for remaining work.

The native host builds an actual upstream `ccl::Scene` containing a floor and
three boxes (38 triangles), calls `Scene::device_update`, and exports Cycles'
prepared vertices, triangle indices, primitive table and static BVH2. The
native backend uploads that snapshot and runs embedded WGSL camera,
intersection and shading kernels. It returns a 256×192 linear RGBA image at
8 samples per pixel through a small C ABI. No CLR or Slang is involved.

![Native WebGPU rendering of the Cycles-prepared scene](evidence/scene-image/linux-x64/render.png)

This is currently a separate native bridge, not a registered `ccl::Device`
implementation used by Cycles' existing integrator. Scene preparation uses the
Cycles CPU device, without using it to render the GPU result. All vertices are
in world space; static-scene mode lets Cycles bake the sole mesh's transform.
The backend manually adapts BVH traversal and uses translated original Cycles
triangle math. Shading is an original, restricted Lambert/point-light model
with opaque shadows, flat normals and a constant background. The color table
is supplied by the fixture, not evaluated from Cycles shader bytecode.

The CPU oracle uses **original Cycles BVH traversal and triangle intersection**,
with the same prototype camera and lighting contract. This is not a comparison
against a full upstream Cycles path-traced image.

## Shipped WebGPU compatibility

All execution uses the unchanged native libraries and C headers from
`CodeBrix.WebGpu.Native.MitLicenseForever` **1.0.290.1433**. This is the package
ID produced by the `CodeBrix.WebGpu.Native` project in `~/GitHome/CodeBrix.WebGpu`.
An audit found all nine native RID binaries and five header/Rust interface files
identical between that repository, the NuGet archive and the extracted cache.
Only Linux x64, Android ARM64 and Android x64 have executed this prototype.

`tools/audit_webgpu.py` checks those identities before every build. The native
driver records the library actually loaded; Android deployments also hash that
file on-device. The backend checks CodeBrix ABI version 2, requires no optional
GPU features, submits WGSL through the existing API, and rejects software
adapters. It currently chooses Vulkan explicitly. No WebGPU source patch,
rebuild, native ray-query feature or SPIR-V passthrough is involved.

The local test directory stages one unchanged WebGPU library alongside the
module. A production Cycles NuGet must depend on the WebGPU package and ship
only the Cycles module and its own approved dependencies, respecting the
package's one-WebGPU-library-per-process and load-order rules. Existing-device
handoff and the managed package-loading path remain untested here.

## Validation and timing

All four devices pass the unchanged component tolerance
`abs(error) <= 5e-5 + 2e-4*abs(reference)` on all 196,608 RGBA components.
Maximum observed error is below `3e-7`; all outputs are finite and alpha is one.
Repeated renders are identical on each device. Invalid scene metadata, a BVH
cycle, invalid image dimensions and a nonfinite camera are rejected. Rejected
scene updates preserve the valid scene; valid replacement changes the image;
restoring a scene reproduces the original image after caller memory is cleared.

The saved JSON records initialization and three synchronous render durations.
These include allocations, command encoding, GPU execution and readback; they
are **not GPU timestamps**. Warm runs vary with system load and driver caches.
The scene is much too small to predict production throughput or claim a
speedup over optimized upstream Cycles. Shader-stack cost, complex materials,
large BVHs and scene memory are still performance gates.

The wider kernel research still has the documented Intel inverse-sine and two
grazing barycentric discrepancies. Passing this small image does not resolve
them; see [the feasibility report](../research/FEASIBILITY.md).

Android tests compile against NDK API 33 and run native executables through
`adb shell`. Android consumes the identical Linux-prepared scene snapshot;
compiling the entire Cycles host for Android and APK/app-sandbox execution are
separate, outstanding work.

## Reproduce on this machine

Prerequisites are the pinned clean Cycles checkout, the pinned native NuGet,
Clang 19/C++20, CMake, Python 3, and the Linux development libraries. The full
host additionally uses locally built OIIO 2.5.18.0 and OCIO 2.4.2. Source pins
and configuration are in `tools/build_host.py`; no system install is performed.

```sh
python3 research/run.py prepare
python3 prototype/tools/bootstrap_linux.py
# Requires the three pinned source checkouts described in tools/build_host.py.
python3 prototype/tools/build_host.py --dependencies
python3 research/run.py linux --prototype
python3 research/run.py android --prototype --serial 98271FFBA0048D
python3 research/run.py android --prototype --serial 192.168.86.225:5555
python3 research/run.py android --prototype --serial R52W207XQ1V
python3 prototype/tools/archive.py
```

Hardware runs require GPU/ADB access. Work output lives under ignored `work/`;
selected evidence, source hashes, dependency records and PNG previews live in
`evidence/scene-image/`. No production native package has been created.

## Experimental ABI and snapshot format

`native/cycles_webgpu.h` defines ABI 1: opaque context creation/destruction,
adapter identification, scene upload and synchronous rendering. Errors are
copied to caller-owned strings and exceptions do not cross C. Each context
serializes operations; callers must not destroy it during a call. GPU failures
poison a context; recreate it. Cancellation/recovery and arbitrary concurrent
shared-device use are not implemented. Images are limited to 512×512 and 1–64
samples; snapshots to 64 MiB.

The snapshot is a little-endian array of 16-byte rows:

| Table | Encoding |
| --- | --- |
| Row 0 | float32 triangle count, leaf base row, primitive-index base row, vertex base row |
| Internal nodes | Four rows per aligned Cycles BVH2 node, starting at row 1; child offsets are bit-preserved int32 values in header z/w |
| Leaves | Original Cycles int4 leaf records; negative child ID `-n-1` selects leaf n |
| Primitive indices | One row per primitive slot, x is a numeric float triangle ID |
| Vertices | Three float4 rows per triangle, expanded using Cycles' prepared indices; xyz plus zero padding |
| Albedos | One float4 row per triangle after all vertices, xyz in [0,1] |

Cycles' original root row 0 becomes row 1; all nonnegative child offsets are
incremented by one. The remaining node and leaf bytes are preserved. The native
validator rejects unsupported instances, bad offsets, cycles and excessive
stack depth before upload. This format is explicitly limited and must evolve
before full Cycles resources can be represented.

## Dependency and license boundary

The [root notices](../THIRD-PARTY-NOTICES.txt) now record a confirmed unresolved
issue: the pinned Cycles `third_party/atomic` headers carry GPL-2.0-or-later
notices and are included by the native host and CPU reference builds. Jeremy
is handling resolution; development continues at his request. The current
host binaries are not cleared for the intended Apache release.

Original contributions use Apache-2.0 and Jeremy's standard copyright. Cycles
derivation notices are in [THIRD-PARTY-NOTICES](../research/THIRD-PARTY-NOTICES.txt).
The separately supplied WebGPU package retains its own license and notices.

The host uses pinned OIIO/OCIO source builds and locally extracted Boost headers
and libraries. Optional FFmpeg, LibRaw and HEIF integrations are disabled. The
system TIFF library pulled in GPL-2+ JBIG; the unused TIFF plugin was therefore
disabled, and the final host dependency inspection confirms that TIFF/JBIG are
absent. The broad bootstrap prefix is a development environment, **not a
redistributable dependency bundle**. A full per-platform source/license and
runtime audit is still required before creating shipping native assets.
