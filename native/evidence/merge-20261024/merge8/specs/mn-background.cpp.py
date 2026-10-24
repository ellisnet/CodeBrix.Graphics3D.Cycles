def b0(o,t):
    a=o[0].replace('shadow_catcher_layout()+background_source','shadow_catcher_layout()+mnee_layout()+background_source'); assert a!=o[0]; return [a]
def b1(o,t):
    return ['  require(!(data.kernel_features&(KERNEL_FEATURE_PATH_GUIDING|KERNEL_FEATURE_DENOISING|KERNEL_FEATURE_SHADOW_LINKING)),',
            '          "WebGPU background optional integration features not implemented");', o[2], o[3], t[2]]
blocks=[b0,b1]
