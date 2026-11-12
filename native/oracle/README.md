# Cycles oracle runner

Copyright (c) 2026 Jeremy Ellis and contributors

Reference renders that validate the WGSL renderer against independent Cycles
backends. One scene file drives every backend on every platform, through the
public C API only, so the corpus also tests that API. Films are raw float32
buffers; disagreements between reference backends are findings, never tuned away.

## Build

The runner (`runner.cpp`, with `json.h` and `sha256.h`) links only the host
library. On macOS, `python3 tools/build_macos.py` builds it with the Metal host
at `native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle`. On Linux
the host build also produces it as the CMake target `codebrix_cycles_oracle`
(`python3 native/tools/build_cycles.py --target codebrix_cycles_oracle`, output
`native/work/cycles-build/bin/codebrix_cycles_oracle`). Standalone, against any
built host library:

```sh
cmake -S native/oracle -B native/work/oracle-build -DCMAKE_BUILD_TYPE=Release \
  -DCODEBRIX_HOST_LIBRARY=native/work/cycles-build/lib/libcodebrix_cycles_host.so
cmake --build native/work/oracle-build
```

## The CPU oracle's arithmetic (one definition on every platform)

The CPU backends are unmodified Cycles CPU kernels built at a fixed baseline, so a
CPU film does not depend on the machine that built it: no floating-point
contraction (`-ffp-contract=off`), no reciprocal math (`-fno-reciprocal-math`),
and a fixed ISA level: Cycles' SSE4.2 kernels only on x86-64 (never
`-march=native`, no AVX2 kernel), the architecture's base level on ARM64 (no
`-mcpu`). Only build flags change; the pinned source is not edited. The shared
CMake injection (`native/tools/inject_cycles.cmake`, and
`inject_cycles_macos.cmake` for osx-arm64) passes the two math options after
Cycles' own `-ffp-contract=on -freciprocal-math`, the build scripts choose the ISA
level and refuse a CPU kernel compile line that is not at the baseline, and
`native/cycles/cpu_arithmetic.cpp`, compiled with the kernels' flags, refuses an
x86-64 oracle with AVX/FMA or reciprocal math and states the arithmetic in the
capability report (`backendScope.cpu.arithmetic`). Each run's `meta.json`
records the CPU kernel compile line (`cpuKernelCompile`: flags, the deciding
`fpContract` and `reciprocalMath`). Android's on-device CPU is a diagnostic,
never the oracle, and keeps Cycles' flags. The WGSL device and the gates'
reference programs model exactly this arithmetic (native/DEVICE.md,
"Fixed-baseline CPU oracle"). CPU films rendered before the fixed baseline
(under `native/evidence/`) came from `-march=native` builds with contraction and
reciprocal math and are not comparable bit for bit.

## Scene files (`cbcOracleScene` version 1)

`scenes/<family>/<name>.json` holds `id`, `description`, `sourceFixture`,
`render` (`width`, `height`, `samples`, `repeats`, default 2), `colorConfig`
(`null` or an OCIO config path relative to the scene), `nodes` and `images`.
`scenes/author_starters.py` regenerates the starter scenes exactly.

Each node entry has a unique `handle` and either `scene` (a singleton:
`integrator`, `camera`, `film`, `background`) or `create` (a type accepted by
`cbc_host_create_node`, for example `shader`, `mesh`, `object`, `pointlight`,
`arealight`). Entries are applied in order:

- `graph`: `nodes` (`handle`, Cycles shader node `type`, optional `set`) and
  `connect` as `[from, output, to, input]`; `output` is the shader output node.
  Handles inside a graph shadow scene handles.
- `mesh`: `positions` and `indices` (or `positionsFile` and `indicesFile`: raw
  little-endian float32 xyz and uint32 files beside the scene), optional
  `attributes` (`name`, `value`, `domain`, `flags`, then `data` floats, `bytes`,
  or `file`: a raw float32 file, raw bytes for `byte4`) and
  optional `motion` (`steps`, odd, and `positions`: `steps` time-ordered vertex
  arrays whose center equals `positions`; `cbc_host_motion_positions`). Face
  material indices and smoothing are the mesh sockets `shader` and `smooth` in
  `set`; object and camera motion are their `motion` transform arrays.
