def b0(o,t):
    a=o[0].replace('"surface","background"};const char *entry[]','"surface","background","shadow_catcher","shadow_catcher","shadow_catcher"};const char *entry[]')
    a=a.replace('"light_tree_forward"};std::array<cbc::Pipeline,23>','"light_tree_forward","shadow_catcher_assign","shadow_catcher_split","shadow_catcher_count"};std::array<cbc::Pipeline,26>')
    assert a.count('shadow_catcher')==6
    return [a]+[x.replace(',23>',',26>') for x in o[1:]]
blocks=[b0]
