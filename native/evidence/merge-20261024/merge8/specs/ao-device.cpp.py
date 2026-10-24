def b0(o,t):
    a=o[0].replace('actual==sc_count.value?25:26;','actual==sc_count.value?25:actual==surface_prepare_raytrace.value?26:27;'); assert a!=o[0]
    return [a,o[1].replace('entry<26','entry<27')]
blocks=[b0]
