# M3 reconciliation of the sealed M1 (Principled) and M2 (SVM nodes) patches

Base: `01eb27b6` (clean worktree, byte-identical to `baseline-materials.json`).

| Input | sha256 (verified) | Paths |
| --- | --- | --- |
| M1 `changes.patch` | `b2072d45e257675c923172af566369179aab9d9f236b6acad20a86c1567dae13` | 21 |
| M2 `changes.patch` | `88b9d42f74a0bea0d7dc82949af5812850ac8722690c83eb8b77ebefb6c9b5a6` | 15 |

Order: M1 applied first (`git apply`, clean), then M2 with `git apply --reject`.
Ten M2 files applied cleanly on top of M1 (including the overlapping
`native/DEVICE.md`, `native/cycles/svm_layout.cpp` and
`native/tools/inject_cycles.cmake`, whose hunks touch disjoint regions). Five
files had rejected hunks; every one was resolved by hand as below. No index,
stash or ref was used.

## Conflict resolutions

| File | Rejected hunk | Resolution |
| --- | --- | --- |
| `native/cycles/pipelines.h` | M2 appended `SF_TEX_NOISE..SF_TEX_VORONOI` at bits 30-47 and `SF_ALL=(1<<48)-1`; M1 had appended `SF_BSDF_PRINCIPLED/THIN_FILM/THIN_WALL` at 30-32 and `SF_ALL=(1<<33)-1` | Both lanes renumbered. Bits 30 and 31 are left unused with a comment reserving them for the E3 shadow-terminator lane; M1 bits 32-34, M2 bits 35-52, `SF_ALL=(1<<53)-1` (contiguous, covers the reserved bits). `SF_PRINCIPLED_ALL` (M1) kept as the OR of the three M1 bits. |
| `native/cycles/pipelines.cpp` hunk 1 | M2's 18 `SVM_NODE_*` names and 7 `SVM_NODES_*` composite regions were anchored on the pre-M1 `SVM_BSDF_MICROFACET` line | Inserted after `LIGHT_TRIANGLE`, before M1's `SVM_BSDF_MICROFACET` (now `GLOSSY|GLASS|PRINCIPLED`) and M1's three Principled names. Names are looked up by string, so the specialized WGSL is independent of the numbering. |
| `native/cycles/pipelines.cpp` hunk 3 | M2's ramp/curve table walk in `svm_census` was anchored on the pre-M1 `NODE_CLOSURE_BSDF` block | Placed after M1's extended block (Principled data census + subsurface rejection), unchanged. (M2's `op_info` hunk applied cleanly.) |
| `native/cycles/surface_fixture.h` | M2's `CBC_NODES_FIXTURE` graph count and `nodes_material` call anchored on the pre-M1 fixture | `GRAPH_COUNT` switch placed after M1's `principled_fixture.h` include; `nodes_material` call placed after M1's `principled_material` call. The fixtures are mutually exclusive compile definitions. |
| `native/cycles/svm_surface_gate.cpp` | (1) M2 changed shared rejection 1 from `NODE_TEX_NOISE` to `NODE_TEX_GABOR`; M1 had changed rejection 0 from Principled to Ashikhmin velvet in the neighbouring line. (2) `node_rejections` function and (3) its call anchored on pre-M1 context | (1) both kept (M1 rejection 0 = Ashikhmin velvet, M2 rejection 1 = Gabor; with M2 present noise is implemented, so the M2 change is required for unspecialized principled/glass modules). (2) inserted before `main`, after M1's extended `rejections`. (3) `invalid+=node_rejections(...)` placed directly after `invalid=rejections(...)`, before M1's native Principled host-rejection block. |
| `docs/PARITY.md` | Both lanes rewrote the "Material coverage" row; M2 also extended "Surface SVM evaluation" | "Surface SVM evaluation": M2's text (M1 did not change it). "Material coverage": M1's row with M2's added node-coverage and deferred-node sentences inserted after the world-interpreter sentence. |
| `native/DEVICE.md` | applied cleanly (M2 section inserted mid-file, M1 section near the end) | Bit numbers in both sections corrected to the final table (text only). |

THIRD-PARTY notices: neither lane changed them; nothing to reconcile.
Gate registration/CMake: M1's `principled_{svm,bounce,render}` loop and M2's
`nodes_{svm,render}` loop + `nodes_world` target applied cleanly into separate
places of `inject_cycles.cmake`; `build_cycles.py` (M2 only: node WGSL file and
`svm_nodes_enums.inc`) applied cleanly.

## Final feature-bit table (`native/cycles/pipelines.h`)

| Bit | Name (`pipelines.h`) | Region marker | Lane |
| --- | --- | --- | --- |
| 0-29 | unchanged base table (`SF_GEOMETRY` ... `SF_LIGHT_TRIANGLE`) | as before | base |
| 30 | (unused) | reserved for E3 shadow-terminator offsets | E3 |
| 31 | (unused) | reserved for E3 | E3 |
| 32 | `SF_BSDF_PRINCIPLED` | `SVM_BSDF_PRINCIPLED` (also in `SVM_BSDF_MICROFACET`) | M1 (was 30) |
| 33 | `SF_BSDF_THIN_FILM` | `SVM_BSDF_THIN_FILM` | M1 (was 31) |
| 34 | `SF_BSDF_THIN_WALL` | `SVM_BSDF_THIN_WALL` | M1 (was 32) |
| 35 | `SF_TEX_NOISE` | `SVM_NODE_TEX_NOISE` | M2 (was 30) |
| 36 | `SF_TEX_WHITE_NOISE` | `SVM_NODE_TEX_WHITE_NOISE` | M2 (was 31) |
| 37 | `SF_TEX_WAVE` | `SVM_NODE_TEX_WAVE` | M2 (was 32) |
| 38 | `SF_TEX_MAGIC` | `SVM_NODE_TEX_MAGIC` | M2 (was 33) |
| 39 | `SF_TEX_BRICK` | `SVM_NODE_TEX_BRICK` | M2 (was 34) |
| 40 | `SF_MAPPING` | `SVM_NODE_MAPPING` | M2 (was 35) |
| 41 | `SF_MAP_RANGE` | `SVM_NODE_MAP_RANGE` | M2 (was 36) |
| 42 | `SF_MIX` | `SVM_NODE_MIX` | M2 (was 37) |
| 43 | `SF_HSV` | `SVM_NODE_HSV` | M2 (was 38) |
| 44 | `SF_COLOR_SEPCOMB` | `SVM_NODE_COLOR_SEPCOMB` | M2 (was 39) |
| 45 | `SF_RAMP` | `SVM_NODE_RAMP` | M2 (was 40) |
| 46 | `SF_FRESNEL` | `SVM_NODE_FRESNEL` | M2 (was 41) |
| 47 | `SF_VECTOR_ROTATE` | `SVM_NODE_VECTOR_ROTATE` | M2 (was 42) |
| 48 | `SF_BLACKBODY` | `SVM_NODE_BLACKBODY` | M2 (was 43) |
| 49 | `SF_NORMAL` | `SVM_NODE_NORMAL` | M2 (was 44) |
| 50 | `SF_SHADING_INFO` | `SVM_NODE_SHADING_INFO` | M2 (was 45) |
| 51 | `SF_LIGHT_PATH` | `SVM_NODE_LIGHT_PATH` | M2 (was 46) |
| 52 | `SF_TEX_VORONOI` | `SVM_NODE_TEX_VORONOI` | M2 (was 47) |
| 53 | `SF_WAVELENGTH` | `SVM_NODE_WAVELENGTH` (also `SVM_NODES_ANY`, `SVM_NODES_DOT`) | M3 |
| 54 | `SF_WIREFRAME` | `SVM_NODE_WIREFRAME` (also `SVM_NODES_ANY`, `SVM_NODES_DOT`) | M3 |

`SF_ALL=(1ull<<55)-1` (contiguous bits 0-54, including the two reserved bits:
the unspecialized module keeps every region, and E3 can claim 30/31 without
touching the mask). `SF_PRINCIPLED_ALL = bits 32|33|34`. Composite regions
(`SVM_BSDF_MICROFACET`, `SVM_NODES_{ANY,DOT,HASH,FMOD,NOISE,TRANSFORM,HSV}`)
are ORs of the bits above. Specialization looks regions up by name, so the
specialized WGSL of every scene is independent of the numbering: the glass,
Principled and node modules/films are byte-identical to the sealed M1/M2 runs.
After the job-1 interim seal, `native/DEVICE.md` text was corrected from the
lanes' own bit numbers (30-47; 30/31/32) to this table.

## Job 2 notes affecting reconciled files

- Shared rejection 1 (M2: `NODE_TEX_GABOR`) now uses `NODE_LIGHT_FALLOFF` and
  node rejections use `NODE_LIGHT_FALLOFF`/`NODE_AOV_START`/`NODE_RADIAL_TILING`
  in place of the wavelength/wireframe opcodes, plus `NODE_TEX_GABOR` (still
  deferred, error 2) as the 21st node rejection. Counts: shared 11 unchanged;
  nodes 31 -> 32.
- `surface_fixture.h` `states_for`: the node fixture's graphs 0-59 keep their
  M2 state indices (7 rays); graphs 60-62 follow with 7 rays (capacity 4,096).

