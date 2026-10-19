#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Compare linear image data, without hiding differences in a display transform."""
import argparse
import array
import json
import math
import struct
import zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def png(directory):
    # Lossless format conversion of the native driver's already encoded preview.
    ppm=(directory/'render.ppm').read_bytes()
    magic, dimensions, maximum, pixels=ppm.split(b'\n',3)
    if magic!=b'P6' or maximum!=b'255': raise ValueError('Unexpected preview format')
    w,h=map(int,dimensions.split())
    if len(pixels)!=w*h*3: raise ValueError('Truncated preview')
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    rows=b''.join(b'\0'+pixels[y*w*3:(y+1)*w*3] for y in range(h))
    output=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))
    output+=chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
    (directory/'render.png').write_bytes(output)

def verify(directory, reference=None):
    actual=array.array('f'); actual.frombytes((directory/'render.rgba.bin').read_bytes())
    expected=array.array('f'); expected.frombytes((reference or ROOT/'work/reference.rgba.bin').read_bytes())
    metadata=json.loads((directory/'render.json').read_text())
    if len(actual)!=len(expected) or len(actual)!=metadata['width']*metadata['height']*4:
        raise ValueError('Image dimensions do not match')
    nonfinite=sum(not math.isfinite(x) for x in actual)
    differences=[abs(a-b) for a,b in zip(actual,expected)]
    failures=[i for i,(d,e) in enumerate(zip(differences,expected)) if not math.isfinite(d) or d>5e-5+2e-4*abs(e)]
    alpha_errors=sum(actual[i]!=1 for i in range(3,len(actual),4))
    report={'passed':not failures and not alpha_errors,'reference':'Original Cycles CPU BVH/triangle functions; prototype camera and Lambert lighting',
        'absoluteTolerance':5e-5,'relativeTolerance':2e-4,'components':len(actual),
        'mismatchedComponents':len(failures),'nonfiniteComponents':nonfinite,'alphaMismatches':alpha_errors,
        'maximumAbsoluteError':max(differences),'rootMeanSquareError':math.sqrt(sum(d*d for d in differences)/len(differences)),
        'firstErrors':[{'component':i,'actual':actual[i],'expected':expected[i]} for i in failures[:10]]}
    (directory/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    png(directory)
    print(directory.name,json.dumps(report))
    return report['passed']

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('directories',type=Path,nargs='+')
    args=parser.parse_args(); results=[verify(d) for d in args.directories]
    raise SystemExit(0 if all(results) else 1)
