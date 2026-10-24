def b0(o,t):
    a=o[0].replace('"shadow_catcher","shadow_catcher","shadow_catcher"};const char *entry[]','"shadow_catcher","shadow_catcher","shadow_catcher","surface_raytrace"};const char *entry[]')
    a=a.replace('"shadow_catcher_count"};std::array<cbc::Pipeline,26>','"shadow_catcher_count","prepare_surface_raytrace"};std::array<cbc::Pipeline,27>')
    assert a.count('surface_raytrace')==2, a.count('surface_raytrace')
    return [a]+[x.replace(',26>',',27>') for x in o[1:]]
blocks=[b0]
