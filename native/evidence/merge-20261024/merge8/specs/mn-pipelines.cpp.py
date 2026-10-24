def b0(o,t): return [o[0].rstrip()+'{"MNEE",SF_MNEE},' if False else o[0]]+o[1:2]+[o[2].rstrip()+'{"MNEE",SF_MNEE},']
def b1(o,t):
    return ['static_assert(SF_ALL.count()==72&&!(SF_ALL&~(ShaderFeatures::range(0,66)|ShaderFeatures::range(94,97)|SF_SHADOW_CATCHER|SF_NODE_AO|SF_AO_SHADOW|SF_MNEE)).any()&&SF_LIGHT_BACKGROUND.test(64)&&SF_LIGHT_NEE.test(65)&&',
            '  SF_LIGHT_TREE.test(94)&&SF_LIGHT_LINKING.test(95)&&SF_LIGHT_SELECT.test(96)&&SF_SHADOW_CATCHER.test(102)&&SF_MNEE.test(110));', o[2]]
blocks=[b0,b1,BOTH]
