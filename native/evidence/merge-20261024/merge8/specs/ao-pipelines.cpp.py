def b0(o,t):
    return ['static_assert(SF_ALL.count()==71&&!(SF_ALL&~(ShaderFeatures::range(0,66)|ShaderFeatures::range(94,97)|SF_SHADOW_CATCHER|SF_NODE_AO|SF_AO_SHADOW)).any()&&SF_LIGHT_BACKGROUND.test(64)&&SF_LIGHT_NEE.test(65)&&',
            '  SF_LIGHT_TREE.test(94)&&SF_LIGHT_LINKING.test(95)&&SF_LIGHT_SELECT.test(96)&&SF_SHADOW_CATCHER.test(102));', t[1]]
blocks=[b0,BOTH]