- `hair`: `keys` (x, y, z triples) and `radius` (one per key), or `keysFile` and `radiusFile` (raw little-endian
  float32), `firstKey` or `firstKeyFile` (uint32, one per curve), optional `shape` (`ribbon`, `thick`, `thick_linear`;
  `cbc_host_curve_shape`) and `attributes` as for a mesh, with the domains `curve`, `curve_key` and `curve_key_normal`.
  `render.curveSubdivisions` (optional, 0 to 4) sets the ribbon sections per segment for the scene. Optional `motion`:
  `steps` (odd, 3 to 129), `keys` or `keysFile` (time-ordered key positions, the centre step equal to `keys`) and optional
  `radius` or `radiusFile` (`cbc_host_curve_motion`).
- `points`: `positions` (x, y, z triples) and `radius` (one per point), or `positionsFile` and `radiusFile` (raw little-endian
  float32), optional `attributes` as for a mesh, with the domain `vertex`, and optional `motion` (`steps`, odd, `positions` and
  `radius`: `steps` time-ordered arrays whose center equals the points; `cbc_host_point_motion`). Point material indices are the socket `shader` in `set`.
- `particles` (on a `particle_system`): an array of `index`, `age`, `lifetime`, `location`, `rotation`, `size`, `velocity` and `angularVelocity`
  (`cbc_host_particles`), which objects name through their sockets `particle_system` and `particle_index` in `set`.
- `set`: socket names and values exactly as Cycles registers them (see
  `cbc_host_registry_json`). The loader picks the setter from
  `cbc_host_property_info`: numbers, booleans, strings or enum labels, flat or
  nested number arrays, and node handles. The pseudo-socket `image` binds an
  entry of `images` to an image or environment texture node; set that node's
  `colorspace` (`__builtin_srgb`, `scene_linear`, `data`) and `alpha_type`
  (`unassociated`, `associated`, ...) explicitly.

`images` entries name a raw pixel file beside the scene: tightly packed rows,
bottom row first, in one of `float`, `float4`, `half`, `half4`, `byte`, `byte4`,
`ushort`, `ushort4`. Anything the loader cannot express fails with the socket named.
To reproduce a scene exactly across backends, set every integrator, film and
camera control the result depends on; do not rely on defaults.

