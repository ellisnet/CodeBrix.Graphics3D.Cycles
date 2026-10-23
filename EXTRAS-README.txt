================================================================================
EXTRAS-README: CodeBrix.Graphics3D.Cycles
Samples, tools and other content in this repository that is not part of a NuGet
package
================================================================================

This repository ships no sample applications. It has one example client (the
glTF render tool below). Exactly one .NET project is packable - src/CodeBrix.Graphics3D.Cycles - and it is a thin wrapper over a
native renderer whose source, build tooling, validation gates and evidence also
live here. None of the content listed below is included in the
CodeBrix.Graphics3D.Cycles.ApacheLicenseForever package.

For runnable, compilable usage of the library, read the test project: the
"WORKING EXAMPLES ON GITHUB" section of AGENT-README.txt maps each feature area
to the test file that exercises it.


TEST PROJECT
============
    tests/CodeBrix.Graphics3D.Cycles.Tests/

xUnit v3; run it as described in MAINTAINER-README.txt (TESTING). Its
native-host tests skip with a reason when the native host has not been built in
the working tree, and its WebGPU tests (WebGpuRenderTests.cs) also skip with a
reason when no WebGPU adapter is available.


GLTF RENDER TOOL (EXAMPLE CLIENT)
=================================
    tools/gltf/CycleGltfRender/      project CodeBrix.Graphics3D.Cycles.GltfRender

A console tool in the solution's Tools folder, never packed. It loads a .gltf or
.glb file with the CodeBrix.Graphics3D.Gltf2.MitLicenseForever package (a
dependency of this tool only, never of the library), converts it into a Cycles
scene through the typed API and renders it on the GPU (WebGPU) or the explicit
CPU reference backend:

    dotnet run --project tools/gltf/CycleGltfRender -c Release -- \
        MODEL.glb --output film.bin [--backend cpu|gpu] [--compare] \
        [--width 64 --height 48 --samples 16] [--camera N | --default-camera] \
        [--native-library PATH]
    ... --write-sample sample.glb [--sample-sun]   (a procedural test model)

It writes the raw film (film.bin: little-endian float32 RGBA, scene-linear,
row 0 at the bottom) and a small header (film.bin.json: size, samples, backend,
sha256); it encodes no images. --compare renders on both backends, writes
film.cpu.bin and film.gpu.bin and prints the maximum error, practical failures
and strict differences with the repository criteria (exit code 3 on a practical
failure). --help prints the mapping below. The native host is found through
--native-library, then CODEBRIX_CYCLES_NATIVE, then the in-tree Linux build.

What it maps (the closest closures the WebGPU backend has today; the Principled
BSDF is not available on that backend yet, so glTF materials are approximated):

    glTF                         Cycles
    baseColorFactor (rgb)        Diffuse BSDF (Lambert) colour
    metallicFactor > 0           Mix Closure (fac = metallic) of Diffuse and a
                                 GGX Glossy BSDF coloured by the base colour
    roughnessFactor              Glossy roughness (both perceptual)
    emissiveFactor (+ strength)  Emission, added with Add Closure; emission
                                 sampling off (forward hits only)
    KHR_materials_unlit          Emission (base colour, strength 1)
    alphaMode BLEND / MASK       Mix Closure with a Transparent BSDF (fac =
                                 alpha) / all-or-nothing at alphaCutoff
    meshes, nodes                one Cycles mesh per glTF mesh (primitives as
                                 per-face shaders), one object per node
                                 instance; shared meshes and
                                 EXT_mesh_gpu_instancing become instances
    NORMAL, TEXCOORD_0           smooth shading with the vertex normals; "uv"
                                 corner attribute with v flipped
    KHR_lights_punctual          directional -> sun (strength = intensity),
                                 point -> point, spot -> spot (strength =
                                 intensity x 4 pi); a default sun when none
    camera                       the first perspective camera (yfov); otherwise
                                 a default camera framing the scene bounds

Limits, stated plainly: textures are NOT used. Gltf2 carries image bytes but
does not decode them and the library takes raw pixels only (it never decodes
image files), so the tool uses base-colour, metallic, roughness and emissive
factors and warns once when a material has textures. Normal and occlusion maps,
dielectric specular, clearcoat, transmission, sheen, volume, KHR_texture_transform,
skins, morph targets, animations, orthographic cameras and light ranges are
ignored with a warning. Objects keep the default shadow-terminator offsets,
which both backends implement. The tests in
tests/CodeBrix.Graphics3D.Cycles.Tests/GltfSceneConverterTests.cs drive the
conversion on the procedural sample model.


NATIVE RENDERER SOURCE
======================
    native/

The native Cycles scene host (native/host, whose cycles_host.h is the ABI the
.NET wrapper binds to), the WebGPU device and WGSL kernels, the oracle runner,
the native test drivers, every build and validation gate script
(native/tools), the reviewed RID binaries (native/binaries/<rid>/) and the
immutable gate evidence (native/evidence). native/work/ is ignored build output.


VENDORED THIRD-PARTY SOURCE
===========================
    third_party/

Pinned, license-reviewed source snapshots: the filtered Cycles snapshot and
every native dependency, with their import manifests, per-file hashes and
license texts. This is vendored upstream content, not project source: never
reformat or edit it by hand. THIRD-PARTY-NOTICES.txt and NOTICE are generated
from these manifests.


BUILD AND LICENSING TOOLS
=========================
    tools/

    build.py, build_android.py,         native build entry points for Linux x64,
    build_linux_arm64.py, build_macos.py Android, Linux ARM64 and osx-arm64
    cycles_source.py, arm_math_source.py, native_dependencies.py,
    android_toolchain.py                source and toolchain verifiers
    dependencies/                       dependency build recipes
    licensing/                          the source audit (audit.py), the
                                        notices generator (notices.py), the
                                        import, offline-build, direct-build and
                                        binary audits, and their guard tests
    compatibility/                      the compatibility inventory generator
                                        and its guard tests

HOST-GATE FIXTURES
------------------
    tools/hosttests/HostLifecycle/   tools/hosttests/ImageModes/
    tools/hosttests/ColorReload/     tools/hosttests/AndroidHost/
    tools/hosttests/WebGpuScenes/    tools/hosttests/NuGet.Config

Console fixtures and an Android app template that consume the .NET wrapper and
the native host. WebGpuScenes/WebGpuValidationScenes.cs holds the small WebGPU
validation scenes (glass, diffuse with a sun, textured plane) compiled into
both the test project and the Android app. They are driven by native/tools/host_gate.py,
macos_host_gate.py and build_android_app.py, are not in the solution, and are
not family tests. MAINTAINER-README.txt (HOST-GATE FIXTURES) describes them.


COMPATIBILITY, PARITY AND RESEARCH
==================================
    compatibility/      the machine-readable Cycles compatibility inventory
    parity/             the material, real-model and integrator parity harness
                        and its evidence
    prototype/          the original native scene-to-image prototype and its
                        evidence
    research/           kernel feasibility research and its evidence
    docs/               PARITY.md (the parity ledger),
                        RELEASE-LICENSE-PLAN.md (publication and license
                        readiness), TESTING-MACOS-ARM64.md (test procedure
                        for Apple Silicon Macs), TESTING-MACOS-X64.md (test
                        procedure for Intel Macs) and TESTING-LINUX-ARM64.md
                        (test procedure for the Raspberry Pi 5)

MAINTAINER-README.txt (MAP OF THE NATIVE DOCUMENTATION) lists the document that
describes each of these areas.


================================================================================
END OF EXTRAS-README
