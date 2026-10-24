def b0(o,t): return [o[0].replace(',27>',',35>')]
def b1(o,t): return o[:3]+['    // split module), 26 (prepare_surface_raytrace, the raytrace-specialized surface module surface_raytrace.wgsl),',
    '    // 27-33 (mnee_receiver, mnee_seed, mnee_eval, mnee_setup, mnee_solve, mnee_transfer, mnee_contribute) and 34',
    '    // (mnee_cleanup), all MNEE entries in surface.wgsl. All follow the v5 slots 9-14.'] if False else [o[0],o[1],
    '    // split module), 26 (prepare_surface_raytrace, the raytrace-specialized surface module surface_raytrace.wgsl),',
    '    // 27-33 (mnee_receiver, mnee_seed, mnee_eval, mnee_setup, mnee_solve, mnee_transfer, mnee_contribute) and 34',
    '    // (mnee_cleanup, all MNEE entries in surface.wgsl). All follow the v5 slots 9-14.']
def b2(o,t):
    a=o[0].replace('||pipeline_dispatches[26];',';for(size_t i=26;i<pipeline_dispatches.size();i++)v10|=pipeline_dispatches[i]!=0;').replace('const bool v10=','bool v10=')
    assert a!=o[0]
    return [a,o[1],'    // Entries 21-22 (light tree: shade_surface_light_select, light_tree_forward), 23-25 (the shadow catcher split',
            '    // module), 26 (prepare_surface_raytrace) and 27-34 (MNEE) are recorder v10; older formats keep 21 counts.',o[4]]
blocks=[b0,b1,b2]