A scene whose result the pinned gold-standard kernels leave undefined names its oracle reference instead:
`"oracleReference": {"scene": ID, "withoutHandles": [HANDLE, ...], "reason": TEXT}`. `render_oracles.py` renders the
gold-standard backends (`cpu-*`, `metal-*`) from that committed scene, which must be the same document without exactly
the listed node entries (and without `oracleReference`; `id` and `description` may differ), and records
`oracleReferenceFor` (the scene's id and SHA-256) in their `meta.json`; `compare.py` then judges the candidate's film of the
scene against those films. The only such scenes are `surface-nodes/local-beside-instances-*` (native/DEVICE.md, open
items 18 and 34).

The library's managed loader (`CodeBrix.Graphics3D.Cycles.Bundles.OracleSceneBundle`)
reads the same format and builds the same scene through the managed API; on the
CPU reference backend its films are bit-identical to the runner's for the same
file. The Android validation app renders a bundle with it (`android_app_gate.py
--mode bundle`, see MAINTAINER-README.txt).

## Bench oracle scene (`scenes/assets/`)

`bench_01.json` is the textured ForestCommunity bench with its original materials
(the Blender glTF importer's node graph: Principled BSDF, image textures,
Separate Color, Mix Color, tangent-space normal map from the glTF TANGENT),
lit by the converter's default sun and a constant world. `bench_01-hdri.json`
is the same bench lit only by a procedural environment (512x256 float4,
importance sampled through a background light with `use_mis` and
`map_resolution` 512). The asset itself (`bench_01.gltf`, its buffer and the six
CC0 textures) is in the repository under `scenes/assets/ForestVillage/` with its
`PROVENANCE.txt`; the two scene documents and `manifest.json` are committed; the
raw geometry, attribute and texture files the runner needs are generated, not
committed. Generate them on a machine with .NET:

```sh
python3 native/oracle/scenes/make_bench_bundle.py \
  --output native/work/oracle-assets/bench_01
```

It runs the glTF example tool (which decodes the textures with CodeBrix.Imaging)
and `scenes/author_environment.py` (the procedural environment: IEEE arithmetic
only, the same bytes on every platform), requires both scene documents to be
byte-identical to the committed ones and every raw file to match the SHA-256
values in `manifest.json` (which also records the decoder identity), and leaves
`bench_01.json` and `bench_01-hdri.json` in the output directory for the runner.
If a generated raw file does not match `manifest.json` (a different decoder
build), or on a machine without .NET, copy a verified bundle directory (the two
scene documents, `bundle-manifest.json` and every raw file) from a machine that
produced it and check its files against `manifest.json`; nothing else is needed to
render it.

## Linux gate corpus (`scenes/linux-gates/`)

`python3 native/oracle/export_scenes.py` regenerates the corpus byte for byte:
the maintained Linux gate fixtures re-expressed through the C ABI, each with
`sourceFixture` naming the gate source and case. It holds glass-render (the 20
GGX refraction/glass graphs), textured-plane (13x9 byte4 cubic texture from the
image fixture formula), world-volume and closed-volumes, triangle-lighting
modes 0, 1, 2, 3 and 5, broad-lighting 0-11, transparent-lighting 0-7,
object-motion 0-7 and deforming-motion 0-7. `scenes/linux-gates/manifest.json`
lists the SHA-256 of every scene and pixel file. The gates render their
fixtures with their own scheduler and override some kernel data directly, so
these scenes are compared backend against backend (CPU, WebGPU, Metal), never
with the gates' films. Integrator controls a gate leaves at their defaults stay
at the pinned Cycles defaults; Session-only denoising is disabled.

## Combination scenes (`scenes/combinations/`, `scenes/light-motion/`)

`author_combinations.py` (`--check`) writes `scenes/combinations/`: committed
scenes with features added (light tree, light linking, AO, glass/Principled,
camera motion, terminator offsets, transparent shadows), and the
`enclosure-emissive-panel*` scenes: an emissive panel sampled nearly edge-on
inside a diffuse, Oren-Nayar, translucent or mixed-material enclosure, which
amplifies any last-bit difference in the surface bounce (native/DEVICE.md,
Resolved, item 27).
`author_light_motion_combinations.py` writes `scenes/light-motion/` (light
linking, light `cast_shadow`/`max_bounces`, combined motion, clamps, caustic
filters and exclude flags; it also writes `sky.float4`). Both are judged CPU
against WGSL like every scene.

## Film pass scenes (`scenes/passes/`)

`author_passes.py` (`--check`) writes `scenes/passes/`: scenes that enable named
passes with `pass` nodes. `light-passes` (diffuse, glossy and transmission direct,
indirect, colour and their combined passes, emission, background), `data-passes`
(depth, position, normal, roughness, UV, object and material index, mist, render
time), `light-groups`, `aovs`, `cryptomatte` (object, material and asset ids; named
objects and shaders), `motion` and `motion-deforming` (the motion pass without motion
blur; the latter with deforming meshes), `denoising-features`, `denoising-features-weighted`,
`denoising-motion` and `denoising-motion-deforming` (the denoising feature passes; no denoiser runs), and `with-<family>-<scene>`: committed
scenes of the other families with every light, data, AO and denoising feature pass
added (raw files stay beside the source scene).

The runner writes every named pass the scene created after each repeat:
`pass-K-N.f32` (K the pass's position in `meta.json` `passes`, N the repeat;
float32 little-endian, the listed number of components per pixel, lower-left
origin, read with `cbc_host_pass_floats`, so through the pinned pass accessor),
and `meta.json` lists each pass's name, type, components and film SHA-256.
Scenes without named passes write no pass fields. `compare.py` judges every pass
of the reference of a pair against the candidate's pass of the same name with the
pair's role: float passes with the practical criterion (strict reported), the
object and material index exactly, cryptomatte per pixel as the set of (id,
weight) slots (ids exactly, weights by the practical criterion, the slot order
of equal weights reported), and render time reported only (it is this run's
timing). A missing pass, a different shape, NaN/Inf or a hash mismatch fails.

## Camera, sampling, lamp and sky scenes (`scenes/cameras/`, `scenes/sampling/`, `scenes/lamps/`, `scenes/sky/`)

`author_sampling.py` (`--check`) writes `scenes/sampling/`: glossy, glass and
diffuse spheres in a lit room rendered with adaptive sampling (thresholds,
automatic settings, a render that ends when every pixel converges; the
`Sample Count` pass makes the per-pixel convergence comparable), with the
Sobol-Burley and blue-noise patterns, with scrambling distance, and pattern
variants of committed scenes (raw files stay beside the source scene).
`author_cameras.py` (`--check`) writes `scenes/cameras/`: the same room seen
through orthographic and panoramic cameras (every panorama type) and with depth
of field (disk and polygonal apertures, anamorphic ratio). `author_cameras_b.py` (`--check`) adds spherical stereo
(`cameras/stereo-*`: perspective and panoramic cameras, left and right eyes, convergence, pole merge, depth of field), the
Texture Coordinate node's Window output through every panorama type (`cameras/window-*`), and `passes/motion-*` and
`passes/denoising-motion-*`: the motion pass and the denoising motion features through an orthographic camera and every
panorama type. `author_lamps.py`
(`--check`) writes `scenes/lamps/`: the Light Falloff and IES Texture nodes and
point, spot, area and sun lights with emission shaders that are not constant.
`author_sky.py` (`--check`) writes `scenes/sky/`: the room under a Sky Texture
world (Preetham, Hosek-Wilkie, single and multiple scattering), with the sun
disc sampled through the importance map or with sun guiding, and sun guiding with
an emissive panel, the light tree, or both.

## Volume scenes (`scenes/volumes/`)

`author_volumes.py` (`--check`) writes `scenes/volumes/`: a homogeneous medium box around the
middle sphere of the sampling family's room, lit by a point light (MIS, distance and
equiangular scatter sampling), a sun or the world through a background light, with an
absorbing-only medium, a world volume, the Volume Coefficients and Principled Volume nodes, an
emissive medium and the Rayleigh, Draine, Fournier-Forand and Mie (two droplet sizes) phase functions.
The same box with noise density and biased ray marching (`heterogeneous-*`, `ray-marching-*`), spot, area and
mesh lights (`scatter-spot`, `scatter-area*`, `scatter-mesh-light`, `heterogeneous-spot`), the light tree
(`tree-*`), the volume light passes (`passes-*`) and shadow catchers inside or behind the media (`catcher-*`).
`author_volumes_b.py` (`--check`) writes more of `scenes/volumes/` from those scenes: the sampling patterns other than
tabulated Sobol (`sobol-burley-*`, `blue-noise-*`), Open Image Denoise (`denoise-*`, judged as `scenes/denoise/`), light
linking (`linking-*`), a Light Path node in the media (`light-path-*`), the heterogeneous box lit only by the world
(`heterogeneous-world`) and BSDF nodes in the medium's Volume socket (`bsdf-in-volume`).
`author_volumes_motion.py` (`--check`) writes the motion blur scenes there (`motion-*`): a static medium in a scene with
camera motion beside a moving and a deforming surface object, a moving heterogeneous medium seen from outside, and a
moving and a deforming scattering medium around the camera.

