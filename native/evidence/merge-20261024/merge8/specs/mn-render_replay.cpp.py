def b0(o,t):
    a=o[0].replace('"surface_raytrace"};const char *entry[]','"surface_raytrace","surface","surface","surface","surface","surface","surface","surface","surface"};const char *entry[]')
    a=a.replace('"prepare_surface_raytrace"};std::array<cbc::Pipeline,27>','"prepare_surface_raytrace","mnee_receiver","mnee_seed","mnee_eval","mnee_setup","mnee_solve","mnee_transfer","mnee_contribute","mnee_cleanup"};std::array<cbc::Pipeline,35>')
    assert a.count('mnee_')==8, a.count('mnee_')
    return [a]+[x.replace(',27>',',35>') for x in o[1:]]
blocks=[b0]
