#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Survey helper (scratch): SHA-256 of every Mach-O section's file bytes, of the LINKEDIT symbol table (nlist entries
without the debug-map N_OSO timestamps) and of the string table, for two binaries, so that a control build can be compared
with the validated build beyond the whole-file hash. Usage: macho_sections.py A B"""
import hashlib,struct,sys
def parse(path):
    data=open(path,'rb').read();magic,cpu,sub,ftype,ncmds,sizecmds,flags,res=struct.unpack_from('<IiiIIIII',data,0)
    assert magic==0xfeedfacf;off=32;out={};symtab=None
    for _ in range(ncmds):
        cmd,size=struct.unpack_from('<II',data,off)
        if cmd==0x19:
            segname=data[off+8:off+24].rstrip(b'\0').decode();nsects=struct.unpack_from('<I',data,off+64)[0]
            for i in range(nsects):
                s=off+72+i*80;sect=data[s:s+16].rstrip(b'\0').decode();addr,sz,fo=struct.unpack_from('<QQI',data,s+32)
                stype=struct.unpack_from('<I',data,s+64)[0]&0xff
                body=b'' if stype in (1,0x0c,0x12) else data[fo:fo+sz]
                out[f'{segname},{sect}']=(sz,hashlib.sha256(body).hexdigest())
        elif cmd==0x2:symtab=struct.unpack_from('<IIII',data,off+8)
        off+=size
    symoff,nsyms,stroff,strsize=symtab;strtab=data[stroff:stroff+strsize]
    def name(i):e=strtab.index(b'\0',i);return strtab[i:e].decode('utf-8','replace')
    plain=hashlib.sha256();oso=[];count=0
    for k in range(nsyms):
        strx,ntype,nsect,ndesc,nvalue=struct.unpack_from('<IBBhQ',data,symoff+16*k);n=name(strx)
        if ntype==0x66:oso.append(n);continue  # N_OSO: object path and mtime
        plain.update(f'{n}|{ntype}|{nsect}|{ndesc}|{nvalue}\n'.encode());count+=1
    out['symbols-without-N_OSO']=(count,plain.hexdigest())
    return out,oso
a,oa=parse(sys.argv[1]);b,ob=parse(sys.argv[2])
same=[k for k in a if a.get(k)==b.get(k)];diff=[k for k in sorted(set(a)|set(b)) if a.get(k)!=b.get(k)]
print('identical sections/tables:',len(same));print('different:',diff)
print('N_OSO entries:',len(oa),len(ob),'differing paths:',sum(1 for x,y in zip(oa,ob) if x!=y))
for x,y in list(zip(oa,ob)):
    if x!=y:print('  A',x);print('  B',y);break
