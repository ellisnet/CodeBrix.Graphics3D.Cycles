# Opcode census after M3 (M1+M2 reconciled, M3 procedural opcodes)

Base: M2 census (`m2-svm-nodes/native/work/m2/handoff/census.md`), 111 symbols.
Delta in M3: NODE_WAVELENGTH and NODE_WIREFRAME done and gated (evidence:
`validation/final/nodes-gate/` in this handoff). NODE_TEX_GABOR was ported and
passed the node gates but is withheld (see its row). Totals: 42 done before M2,
32 done in M2, 2 done in M3, 35 deferred.

Deferred and excluded by the M3 rule (other lanes): every `*_DERIVATIVE` variant,
SET_BUMP / ENTER_BUMP_EVAL / LEAVE_BUMP_EVAL (M4); SET_DISPLACEMENT /
DISPLACEMENT / VECTOR_DISPLACEMENT (displacement stage); VOLUME_COEFFICIENTS /
PRINCIPLED_VOLUME (volume lane); LIGHT_FALLOFF, IES (light-shader stage);
TEX_SKY (precompute); PARTICLE_INFO / HAIR_INFO / POINT_INFO; AMBIENT_OCCLUSION,
BEVEL, RAYCAST (shader ray tracing); AOV_START/COLOR/VALUE (new pass path).
Deferred though they meet the rule: TEX_GABOR (withheld, driver finding) and
RADIAL_TILING (time).

