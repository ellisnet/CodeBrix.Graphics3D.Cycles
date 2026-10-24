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
.glb file with the CodeBrix.Graphics3D.Gltf2.MitLicenseForever package, decodes
its texture images with the CodeBrix.Imaging.ApacheLicenseForever package (both
dependencies of this tool and the tests only, never of the library), converts it
into a Cycles scene and renders it on the GPU (WebGPU) or the explicit CPU
reference backend:

    dotnet run --project tools/gltf/CycleGltfRender -c Release -- \
        MODEL.gltf --output film.bin [--backend cpu|gpu] [--compare] \
        [--material-mapping closures|blender] [--textures DIR] \
        [--width 64 --height 48 --samples 16] [--camera N | --default-camera] \
        [--write-bundle DIR [--bundle-id ID] [--repeats N]] [--native-library PATH]
    ... --bundle SCENE.json --output film.bin [--compare]   (render a saved bundle)
    ... --write-sample sample.glb [--sample-sun | --sample-textured]

It writes the raw film (film.bin: little-endian float32 RGBA, scene-linear,
row 0 at the bottom) and a small header (film.bin.json: size, samples, backend,
sha256); it encodes no images. --compare renders on both backends, writes
film.cpu.bin and film.gpu.bin and prints the maximum error, practical failures
and strict differences with the repository criteria (exit code 3 on a practical
failure). --write-bundle saves the converted scene as a cbcOracleScene bundle
(scene.json, raw geometry, attribute and pixel files, bundle-manifest.json),
which the library's OracleSceneBundle loader and the C oracle runner render
identically. --help prints both mappings. The native host is found through
--native-library, then CODEBRIX_CYCLES_NATIVE, then the in-tree Linux build.

Mapping "closures" (the default; factors only, textures ignored with a warning):

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

Mapping "blender" (the node graph Blender's glTF importer builds):

    baseColorTexture x factor    Image Texture (scene_linear_srgb) -> Mix Color
                                 MULTIPLY (fac 1, B = factor) -> Base Color
    metallicRoughnessTexture     Image Texture (data) -> Separate Color RGB:
                                 B x metallicFactor -> Metallic,
                                 G x roughnessFactor -> Roughness
    normalTexture (scale)        Image Texture (data) -> Normal Map (tangent
                                 space, OpenGL, strength = scale) -> Normal,
                                 with the glTF TANGENT as the tangent frame
    occlusionTexture             not applied (reported)
    emissive factor/texture      Emission Color / Strength
    sampler                      REPEAT/CLAMP_TO_EDGE/MIRRORED_REPEAT ->
                                 periodic/clamp/mirror; NEAREST -> closest,
                                 otherwise linear; no mipmaps in Cycles
    everything                   one Principled BSDF (multiscatter GGX, IOR 1.5
                                 or KHR_materials_ior)

Both mappings: one Cycles mesh per glTF mesh (primitives as per-face shaders),
one object per node instance (shared meshes and EXT_mesh_gpu_instancing become
instances); NORMAL -> smooth shading; TEXCOORD_0 -> "uv" corner attribute with v
flipped; KHR_lights_punctual directional/point/spot -> sun/point/spot (a default
sun when the file has none); the first perspective camera, otherwise a default
camera framing the bounds.

Limits, stated plainly: KHR_texture_transform, TEXCOORD_1 and later, the
KHR_materials_* extensions other than unlit, emissive_strength and ior, skins,
morph targets, animations, orthographic cameras and light ranges are reported
and not converted; a normal map on a primitive without TANGENT is refused
(MikkTSpace generation is not implemented). The tests in
tests/CodeBrix.Graphics3D.Cycles.Tests/GltfSceneConverterTests.cs drive both
mappings on procedural sample models (synthetic textures; no external assets).


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
