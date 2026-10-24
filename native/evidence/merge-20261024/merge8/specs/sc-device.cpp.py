def b0(o,t):
    a=o[0].replace('actual==light_tree_forward.value?22:23;','actual==light_tree_forward.value?22:actual==sc_assign.value?23:actual==sc_split.value?24:actual==sc_count.value?25:26;')
    assert a!=o[0]
    return [a,o[1].replace('entry<23','entry<26')]
blocks=[b0]