| Opcode | Status | Note |
| --- | --- | --- |
| NODE_END | done (before M2) |  |
| NODE_SHADER_JUMP | done (before M2) |  |
| NODE_CLOSURE_BSDF | done (before M2) |  |
| NODE_CLOSURE_EMISSION | done (before M2) |  |
| NODE_CLOSURE_BACKGROUND | done (before M2) |  |
| NODE_CLOSURE_SET_WEIGHT | done (before M2) |  |
| NODE_CLOSURE_WEIGHT | done (before M2) |  |
| NODE_EMISSION_WEIGHT | done (before M2) |  |
| NODE_MIX_CLOSURE | done (before M2) |  |
| NODE_JUMP_IF_ZERO | done (before M2) |  |
| NODE_JUMP_IF_ONE | done (before M2) |  |
| NODE_GEOMETRY | done (before M2) |  |
| NODE_GEOMETRY_DERIVATIVE | deferred | dual-number derivative variant (bump/filtered lookups); derivative lane |
| NODE_CONVERT | done (before M2) |  |
| NODE_CONVERT_DERIVATIVE | deferred | derivative variant |
| NODE_TEX_COORD | done (before M2) |  |
| NODE_TEX_COORD_DERIVATIVE | deferred | derivative variant; derivative lane |
| NODE_VALUE_F | done (before M2) |  |
| NODE_VALUE_F_DERIVATIVE | deferred | derivative variant |
| NODE_VALUE_V | done (before M2) |  |
| NODE_VALUE_V_DERIVATIVE | deferred | derivative variant |
| NODE_ATTR | done (before M2) |  |
| NODE_ATTR_DERIVATIVE | done (before M2) |  |
| NODE_VERTEX_COLOR | done (before M2) |  |
| NODE_VERTEX_COLOR_DERIVATIVE | done (before M2) |  |
| NODE_SET_DISPLACEMENT | deferred | displacement stage (not interpreted) |
| NODE_DISPLACEMENT | deferred | displacement stage |
| NODE_VECTOR_DISPLACEMENT | deferred | displacement stage |
| NODE_TEX_IMAGE | done (before M2) |  |
| NODE_TEX_IMAGE_DERIVATIVE | done (before M2) |  |
| NODE_TEX_IMAGE_BOX | done (before M2) |  |
| NODE_TEX_IMAGE_BOX_DERIVATIVE | done (before M2) |  |
| NODE_TEX_NOISE | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_SET_BUMP | deferred | bump re-evaluation (offset shading points) |
| NODE_CLOSURE_SET_NORMAL | done (before M2) |  |
| NODE_ENTER_BUMP_EVAL | deferred | bump state |
| NODE_LEAVE_BUMP_EVAL | deferred | bump state |
| NODE_HSV | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_CLOSURE_HOLDOUT | done (before M2) |  |
| NODE_FRESNEL | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_LAYER_WEIGHT | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_CLOSURE_VOLUME | done (before M2) |  |
| NODE_VOLUME_COEFFICIENTS | deferred | volume lane |
| NODE_PRINCIPLED_VOLUME | deferred | volume lane |
| NODE_MATH | done (before M2) |  |
| NODE_VECTOR_MATH | done (before M2) |  |
| NODE_VECTOR_MATH_DERIVATIVE | deferred | derivative variant |
| NODE_RGB_RAMP | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_GAMMA | done (before M2) |  |
| NODE_BRIGHTCONTRAST | done (before M2) |  |
| NODE_LIGHT_PATH | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_OBJECT_INFO | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_PARTICLE_INFO | deferred | particle data not uploaded |
| NODE_HAIR_INFO | deferred | curves not supported |
| NODE_POINT_INFO | deferred | points not supported |
| NODE_TEXTURE_MAPPING | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_TEXTURE_MAPPING_DERIVATIVE | deferred | derivative variant |
| NODE_MAPPING | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_MAPPING_DERIVATIVE | deferred | derivative variant |
| NODE_MIN_MAX | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_CAMERA | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_TEX_ENVIRONMENT | done (before M2) |  |
| NODE_TEX_ENVIRONMENT_DERIVATIVE | done (before M2) |  |
| NODE_TEX_SKY | deferred | out of scope: sky precomputation |
| NODE_TEX_GRADIENT | done (before M2) |  |
| NODE_TEX_VORONOI | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_TEX_GABOR | M3: ported, withheld | passed nodes_svm/world/render (2D/3D, all outputs, precise acos/sincos adapters) but its code in the unspecialized SVM probe module makes Intel Mesa 25.0.7 mis-evaluate Principled paths (principled_svm: stack error 8 / wrong closure / device lost; stubbed body passes); candidate WGSL in `unshipped/` |
| NODE_TEX_WAVE | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_TEX_MAGIC | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_TEX_CHECKER | done (before M2) |  |
| NODE_TEX_BRICK | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_TEX_WHITE_NOISE | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_NORMAL | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_LIGHT_FALLOFF | deferred | light-shader evaluation stage not interpreted |
| NODE_IES | deferred | IES table data (light textures) |
| NODE_CURVES | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_TANGENT | done (before M2) |  |
| NODE_TANGENT_DERIVATIVE | done (before M2) |  |
| NODE_NORMAL_MAP | done (before M2) |  |
| NODE_RADIAL_TILING | deferred (meets the M3 rule) | ~900-line piecewise r-gon kernel (radial_tiling_shared.h); not reached in the 4 h window; next candidate |
| NODE_INVERT | done (before M2) |  |
| NODE_MIX | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_SEPARATE_COLOR | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_COMBINE_COLOR | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_SEPARATE_VECTOR | done (before M2) |  |
| NODE_SEPARATE_VECTOR_DERIVATIVE | deferred | derivative variant |
| NODE_COMBINE_VECTOR | done (before M2) |  |
| NODE_COMBINE_VECTOR_DERIVATIVE | deferred | derivative variant |
| NODE_VECTOR_ROTATE | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_VECTOR_TRANSFORM | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_WIREFRAME | M3: done, gated | nodes_svm/nodes_world; vertices from the existing tri_verts/motion tables, pixel size from ShaderData dP; bit 54; render variant excluded (step function) |
| NODE_WAVELENGTH | M3: done, gated | nodes_svm/nodes_world/nodes_render; CIE rows as switch constants (no table upload, no private array); bit 53 |
| NODE_BLACKBODY | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_MAP_RANGE | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_VECTOR_MAP_RANGE | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_CLAMP | done (before M2) |  |
| NODE_BEVEL | deferred | out of scope: shader ray tracing |
| NODE_AMBIENT_OCCLUSION | deferred | out of scope: shader ray tracing |
| NODE_RAYCAST | deferred | out of scope: shader ray tracing |
| NODE_AOV_START | deferred (excluded by rule) | needs a render-buffer write path from the SVM evaluation in prepare_surface plus AOV pass allocation/readout (new pass path) |
| NODE_AOV_COLOR | deferred (excluded by rule) | as NODE_AOV_START |
| NODE_AOV_VALUE | deferred (excluded by rule) | as NODE_AOV_START |
| NODE_FLOAT_CURVE | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_MIX_COLOR | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_MIX_FLOAT | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_MIX_VECTOR | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_MIX_VECTOR_NON_UNIFORM | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_SCENE_TIME | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_NONE | deferred | sentinel |
| NODE_PAD1 | deferred | sentinel |
| NODE_NUM | deferred | sentinel |
