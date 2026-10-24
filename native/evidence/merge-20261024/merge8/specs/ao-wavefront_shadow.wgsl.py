def sc_on_ao(o,t,old):
    # o = [//#if SHADOW_CATCHER else <old line>, <sc line>, //#endif]; t = [<ao line>]
    assert len(o)==3 and len(t)==1 and o[0].startswith('//#if SHADOW_CATCHER else ') and o[2]=='//#endif'
    a=t[0]; assert old in a
    b=a.replace(old, old.replace('(flag)','(flag&~SC_FLAGS)').replace('(state_word(S_shadow_path_flag,index))','(state_word(S_shadow_path_flag,index)&~SC_FLAGS)'))
    assert b!=a
    return ['//#if SHADOW_CATCHER else '+a, b, '//#endif']
def b0(o,t): return sc_on_ao(o,t,'!shadow_flag_supported(state_word(S_shadow_path_flag,index))')
def b1(o,t):
    k=t.index('//#endif'); return t[:k+1]+o
def b2(o,t): return sc_on_ao(o,t,'!shadow_flag_supported(flag)')
blocks=[b0,b1,b2,b2]
