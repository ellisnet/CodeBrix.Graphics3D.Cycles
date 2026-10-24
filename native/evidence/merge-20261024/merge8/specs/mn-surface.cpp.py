def b0(o,t):
    a=o[0].replace('shadow_catcher_layout()+surface_fragment','shadow_catcher_layout()+mnee_layout()+surface_fragment'); assert a!=o[0]; return [a]+o[1:]
def b1(o,t):
    return ['  require(!(data.kernel_features&(KERNEL_FEATURE_PATH_GUIDING|KERNEL_FEATURE_DENOISING|KERNEL_FEATURE_SHADOW_LINKING)),',
            '          "WebGPU surface optional integration features not implemented");', o[2], t[2], o[3], o[4]]
def b2(o,t):
    return ['  // Reserved shadow slots (control op 5) give every input fixed slots: needed by',
            '  // resumable transparent shadows, with ambient occlusion, whose two shadow',
            '  // paths per input otherwise take atomic slots in a run-dependent order (the',
            '  // native scheduler\'s compaction counts then differ between runs), and in caustic',
            '  // scenes (MNEE reserves its slots the same way; mnee.cpp).',
            '  const bool reserve_shadow=(data.integrator.use_direct_light&&(data.integrator.transparent_shadows||data.integrator.use_caustics))||ao;']
def b3(o,t):
    a=o[0].replace('KERNEL_FEATURE_MNEE|',''); assert a!=o[0]; return [a]
blocks=[b0,b1,b2,b3]
