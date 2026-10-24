def b0(o,t):
    assert t[0].startswith('17. **MNEE on the Adreno 730')
    item=['19. **MNEE on the Adreno 730'+t[0][len('17. **MNEE on the Adreno 730'):]]+t[1:]
    item20=['20. **MNEE with the light tree (refused).** Caustic scenes with the integrator\'s light tree, light or shadow',
            '   linking, AO, volumes or motion blur are refused before rendering ("Manifold next-event estimation (caustics)");',
            '   MNEE would call the light tree\'s selection in its own entry. Shadow linking is the next lighting work (Milestone 2,',
            '   wave B); the light-tree combination is open with it.']
    return o+item+item20
def b1(o,t):
    tt=[x.replace('open item 17','open item 19') for x in t]
    while o and o[-1]=='': o=o[:-1]
    return o+['']+tt
blocks=[b0,b1]
