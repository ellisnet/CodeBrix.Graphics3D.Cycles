def b0(o,t):
    return o[:-2]+t[:-3]+['// Every assigned scene feature (bits 0-61, 63, 64, 65, 94-96, 102, 106 and 108;',
     '// bit 62 is unassigned; 107 is the raytrace module variant).',
     'inline constexpr ShaderFeatures SF_ALL=ShaderFeatures::range(0,62)|SF_TEX_GABOR|SF_LIGHT_BACKGROUND|SF_LIGHT_NEE|SF_LIGHT_TREE|SF_LIGHT_LINKING|SF_LIGHT_SELECT|SF_SHADOW_CATCHER|SF_NODE_AO|SF_AO_SHADOW;']
def b1(o,t): return ['  SF_VALUE_DERIVATIVE|SF_VECTOR_MATH_DERIVATIVE|SF_MAPPING_DERIVATIVE|SF_TEX_GABOR|SF_SHADOW_CATCHER|SF_NODE_AO|SF_AO_SHADOW;']
blocks=[b0,b1]