## World scenes (`scenes/world/`)

`author_world.py` (`--check`) writes `scenes/world/`: the sampling family's room with a window in the ceiling under a Sky
Texture world sampled through a background light: background light portals (`portal-*`: over the window and at the open
side, with and without the importance map, with the light tree, with sun guiding, and over the homogeneous and
heterogeneous medium boxes), two background light objects (`two-background-lights*`) and the Ray Portal BSDF on a panel
in front of the spheres (`ray-portal*`).

## Denoising scenes (`scenes/denoise/`)

`author_denoise.py` (`--check`) writes `scenes/denoise/` from committed scenes:
`enclosure-emissive-panel` (combinations), `beckmann` (bsdf) and
`catcher-point-light` (shadow-catcher), each with Open Image Denoise on (`-on`:
prefilter accurate, quality high, start sample 0, CPU device) and as a control
with denoising off (`-off`); the shadow catcher pair also requests the noisy
passes (`-on-noisy`, `-off-noisy`). Every backend denoises with CPU Open Image
Denoise, so the WGSL film is judged against the CPU film denoised the same way,
with `compare.py` like every scene. Light groups with denoising are refused by
the product, so that combination has no scene.

## Curve scenes (`scenes/curves/`)

`author_curves.py` (`--check`) writes `scenes/curves/`: tufts of ribbons, thick curves and thick linear curves in the
sampling family's room: each shape alone, instanced with rotated, non-uniform and mirrored transforms, under the closures
the WebGPU device has, per curve and per key attributes, UVs, the Hair Info node, the Geometry and Texture Coordinate
nodes with a bump, emission, area and sun lights, the light tree and data passes, through the public C API.

