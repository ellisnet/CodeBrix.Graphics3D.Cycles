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
model. Original model files, textures and fonts remain external. Saved rendering
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
