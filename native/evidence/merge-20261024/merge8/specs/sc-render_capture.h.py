def b0(o,t): return [o[0].replace('<uint64_t,23>','<uint64_t,26>')]
def b1(o,t):
    a=t[0].replace('pipeline_dispatches[21]||pipeline_dispatches[22]||pipeline_dispatches[23]','pipeline_dispatches[21]||pipeline_dispatches[22]||pipeline_dispatches[23]||pipeline_dispatches[24]||pipeline_dispatches[25]')
    assert a!=t[0]
    return [a,t[1],'    // Entries 21-22 (light tree: shade_surface_light_select, light_tree_forward) and 23-25 (the shadow catcher split',
            '    // module) are recorder v10; older formats keep 21 counts.',t[3]]
blocks=[b0,b1]