## Curve scenes B (`scenes/curves-b/`)

`author_curves_b.py` (`--check`) writes `scenes/curves-b/` in the same room: deformation motion on every shape (three steps;
five steps with radii per step), object motion, moving and static instances together, camera motion with both, the Hair
Info node on deforming curves, and the motion pass without motion blur (static, deforming, moving); transparent shadows
through baked curve transparency (the opacity cutoff, the hit limit, the Light Path node, motion, shadow catchers, fast GI).

## Point scenes (`scenes/points/`)

`author_points.py` (`--check`) writes `scenes/points/`: balls of points in the sampling family's room, single-user and instanced, under the closures
the WebGPU device has, point attributes, UVs, the Point Info and Particle Info nodes, the Geometry and Texture Coordinate nodes with a bump, emission,
transparency, area and sun lights, the light tree, data passes, camera, object and deformation motion and the motion pass, through the public C API.
The `beside-*` and `*-on-points` scenes add points to a committed scene of another family: AO, Bevel and Raycast nodes, subsurface scattering, caustics, volumes,
displacement, shadow and light linking. `with-curves*` mix them with curves.

## Hair closure scenes (`scenes/hair/`)

`author_hair.py` (`--check`) writes `scenes/hair/`: the Hair BSDF node (`reflection-*`, `transmission-*`) and the Principled
Hair BSDF node's Chiang model (`chiang-*`) in its three colour parametrizations, on the three curve shapes in the curve family's
room and on a triangle sphere: roughness clamps, a linked tangent and random input, the curve random attribute, coat and tilt,
a zero colour channel, Filter Glossy, closure mixtures, data, colour and denoising passes and the light tree.
`author_hair_huang.py` (`--check`) adds the Principled Hair BSDF node's Huang model (`huang-*`) in the sampling family's room:
its three colour parametrizations, circular and elliptical cross-sections, lobe weights, roughness and tilt, the Random and
`curve_random` inputs, near and far field, a mix with other closures, data and denoising passes, the shader's transparent shadow
on (`huang-default-shadows-thick`) and the model on triangles.

## Subsurface scenes (`scenes/subsurface/`)

`author_subsurface.py` (`--check`) writes `scenes/subsurface/`: the sampling family's room whose middle (and right)
spheres carry subsurface materials: the Principled BSDF's random walk, skin, wax (forward anisotropy) and marble-like
(half subsurface, backward anisotropy, a zero radius channel) settings and the Subsurface Scattering node with the
random walk, legacy random walk and skin methods. `author_subsurface_disk.py` (`--check`) adds the Christensen-Burley
method there: on the Principled BSDF, on the node beside a random walk node, with the Sobol-Burley pattern, and with
points in the scene and carrying a Burley closure. `author_subsurface_thin.py` (`--check`) adds thin-walled Principled
spheres there: diffuse and translucent, Oren-Nayar and rough translucent closures, with transmission and a coat, and the
Christensen-Burley method selected on a thin wall.
`author_subsurface_combinations.py` (`--check`) puts a random walk and a Christensen-Burley sphere there together with
film passes (light, data, AOV, cryptomatte and denoising feature passes), denoising, light and shadow linking, holdout
objects, the shadow catcher, object and deformation motion blur, volumes and MNEE caustics from point and sun lights:
`with-passes`, `aov-cryptomatte-bump`, `aov-only-bump`, `denoise-on`, `light-linking`, `shadow-linking`, `holdout`,
`shadow-catcher`, `object-motion`, `deformation-motion`, `volume-crossing`, `volume-enclosed`, `mnee-receivers`.

