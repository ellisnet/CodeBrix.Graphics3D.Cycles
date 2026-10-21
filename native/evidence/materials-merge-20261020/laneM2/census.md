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
| NODE_TEX_GABOR | deferred | procedural, not reached in this lane (time); next candidate |
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
| NODE_RADIAL_TILING | deferred | procedural, not reached in this lane; next candidate |
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
| NODE_WIREFRAME | deferred | needs vertex positions and pixel-size differentials; deferred |
| NODE_WAVELENGTH | deferred | needs the 81x3 CIE table uploaded (no large private arrays in WGSL) |
| NODE_BLACKBODY | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_MAP_RANGE | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_VECTOR_MAP_RANGE | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_CLAMP | done (before M2) |  |
| NODE_BEVEL | deferred | out of scope: shader ray tracing |
| NODE_AMBIENT_OCCLUSION | deferred | out of scope: shader ray tracing |
| NODE_RAYCAST | deferred | out of scope: shader ray tracing |
| NODE_AOV_START | deferred | AOV film passes not plumbed |
| NODE_AOV_COLOR | deferred | AOV film passes not plumbed |
| NODE_AOV_VALUE | deferred | AOV film passes not plumbed |
| NODE_FLOAT_CURVE | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_MIX_COLOR | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_MIX_FLOAT | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_MIX_VECTOR | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_MIX_VECTOR_NON_UNIFORM | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_SCENE_TIME | this lane: done, gated | nodes_svm/nodes_world/nodes_render |
| NODE_NONE | deferred | sentinel |
| NODE_PAD1 | deferred | sentinel |
| NODE_NUM | deferred | sentinel |
