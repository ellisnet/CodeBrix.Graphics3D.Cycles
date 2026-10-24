def b0(o,t):
    return o[:-2]+t[:-2]+['// Every assigned feature (bits 0-61, 63, 64, 65, 94-96 and 102; bit 62 is unassigned).',
     'inline constexpr ShaderFeatures SF_ALL=ShaderFeatures::range(0,62)|SF_TEX_GABOR|SF_LIGHT_BACKGROUND|SF_LIGHT_NEE|SF_LIGHT_TREE|SF_LIGHT_LINKING|SF_LIGHT_SELECT|SF_SHADOW_CATCHER;']
blocks=[b0]
