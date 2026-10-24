def b0(o,t): return [o[0].replace(',26>',',27>')]
def b2(o,t): return [o[0],'    // (shade_surface_light_select) and 22 (light_tree_forward) are recorder v10, as are 23-25 (the shadow catcher',
   '    // split module) and 26 (prepare_surface_raytrace, the raytrace-specialized surface module surface_raytrace.wgsl).',
   '    // All follow the v5 slots 9-14.']
def b3(o,t):
    a=o[0].replace('||pipeline_dispatches[25];','||pipeline_dispatches[25]||pipeline_dispatches[26];'); assert a!=o[0]
    return [a,'    // Entries 21-22 (light tree: shade_surface_light_select, light_tree_forward), 23-25 (the shadow catcher split',
            '    // module) and 26 (prepare_surface_raytrace) are recorder v10; older formats keep 21 counts.',o[3]]
blocks=[b0,BOTH,b2,b3]
