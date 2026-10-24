def b0(o,t):
    a=t[0].rstrip()+'+light_tree'; return [o[0],a]
def b1(o,t):
    a=t[0]+"+(ROOT/'shaders/wavefront_light_link.wgsl').read_text()"; return [a,t[1]]
blocks=[b0,b1]
