# Real-model corpus

Jeremy supplied `~/GitHome/Oasys/Assets/ForestCommunity` for rendering tests.
`tools/catalog_assets.py` performs read-only metadata inspection and saves
`work/asset-catalog.json`. It found 706 glTF/GLB documents, 696 with images,
without metadata parse errors. The inventory itself is not a renderer validation
or a fresh full load through Gltf2. Three models have now been strictly loaded
with Gltf2 1.0.290.996 and rendered through the native clay geometry gate:
bench, lime and ceramic home. See [measured results](README.md#models-supplied-by-jeremy).

Use `CodeBrix.Graphics3D.Gltf2.MitLicenseForever` to prepare imported test
scenes. Its managed loader is a test/application-side consumer; the Cycles
scene manager, rendering orchestration and WebGPU backend stay native C++.
The library's `ModelRoot.Load`, runtime `SceneTemplate`/`SceneInstance`, decoded
mesh streams, material channels and per-instance transforms expose the data
needed by a native scene API. It carries image bytes but does not decode them;
image decoding belongs in the native dependency/texture pipeline.

The existing asset collections include their own Gltf2 validation results and
provenance. Preserve those notices and keep external resources beside each
model. Original model files, textures and fonts remain external, except the
bench_01 asset and its six textures, imported at their reviewed hashes into
`native/oracle/scenes/assets/ForestVillage/` (CC0; see its `PROVENANCE.txt`). Saved rendering
evidence includes prepared geometry and numerical captures derived from the
bench, lime and ceramic-home tests. The root third-party notices record their
origins and applicable licenses; these clay tests do not import the original
textures or fonts.

Suggested progression based on inspected metadata:

| Example | What it exercises |
| --- | --- |
| `Domes/ForestVillage/CommunityProps/bench_01.gltf` | Static prop and material assignment |
| `Nature/PolyHaven/food_lime_01/food_lime_01_2k.gltf` | 8,100 triangles, one material, three images |
| `Nature/PolyHavenTextures/MaterialTiles/mud_forest.gltf` | Texture sampling and normal/material maps on 4,608 triangles |
| `Nature/PolyHaven/calathea_orbifolia_01/calathea_orbifolia_01_2k.gltf` | Multiple meshes, alpha masking, IOR/specular extensions |
| `Domes/ForestVillage/Buildings/07_ceramic_home_home.gltf` | 40,553 triangles, 21 materials, opaque and blended surfaces |
| `Nature/OregonForest/red_alder/red_alder_01_distant.gltf` | Skinned/animated vegetation and masked leaves |
| `Nature/PolyHaven/island_tree_02/island_tree_02_2k.gltf` | 1,072,213 triangles, nine images, texture transforms and transparency |

Parity is against Cycles after an explicit glTF-to-Cycles material conversion.
The corpus's alpha-blended glass is not automatically a physical transmission
material; importing it must not silently substitute a different material model.
Geometry-only tests must be labeled as such until material coverage is present.

The original bench texture license review is recorded in
[bench-texture-license-review.json](../third_party/bench-texture-license-review.json)
(2026-10-19). All six 2K JPEGs match the supplied download byte lengths and MD5
values, plus the collection's SHA-256 delivery manifest. The two texture sets are
[Rough Wood](https://polyhaven.com/a/rough_wood) by Rob Tuytel and
[Japanese Cedar Planks](https://polyhaven.com/a/japanese_cedar_planks) by Charlotte
Baglioni (scanning) and Rico Cilliers (processing). Poly Haven identifies these
asset downloads as [CC0](https://polyhaven.com/license); their use is compatible
with this Apache-2.0 project while retaining their CC0 status. The full CC0 text
is already retained in `third_party/licenses/CC0-1.0.txt`.

The review pins the external files and source documents; it does not import
them or establish original-material rendering. Before copying an image, verify
its exact reviewed hash and record the destination in the dependency notices.
The review covers the listed texture downloads only. Other collection assets,
font outlines, generator implementations and website previews need their own
review before use.

The bench has two textured wood materials and a material with metallic factor
0.8 and roughness 0.35. Rendering them requires native microfacet/Fresnel closures
and an explicit glTF-to-Cycles material conversion. That conversion must account
for the supplied normal scale, color and packed material channels, occlusion
strength 0.65, and the requested trilinear mip minification with repeat wrapping.
The existing clay preview and synthetic normal-map proof do not cover these
original material requirements.

Original-material rendering of the bench now exists: the glTF example tool
(`tools/gltf/CycleGltfRender`, mapping "blender") decodes the six reviewed JPEGs
with CodeBrix.Imaging after checking their reviewed SHA-256, builds the Blender
glTF importer's node graph (normal scale as Normal Map strength, packed
metallic/roughness channels, sRGB base colour times its factor, occlusion not
applied, repeat wrapping, linear interpolation without mipmaps) and renders it on
the CPU reference and WebGPU backends; `native/oracle/scenes/assets/` holds the
bench oracle scene documents and the expected hashes of the generated files. The
bench asset and its six reviewed textures are in the repository
(`native/oracle/scenes/assets/ForestVillage/`, CC0); the decoded and derived
pixel files are still generated locally (`native/oracle/scenes/make_bench_bundle.py`)
under `native/work/` and never committed.
