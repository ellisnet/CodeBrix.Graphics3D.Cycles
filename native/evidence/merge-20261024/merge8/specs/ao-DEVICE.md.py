def b0(o,t):
    assert o[0].startswith('17. **Replays') and t[0].startswith('17. **Ambient')
    oo=list(o)
    oo[-1]=oo[-1]+' Merge wave 8: the ambient-occlusion stage reserves its shadow slots stably whenever AO is on'
    oo+=['   (two reserved slots per surface input and the stable serial path-array scan; "Ambient occlusion"), which made its',
         '   own replays exact; scenes without AO keep the atomic allocator unchanged, so this case still does not replay.']
    return oo+['18. **Ambient occlusion remainder.**'+t[0][len('17. **Ambient occlusion remainder.**'):]]+t[1:]
blocks=[b0]