## Surface node and displacement additions

`author_only_local.py` (`--check`) writes `scenes/surface-nodes/local-beside-instances-*`: the Ambient Occlusion node with
only_local, the Bevel node and the Raycast node with only_local on a transform-applied object beside instances, each with
its oracle reference (the same scene without the instances; see "Scene files"). `author_geometry_tangent.py` (`--check`)
writes `scenes/surface-nodes/geometry-tangent*` (the Geometry node's Tangent output as a colour and as a Bump height),
`scenes/light-motion/deforming-smooth-normal-bump` (a deforming smooth mesh bumped from the Texture Coordinate node's Normal)
and `scenes/displacement/stateful-nodes` (true displacement by the Ambient Occlusion, Light Path, Wireframe, Bevel and
Raycast nodes). `author_light_motion_b.py` (`--check`) writes `scenes/light-motion/moving-lamps*`: object motion on point,
spot and area lamps, with an object-space emission shader in one.

## Bake scenes (`scenes/bake/`)

`author_bake.py` (`--check`) writes `scenes/bake/`: texture bakes in the sampling family's room, the target a textured,
UV-mapped sphere (or the floor). A bake scene's `render` block names the bake, `"bake": {"object": HANDLE, "type": NAME,
"flags": ["use_camera", "include_albedo"], "uv": NAME, "aov": NAME}`; the runner then calls `cbc_host_bake` at the
render's size and samples for every repeat, and each `film-N.f32` holds the bake, `filmComponents` floats per pixel
(meta.json), row 0 at v = 0. Every bake type the host exposes, the two flags, a named vertex-domain UV layout, an
instanced target, the AOVs, adaptive sampling, denoising and a shadow catcher target. `compare.py` judges bake films
like render films, per component; `--previews` handles RGBA render films only.

## Run, compare and bundle

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/<new run> \
  --runner <codebrix_cycles_oracle> --host-library <host library> [--backend NAME] [--scene FILE]
python3 native/oracle/compare.py REFERENCE_DIR OTHER_DIR [...] [--json OUT]
python3 native/oracle/compare.py --scene-root RUN_DIR SCENE_ID [--json OUT]
python3 native/oracle/compare.py --self-test
python3 native/oracle/render_oracles.py --bundle native/work/oracles/<run>
```

Each run writes `oracle/<scene id>/<backend>/film-N.f32` (RGBA float32,
little-endian, lower-left origin) and `meta.json`: scene and film SHA-256,
repeat statistics, render times, host library and runner hashes, repository
commit, OS, CPU, GPU, compiler, Xcode, CMake, CPU kernel flags and, for Metal,
the fixed settings. The runner refuses a non-empty output directory, so films
are never overwritten.

Linux backends: `cpu-x64-linux` and `wgsl-linux-intel` (WebGPU). A backend
that rejects a scene is recorded under `failures` in `run.json` and the run
continues; such a scene is compared only among the backends that rendered it.

macOS backends: `cpu-arm64-macos`; `metal-m2pro` (MetalRT off, so BVH2, and
generic kernels, through `CYCLES_METALRT=0` and
`CYCLES_METAL_SPECIALIZATION_LEVEL=0`); and `metal-m2pro-metalrt`, which
forces MetalRT on to validate the path M3 and later GPUs use by default. Each
Metal backend gets a fresh Cycles kernel cache in the run directory, and its
first render is recorded as `kernelCompileSeconds`.

macOS x64 (Intel Mac) backends: `cpu-x64-macos` (the fixed-baseline CPU
oracle: SSE4.2 kernels only), `cpu-x64-macos-sse42` (`CYCLES_CPU_NO_AVX2=1`,
kept for comparison with older runs; without an AVX2 kernel it runs the same
kernels) and
`wgsl-macos-x64` (the CodeBrix WGSL device on the shipped runtime's Metal route).
`python3 tools/build_macos_x64.py` builds the runner at
`native/work/macos/osx-x64/oracle-build/codebrix_cycles_oracle`; see
[MACOS_X64.md](../host/MACOS_X64.md). On an Intel Mac the bundle and its
evidence directory are named `cycles-oracles-macos-x64-<date>`. Without
`--backend`, a run renders the Apple Silicon and Linux backends only.

`compare.py` is the multi-backend comparator: it takes any number of backend
directories for one scene (`cpu-x64-linux`, `cpu-arm64-macos`, `metal-m2pro`,
`wgsl-linux-intel`, `wgsl-android-<device>`, `cpu-arm64-android-<device>`, ...;
`--scene-root` collects every backend under `RUN_DIR/oracle/SCENE_ID`, CPU
backends first, so a Mac bundle drops in as more directories) and reports the
pairwise matrix: practical failures, strict differences, maximum absolute and
relative error, RMS, the worst pixel and NaN/Inf counts, plus every backend's
repeat films re-read and compared with film-0.

Two gold standards. The unmodified pinned Cycles CPU kernels (`cpu-*` backends)
and the unmodified pinned Cycles Metal backend (`metal-*`) are both reference
renderers; every other backend (`wgsl-*`) is a candidate. A candidate is judged
against the CPU gold standard of its own ISA family (`wgsl-linux-intel` against
`cpu-x64-linux`; an arm64-host candidate against `cpu-arm64-*`; when no same-ISA
CPU film is present, against the CPU film that is, and the report says so). A
WGSL film's `meta.json` records `rid` and `wgslArithmetic`, the CPU oracle
arithmetic its generated adapters reproduced (from the device report,
native/host/ABI.md), and that decides its family: an Android ARM64 film in the
product default (`arm64-scalar`) gates against the Linux ARM64 CPU oracle
`cpu-arm64-linux` (native/evidence/linux-arm64-cpu-oracle-20261031/), a film
rendered with `CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2` (`x64-sse4.2`)
and every Android x64 film against `cpu-x64-linux` (native/DEVICE.md, "Validation
reference policy", Option A). A `wgsl-android-*` film without the field (made
before it was recorded) is judged as x64; the report marks it (`isaSource`,
`legacyAndroidIsa`). For an Android oracle-corpus run, put the device film beside
both CPU films (`cpu-x64-linux` from the Linux x64 run of the same tree,
`cpu-arm64-linux` from the ARM64 oracle) and the verdict follows the film's own
arithmetic; the other pair is reported. That pair is `gating`, with the CPU film as the reference `b`, and the practical criterion
`abs(a - b) <= 5e-5 + 2e-4 * abs(b)` must hold for every component; strict
differences (`1e-7 + 1e-7 * abs(b)`) are reported. Cross-ISA pairs (a CPU x64
film against a CPU arm64 film, an x64 candidate against a CPU arm64 film),
candidate-versus-Metal pairs, the pairs of gold standards and candidate-versus-candidate pairs are `reported`
with the same numbers and never fail the run: Metal's differences from the CPU
(path divergence, hardware texture filtering, MetalRT's motion interpolation)
are characteristics of a second reference renderer, not defects or deviations.
The verdict is `perfect` when every gating pair passes and nothing else failed;
the table and the JSON mark each pair `gating` or `reported` and list the
`goldStandards` and `candidates`. A run without a CPU gold standard is invalid
input ("no gold standard", exit 2). NaN/Inf, shape or scene mismatches and films
whose bytes differ from their recorded SHA-256 fail.

Previews (`film-0.png`: Reinhard, then 1/2.2 power, rows flipped) come from a
scratch .NET tool outside the repository build that uses the CodeBrix.Imaging
NuGet package. Bitmap textures of glTF assets are converted into raw pixel files
by the glTF example tool (`tools/gltf/CycleGltfRender`, CodeBrix.Imaging).
The bundle (`cycles-oracles-macos-<date>.tar.zst`) carries a `manifest.json`
with the SHA-256 of every file and is verified by extracting it and re-hashing.
That verified extraction is published as `native/evidence/cycles-oracles-macos-<date>/`
(a numeric suffix is added rather than overwriting existing evidence), so other
platforms compare against the repository copy.
