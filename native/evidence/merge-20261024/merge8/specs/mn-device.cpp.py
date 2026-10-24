def b0(o,t):
    a=o[0].replace('actual==surface_prepare_raytrace.value?26:27;','actual==surface_prepare_raytrace.value?26:actual==mnee_cleanup.value?34:mnee_entry_of(*this,actual);'); assert a!=o[0]
    return [a,o[1].replace('entry<27','entry<35')]
blocks=[b0]
