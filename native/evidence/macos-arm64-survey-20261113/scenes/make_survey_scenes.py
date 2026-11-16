#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the scratch scene documents of the Apple Silicon feature survey (docs/KIT-MACOS-ARM64.md, Part C).

Scratch documents are never committed: either the host refuses them at the base commit, or the pinned result is
undefined (rows 6 and the Integer Math traps of row 2), so a committed scene would break the bitwise check of kit rule 9.
Every document is derived from a committed scene under native/oracle/scenes (read, never written) and keeps its geometry
inline, so it needs no raw files beside it. Ids are survey/<name>.

    python3 make_survey_scenes.py [--output DIR] [--table-builder DIR]

--table-builder names a directory holding an author_surface_nodes.py with integer_table() and pinned_integer_math() (the
phase-2 draft of the Integer Math table builder); without it the two Integer Math documents are not written.
"""
import argparse,copy,importlib.util,json,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
SCENES=REPO/'native/oracle/scenes'


def load(name):return json.loads((SCENES/name).read_text())


def node(document,handle):return next(n for n in document['nodes'] if n['handle']==handle)


def survey(document,name,description):
    out=copy.deepcopy(document)
    out['id']='survey/'+name;out['description']=description
    out['sourceFixture']=f"native/work/macos-arm64-survey-20261113/make_survey_scenes.py (from {document['id']})"
    return out


def integrator(document,**values):node(document,'integrator')['set'].update(values);return document


def both_displacement(source,geometry,name,what):
    """Row 6: the committed scene with displacement/both's Both shader as the geometry's material (DEVICE.md item 87)."""
    document=survey(load(source),name,f'{what} with a Both displacement shader (displacement/both both-smooth): the pinned '
                    'kernel reads an uninitialised SVM stack slot (native/DEVICE.md item 87); survey row 6, never committed')
    both=copy.deepcopy(node(load('displacement/both.json'),'both-smooth'))
    index=next(i for i,n in enumerate(document['nodes']) if n['handle']=='matte')
    document['nodes'].insert(index+1,both)
    node(document,geometry)['set']['used_shaders']=['both-smooth']
    return document


def bake_subdivision():
    """Row 8b: a linear subdivision cage with a corner UV layout, baked (the host refuses it at the base commit)."""
    document=survey(load('subdivision/linear-object-attribute.json'),'bake-subdivision',
                    'Combined bake of the linear subdivision cage of subdivision/linear-object-attribute with a corner UV layout; '
                    'survey row 8b (host_bake.inc refuses "A subdivision surface cannot be baked" at the base commit)')
    cage=node(document,'cage')['subdivision'];xyz=cage['positions']
    xs=xyz[0::3];ys=xyz[1::3];x0,x1,y0,y1=min(xs),max(xs),min(ys),max(ys)
    uv=[]
    for corner in cage['corners']:
        uv+=[round(0.05+0.9*(xs[corner]-x0)/(x1-x0),6),round(0.05+0.9*(ys[corner]-y0)/(y1-y0),6)]
    cage['attributes'].append({'name':'uv','value':'point2','domain':'corner','flags':['standard','subdivision'],'data':uv})
    document['render']['bake']={'object':'cage-object','type':'combined'}
    return document


def bake_flat_normal():
    """Row 8a scratch: the flat Normal bake of the pole sphere (expected host text "Nonfinite bake output", DEVICE.md 149)."""
    document=survey(load('bake/zero-area-smooth-normal.json'),'bake-zero-area-flat-normal',
                    'The flat (not smooth) Normal bake of the pole sphere of bake/zero-area-smooth-normal, whose zero-area pole '
                    'triangles are in the UV layout; survey row 8a')
    ball=node(document,'ball');ball['set']['smooth']=[False]*len(ball['set']['smooth'])
    return document


ZERO_AREA_PROBES=['diffuse','diffuse_direct','glossy','glossy_direct','transmission','transmission_direct']


def bake_zero_area(kind):
    """Row 8a: bake/zero-area-smooth-normal with another bake type (the same pole sphere and room; equal to the committed
    builder author_bake_motion.zero_area() apart from id, description and sourceFixture). These six types fail with
    "Nonfinite bake output" on every backend, so they are not committed scenes; the finite ones are bake/zero-area-<type>."""
    document=survey(load('bake/zero-area-smooth-normal.json'),'bake-zero-area-'+kind.replace('_','-'),
                    f'The {kind} bake of the pole sphere of bake/zero-area-smooth-normal (zero-area pole triangles in the UV layout); survey row 8a')
    document['render']['bake']['type']=kind
    return document


def upscale(name,factor,use_gpu,source='denoise/beckmann-on.json',what='denoise/beckmann-on'):
    """Row 9: Open Image Denoise with a denoiser upscale factor (CPU refused at the base commit; Metal accepted)."""
    document=survey(load(source),name,f'{what} with denoiser_upscale_factor {factor} and denoise_use_gpu {str(use_gpu).lower()}; '
                    'survey row 9')
    return integrator(document,denoise_use_gpu=use_gpu,denoiser_upscale_factor=factor)


def splats():
    """Row 12: points/diffuse with its point cloud rendered as Gaussian splats (refused by the host on every backend)."""
    document=survey(load('points/diffuse.json'),'points-gaussian-splats',
                    'points/diffuse with the point cloud input render_as gaussian_splats; survey row 12 (host refusal)')
    node(document,'points')['set']['render_as']='gaussian_splats'
    return document


def denoise_refusal(name,description,**values):
    """Row 14: denoise/beckmann-on with a denoiser setting the host refuses."""
    return integrator(survey(load('denoise/beckmann-on.json'),name,description+'; survey row 14 (host refusal)'),**values)


def scenes():
    out={}
    out['curves-thick-diffuse-both.json']=both_displacement('curves/thick-diffuse.json','tuft','curves-thick-diffuse-both',
                                                            'curves/thick-diffuse (thick curves)')
    out['points-diffuse-both.json']=both_displacement('points/diffuse.json','points','points-diffuse-both','points/diffuse')
    out['bake-subdivision.json']=bake_subdivision()
    out['bake-zero-area-flat-normal.json']=bake_flat_normal()
    for kind in ZERO_AREA_PROBES:out[f"bake-zero-area-{kind.replace('_','-')}.json"]=bake_zero_area(kind)
    for factor,gpu,suffix in [(1.0,True,'1-gpu'),(2.0,True,'2-gpu'),(4.0,True,'4-gpu'),(1.5,True,'1.5-gpu'),(2.0,False,'2-cpu-oidn')]:
        out[f'denoise-upscale-{suffix}.json']=upscale(f'denoise-upscale-{suffix}',factor,gpu)
    for factor,suffix in [(1.0,'1-gpu-catcher'),(2.0,'2-gpu-catcher')]:
        out[f'denoise-upscale-{suffix}.json']=upscale(f'denoise-upscale-{suffix}',factor,True,'denoise/catcher-point-light-on-noisy.json',
                                                     'denoise/catcher-point-light-on-noisy (a shadow catcher)')
    for factor,suffix in [(1.0,'1-gpu-noisy'),(2.0,'2-gpu-noisy')]:
        document=upscale(f'denoise-upscale-{suffix}',factor,True)
        document['description']+=' (with the noisy combined pass)'
        document['nodes'].append({'handle':'pass-noisy-combined','create':'pass','set':{'type':'combined','name':'Noisy Image','mode':'noisy'}})
        out[f'denoise-upscale-{suffix}.json']=document
    out['points-gaussian-splats.json']=splats()
    out['denoise-dlss.json']=denoise_refusal('denoise-dlss','denoise/beckmann-on with denoiser_type dlss',denoiser_type='dlss')
    out['denoise-passes-128.json']=denoise_refusal('denoise-passes-128','denoise/beckmann-on with denoiser_passes 128',denoiser_passes=128)
    out['denoise-passes-albedo-normal-256.json']=denoise_refusal('denoise-passes-albedo-normal-256',
                                                                'denoise/beckmann-on with denoiser_passes 261 (albedo, normal and bit 256)',
                                                                denoiser_passes=261)
    return out


def table_scenes(directory):
    """Row 2 scratch: the Integer Math cases whose pinned evaluation is undefined, with the table builder of the phase-2 draft
    of author_surface_nodes.py (integer_table with strict=False keeps them). Two documents, so that a crash in one family
    loses nothing of the other: the overflow wraps, and the INT_MIN / -1 family with divide_round by INT_MIN (which divides by
    zero after abs(INT_MIN) wraps); the latter trap (#DE) in an x86-64 idiv (INFERENCE)."""
    path=Path(directory).resolve()/'author_surface_nodes.py'
    sys.path.insert(0,str(path.parent))
    spec=importlib.util.spec_from_file_location('survey_author_surface_nodes',path);m=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    MIN,MAX=m.INT_MIN,m.INT_MAX
    wraps={'add':[(MAX,1),(MIN,-1),(2**30,2**30)],'subtract':[(MIN,1),(MAX,-1),(0,MIN)],
           'multiply':[(65536,65536),(46341,46341),(MAX,2),(MIN,-1)],'multiply_add':[(MAX,1,1),(65536,32768,0)],
           'power':[(2,30),(2,31),(-2,31),(2,32),(3,20),(10,9),(10,10),(MIN,1),(MIN,2),(46341,1)],
           'floored_modulo':[(1,MAX),(-1,MIN)],'absolute':[(MIN,)],'gcd':[(MIN,6),(MIN,0),(MIN,MIN)],
           'lcm':[(65536,65537),(MIN,1),(MIN,6),(MIN,MIN)],'negate':[(MIN,)],'divide_ceil':[(5,MIN),(MIN,-1)],
           'divide_round':[(2**30,3),(MAX,1),(MIN,1)]}
    traps={'divide':[(MIN,-1)],'floored_modulo':[(MIN,-1)],'divide_floor':[(MIN,-1)],'divide_ceil':[(MIN,1)],
           'divide_round':[(5,MIN),(MIN,MIN),(-5,MIN)],'modulo':[(MIN,-1)]}
    out={}
    for name,cases,what in [('integer-math-wraps',wraps,'signed overflow (two\'s-complement wraps on most machines)'),
                            ('integer-math-traps',traps,'INT_MIN / -1 and divide_round by INT_MIN (division by zero after abs(INT_MIN))')]:
        for op,rows in cases.items():
            for case in rows:
                a,b,c=(list(case)+[0,0])[:3]
                try:m.pinned_integer_math(op,a,b,c,strict=True)
                except m.Undefined:continue
                raise SystemExit(f'{op}{case} is defined; it belongs in surface-nodes/integer-math')
        document=m.integer_table('survey/'+name,f'Integer Math cases whose pinned evaluation is undefined in C++ and MSL: {what}; '
                                 'the result in the AOVs D0..D3 and R; survey row 2, never committed',cases,strict=False)
        document['sourceFixture']=f'native/work/macos-arm64-survey-20261113/make_survey_scenes.py (integer_table() of {path.relative_to(REPO)})'
        out[name+'.json']=document
    return out


def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output',type=Path,default=HERE/'scenes');parser.add_argument('--table-builder',type=Path)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    documents=scenes()
    if args.table_builder:documents.update(table_scenes(args.table_builder))
    for name,document in documents.items():
        (args.output/name).write_text(json.dumps(document,indent=1)+'\n');print(args.output/name)


if __name__=='__main__':main()
