#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Review the osx-arm64 host build and exercise its CPU and Metal lifecycles.

    python3 native/tools/macos_host_gate.py --output native/work/macos-validation-v1

Same-host CPU films must match the independent native Session and image
references bit for bit, through the C API and the thin .NET wrapper alike.
This profile reads no image files, so the image reference and the reload
fixtures run their 256 memory-image cases and verify that texture filenames are
rejected; the four file cases apply only to builds that read files. Metal films
are compared with the CPU references using the practical criterion and reported;
disagreements are findings, never adjusted.
"""
import argparse,ctypes,json,math,os,re,struct,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'tools/dependencies'))
from macos_checks import linked,tool
import hashlib
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
BASE=REPO/'native/work/macos/osx-arm64';FLOATS=32*24*4


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 host=BASE/'host';build=host/'build';prefix=BASE/'dependencies/install';out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 state=dict(complete=False,phase='binary-review',rid='osx-arm64');save(out/'status.json',state)
 results={};comparisons=[];commands=[]
 env=dict(os.environ)
 for name in ['OCIO','DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','CYCLES_METALRT','CYCLES_METAL_SPECIALIZATION_LEVEL']:env.pop(name,None)
 def call(command,name,timeout=900,extra=None):
  commands.append(dict(command=list(map(str,command)),environment=extra or {},timeoutSeconds=timeout));save(out/'commands.json',commands)
  state['phase']=name;save(out/'status.json',state)
  with (out/(name+'.stdout.txt')).open('w') as stdout,(out/(name+'.stderr.txt')).open('w') as stderr:
   subprocess.run(list(map(str,command)),stdout=stdout,stderr=stderr,check=True,timeout=timeout,env={**env,**(extra or {})})
  return (out/(name+'.stdout.txt')).read_text()
 def report(binary,arguments,name,timeout=900,extra=None):
  rows=[json.loads(line) for line in call([build/'bin'/binary,*arguments],name,timeout,extra).splitlines() if line.strip()]
  assert rows and all(row['passed'] for row in rows),rows;results[name]=rows;save(out/'results.json',results);return rows
 def compare(actual,ref,kind,bitwise=False,practical=True):
  x,y=actual.read_bytes(),ref.read_bytes();v,w=struct.unpack(f'<{FLOATS}f',x),struct.unpack(f'<{FLOATS}f',y)
  assert all(math.isfinite(f) for f in v+w)
  errors=[abs(i-j) for i,j in zip(v,w)]
  comparisons.append(dict(film=str(actual),reference=str(ref),kind=kind,filmSha256=sha(actual),bitwiseEqual=x==y,bitwiseRequired=bitwise,
   practicalRequired=practical,maximumAbsoluteError=max(errors),practicalFailures=sum(e>5e-5+2e-4*abs(b) for e,b in zip(errors,w)),
   strictDifferences=sum(e>1e-7+1e-7*abs(b) for e,b in zip(errors,w))))
 try:
  status=json.loads((host/'status.json').read_text());assert status['complete'] and status['passed']
  pins={str(build/name):digest for name,digest in status['artifacts'].items()}
  dependency=json.loads((prefix/'codebrix-osx-arm64-build.json').read_text())
  pins.update({str(prefix/name):digest for name,digest in dependency['libraries'].items()})
  assert all(sha(Path(path))==digest for path,digest in pins.items())
  library=build/'lib/libcodebrix_cycles_host.dylib'
  exported={line.split()[-1].lstrip('_') for line in tool('nm','-gU',str(library)).splitlines() if ' T ' in line}
  expected=set(re.findall(r'\b(cbc_host_\w+)\(',(REPO/'native/host/cycles_host.h').read_text()));assert exported==expected and len(exported)==65
  closure={};review=[]
  for path in [library,*[build/'bin'/name for name in ['codebrix_host_cpu_test','codebrix_host_gpu_test','codebrix_host_session_reference']]]:
   names=linked(path)
   for name in names:assert name.startswith(('@rpath/','/usr/lib/','/System/Library/')),(path,name)
   review.append(dict(path=str(path),sha256=sha(path),linked=names,minimumOs=re.findall(r'minos (\S+)',tool('otool','-l',str(path))),
    signature=tool('codesign','-dv',str(path)).splitlines()))
   subprocess.run(['codesign','--verify',str(path)],check=True)
  frameworks=sorted({n for r in review for n in r['linked'] if n.startswith('/System/')})
  assert any('/Metal.framework/' in f for f in frameworks),frameworks
  codecs=[n for n in status['dependencyClosure'] if re.match(r'lib(jpeg|png|tiff|webp|gif|heif|raw|openjp|avif|jxl)',n)];assert not codecs,codecs
  for kind in ['session','session64','image']:
   link=(build/('codebrix_host_'+kind+'_reference.map')).read_text()
   assert 'codebrix_host_reference_color.dir' in link and 'colorspace.cpp.o' in link
  save(out/'binary-review.json',dict(passed=True,exports=sorted(exported),binaries=review,frameworks=frameworks,dependencyClosure=status['dependencyClosure'],
   scope='osx-arm64 exports, Mach-O minimum OS, ad-hoc signatures and load closure; reference executables link the original color implementation'))

  # Contract checks through the public API, without any test executable.
  api=ctypes.CDLL(str(library));config=(ctypes.c_uint32*8)(32,1,1,1,0,0,0,0);handle=ctypes.c_uint64(0)
  webgpu=api.cbc_host_create(config,ctypes.byref(handle))
  config[2]=2;assert api.cbc_host_create(config,ctypes.byref(handle))==0
  node=ctypes.c_uint64(0);shader=ctypes.c_uint64(0);assert api.cbc_host_create_node(handle,b'shader',ctypes.byref(shader))==0
  assert api.cbc_host_shader_node(handle,shader,b'image_texture',ctypes.byref(node))==0
  rejected=api.cbc_host_set_string(handle,node,b'filename',b'/tmp/texture.png');cleared=api.cbc_host_set_string(handle,node,b'filename',b'')
  # UDIM tiles (PROVISIONAL cbc_host_image_bind_tiles): refused by name on an Environment Texture node (2), below tile
  # 1001, twice the same tile and across image formats (1); accepted on an Image Texture node, and a render with the
  # tiles bound succeeds.
  def image(fmt,data):
   created=ctypes.c_uint64(0);assert api.cbc_host_image_create(handle,(ctypes.c_uint32*8)(32,1,1,fmt,0,0,0,0),data,ctypes.c_uint64(len(data)),ctypes.byref(created))==0
   return created.value
  float4=image(2,bytes(struct.pack('<4f',.2,.4,.6,1)));byte4=image(6,bytes([10,20,30,255]));environment=ctypes.c_uint64(0)
  assert api.cbc_host_shader_node(handle,shader,b'environment_texture',ctypes.byref(environment))==0
  def tiles(target,numbers,images):
   return api.cbc_host_image_bind_tiles(handle,target,(ctypes.c_uint32*len(numbers))(*numbers),(ctypes.c_uint64*len(images))(*images),ctypes.c_uint64(len(numbers)))
  udim=dict(environmentStatus=tiles(environment,[1001],[float4]),tile1000Status=tiles(node,[1000],[float4]),duplicateStatus=tiles(node,[1001,1001],[float4,float4]),
   mixedFormatStatus=tiles(node,[1001,1002],[float4,byte4]),boundStatus=tiles(node,[1001,1003],[float4,float4]))
  # Texture limit (PROVISIONAL cbc_host_texture_limit): above INT_MAX refused (1), 0 and 1024 accepted before a render;
  # after the render below, which loads the tiles' images, the same value is accepted (0) and another refused (5).
  limit=lambda size:api.cbc_host_texture_limit(handle,ctypes.c_uint32(size))
  texture=dict(aboveIntMaxStatus=limit(0x80000000),zeroStatus=limit(0),setStatus=limit(1024))
  pixels=(ctypes.c_float*(8*8*4))();udim['renderStatus']=api.cbc_host_render(handle,(ctypes.c_uint32*8)(32,8,8,1,0,0,0,0),pixels,ctypes.c_uint64(8*8*4))
  assert all(math.isfinite(v) for v in pixels)
  texture.update(sameAfterRenderStatus=limit(1024),changedAfterRenderStatus=limit(512))
  # BVH spatial splits (PROVISIONAL cbc_host_bvh_spatial_split): 2 refused (1), 1 and 0 accepted after a render, and a
  # render with them on (every BVH built again) succeeds with a finite film.
  split=lambda enabled:api.cbc_host_bvh_spatial_split(handle,ctypes.c_uint32(enabled))
  spatial=dict(twoStatus=split(2),onStatus=split(1))
  spatial['renderStatus']=api.cbc_host_render(handle,(ctypes.c_uint32*8)(32,8,8,1,0,0,0,0),pixels,ctypes.c_uint64(8*8*4))
  assert all(math.isfinite(v) for v in pixels)
  spatial['offStatus']=split(0)
  # Tiled rendering (PROVISIONAL cbc_host_tile_size): sizes Cycles would change refused (1), 8 and 256 accepted, a 32 x 8
  # render in tiles of 8 (4 tiles) succeeds with a finite film, its 4 tiles kept in memory and one frame read back (the
  # device report), and 0 restores one tile.
  tile=lambda size:api.cbc_host_tile_size(handle,ctypes.c_uint32(size))
  tiling=dict(sevenStatus=tile(7),status129=tile(129),status8193=tile(8193),status256=tile(256),eightStatus=tile(8))
  wide=(ctypes.c_float*(32*8*4))();tiling['renderStatus']=api.cbc_host_render(handle,(ctypes.c_uint32*8)(32,32,8,1,0,0,0,0),wide,ctypes.c_uint64(32*8*4))
  assert all(math.isfinite(v) for v in wide)
  required=ctypes.c_uint64(0);assert api.cbc_host_device_report_json(handle,None,ctypes.c_uint64(0),ctypes.byref(required))==6
  text=ctypes.create_string_buffer(required.value);assert api.cbc_host_device_report_json(handle,text,required,ctypes.byref(required))==0
  tiling['deviceReport']=json.loads(text.value.decode())['tiledRendering'];tiling['zeroStatus']=tile(0)
  # Tiled images (PROVISIONAL cbc_host_image_create_tiled): without a reader refused (1) and released at once; created
  # (0), not released while the handle is bound to the Environment Texture node, released once by the destroy.
  READ=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint64)
  RELEASE=ctypes.CFUNCTYPE(None,ctypes.c_void_p);released=[]
  read_callback=READ(lambda *arguments:1);release_callback=RELEASE(lambda user:released.append(user))
  api.cbc_host_image_create_tiled.argtypes=[ctypes.c_uint64,ctypes.c_void_p,READ,RELEASE,ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint64)]
  tiled_image=ctypes.c_uint64(0);tiled_config=(ctypes.c_uint32*8)(32,256,256,6,0,0,0,0)
  tiled=dict(noReaderStatus=api.cbc_host_image_create_tiled(handle,tiled_config,READ(),release_callback,ctypes.c_void_p(11),ctypes.byref(tiled_image)))
  tiled['noReaderReleases']=released.count(11)
  tiled['createStatus']=api.cbc_host_image_create_tiled(handle,tiled_config,read_callback,release_callback,ctypes.c_void_p(12),ctypes.byref(tiled_image))
  tiled['bindStatus']=api.cbc_host_image_bind(handle,environment,tiled_image)
  tiled['releasesBeforeDestroy']=released.count(12)
  # Tile sizes (PROVISIONAL CBCImageConfig.tile_size): on this CPU host a power of two from 64 to 65536; 32, 96 and 131072
  # refused (1) and released at once, 128 and 65536 created (0) and released once by the destroy; a plain image with a
  # tile size refused (1).
  def sized(size,user):
   created=ctypes.c_uint64(0);return api.cbc_host_image_create_tiled(handle,(ctypes.c_uint32*8)(32,256,256,6,size,0,0,0),read_callback,release_callback,ctypes.c_void_p(user),ctypes.byref(created))
  tiled.update(tileSize32Status=sized(32,13),tileSize96Status=sized(96,14),tileSize131072Status=sized(131072,15),tileSize128Status=sized(128,16),
   tileSize65536Status=sized(65536,17))
  tiled['refusedSizeReleases']=[released.count(user) for user in (13,14,15)];tiled['acceptedSizeReleasesBeforeDestroy']=[released.count(user) for user in (16,17)]
  plain=ctypes.c_uint64(0);tiled['plainTileSizeStatus']=api.cbc_host_image_create(handle,(ctypes.c_uint32*8)(32,1,1,6,64,0,0,0),bytes([1,2,3,255]),ctypes.c_uint64(4),ctypes.byref(plain))
  assert api.cbc_host_destroy(handle)==0
  tiled['releasesAfterDestroy']=released.count(12);tiled['acceptedSizeReleasesAfterDestroy']=[released.count(user) for user in (16,17)]
  # Subdivision-surface bakes (PROVISIONAL), on a CPU host of their own: a linear cage of a quad and a pentagon with a corner
  # UV layout among its subdivision attributes. With Cycles' default pixel-space dicing the bake map and the bake are refused
  # (2); diced in object space the map renders nothing and covers texels with indices into Cycles' tessellation (far more
  # triangles than the cage's two faces), and a Combined bake succeeds with a finite film (0); Catmull-Clark is refused (2).
  class Bake(ctypes.Structure):_fields_=[('size',ctypes.c_uint32),('width',ctypes.c_uint32),('height',ctypes.c_uint32),('samples',ctypes.c_uint32),
   ('type',ctypes.c_uint32),('flags',ctypes.c_uint32),('reserved',ctypes.c_uint32*2),('object',ctypes.c_uint64),('uv',ctypes.c_char_p),('aov',ctypes.c_char_p),
   ('reserved2',ctypes.c_uint64)]
  subd=ctypes.c_uint64(0);assert api.cbc_host_create(config,ctypes.byref(subd))==0
  cage=ctypes.c_uint64(0);target=ctypes.c_uint64(0);assert api.cbc_host_create_node(subd,b'mesh',ctypes.byref(cage))==0
  xyz=[-1.1,-.6,.3,-.2,-.7,.1,-.1,.5,-.2,-1.,.5,0.,.2,-.5,0.,1.,-.6,.2,1.1,.2,.1,.7,.7,-.1,.1,.3,.05];corners=list(range(9))
  assert api.cbc_host_subd_mesh(subd,cage,(ctypes.c_float*27)(*xyz),ctypes.c_uint64(9),(ctypes.c_uint32*2)(4,5),ctypes.c_uint64(2),(ctypes.c_uint32*9)(*corners),ctypes.c_uint64(9))==0
  uv=[c for i in corners for c in ((xyz[3*i]+1.2)/2.4,(xyz[3*i+1]+.8)/1.6)]
  assert api.cbc_host_attribute_floats(subd,cage,b'uv',(ctypes.c_uint32*8)(32,10,4,3,0,0,0,0),(ctypes.c_float*18)(*uv),ctypes.c_uint64(18))==0
  assert api.cbc_host_set_string(subd,cage,b'subdivision_type',b'linear')==0 and api.cbc_host_set_floats(subd,cage,b'subd_dicing_rate',(ctypes.c_float*1)(.1),ctypes.c_uint64(1))==0
  assert api.cbc_host_create_node(subd,b'object',ctypes.byref(target))==0 and api.cbc_host_set_nodes(subd,target,b'geometry',(ctypes.c_uint64*1)(cage.value),ctypes.c_uint64(1))==0
  bake=Bake(64,16,16,1,1,0,(ctypes.c_uint32*2)(),target.value,None,None,0);required=ctypes.c_uint64(0);components=ctypes.c_uint32(0)
  triangles=(ctypes.c_int32*256)();film=(ctypes.c_float*1024)()
  bake_map=lambda:api.cbc_host_bake_map(subd,ctypes.byref(bake),triangles,None,ctypes.c_uint64(256),ctypes.byref(required))
  bake_film=lambda:api.cbc_host_bake(subd,ctypes.byref(bake),film,ctypes.c_uint64(1024),ctypes.byref(required),ctypes.byref(components))
  subdivision=dict(pixelSpaceMapStatus=bake_map(),pixelSpaceBakeStatus=bake_film())
  assert api.cbc_host_set_string(subd,cage,b'subd_adaptive_space',b'object')==0
  subdivision.update(mapStatus=bake_map(),coveredTexels=sum(1 for i in triangles if i>=0)>128,trianglesBeyondTheCage=max(triangles)>=2,bakeStatus=bake_film())
  subdivision['bakeFinite']=all(math.isfinite(v) for v in film)
  assert api.cbc_host_set_string(subd,cage,b'subdivision_type',b'catmull_clark')==0
  subdivision['catmullClarkStatus']=bake_map();assert api.cbc_host_destroy(subd)==0
  contract=dict(webgpuCreateStatus=webgpu,filenameSetStatus=rejected,emptyFilenameStatus=cleared,udimTiles=udim,textureLimit=texture,bvhSpatialSplit=spatial,
   tiledImages=tiled,tileSize=tiling,subdivisionBakes=subdivision)
  assert contract==dict(webgpuCreateStatus=2,filenameSetStatus=2,emptyFilenameStatus=0,udimTiles=dict(environmentStatus=2,tile1000Status=1,duplicateStatus=1,
   mixedFormatStatus=1,boundStatus=0,renderStatus=0),textureLimit=dict(aboveIntMaxStatus=1,zeroStatus=0,setStatus=0,sameAfterRenderStatus=0,
   changedAfterRenderStatus=5),bvhSpatialSplit=dict(twoStatus=1,onStatus=0,renderStatus=0,offStatus=0),tiledImages=dict(noReaderStatus=1,
   noReaderReleases=1,createStatus=0,bindStatus=0,releasesBeforeDestroy=0,tileSize32Status=1,tileSize96Status=1,tileSize131072Status=1,
   tileSize128Status=0,tileSize65536Status=0,refusedSizeReleases=[1,1,1],acceptedSizeReleasesBeforeDestroy=[0,0],plainTileSizeStatus=1,
   releasesAfterDestroy=1,acceptedSizeReleasesAfterDestroy=[1,1]),tileSize=dict(sevenStatus=1,status129=1,
   status8193=1,status256=0,eightStatus=0,renderStatus=0,deviceReport=dict(tileSize=8,tilesKeptInMemory=4,framesReadBack=1,tilesHeld=0),zeroStatus=0),
   subdivisionBakes=dict(pixelSpaceMapStatus=2,pixelSpaceBakeStatus=2,mapStatus=0,coveredTexels=True,trianglesBeyondTheCage=True,bakeStatus=0,bakeFinite=True,
   catmullClarkStatus=2)),contract
  results['contract']=contract;save(out/'results.json',results)

  for name,kind in [('cpu','session'),('cpu64','session64')]:
   folder=out/name;folder.mkdir();rows=report('codebrix_host_'+kind+'_reference',['CPU',folder/'run'],kind+'-reference')
   assert rows[0]['actualSession'] and rows[0]['renders']==3 and rows[0]['device']=='CPU'
  # cpu-progressive: the same lifecycle on hosts created with CBC_HOST_PROGRESSIVE_RESULTS (PROVISIONAL); every render
  # sends film events, its last film equals the returned film, and the films equal the Session references bit for bit.
  # cpu-tiles: the same lifecycle in tiles of 8 (PROVISIONAL cbc_host_tile_size; 12 tiles per film, kept in memory); the
  # films equal the Session references bit for bit and no tile file appears.
  tile_files=lambda:sorted(str(p) for folder in [REPO,out,Path.cwd()] for p in folder.glob('cycles-tile-buffer*'))
  tile_files_before=tile_files()
  for mode,flag in [('cpu',[]),('cpu-exit',['--exit-live']),('cpu-progressive',['--progressive']),('cpu-tiles',['--tiles'])]:
   folder=out/(mode+'-consumer');folder.mkdir();rows=report('codebrix_host_cpu_test',[folder/'run',*flag],mode)
   if mode=='cpu-progressive':assert rows[0]['progressiveResults'] and rows[0]['filmEvents']>=6,rows
   if mode=='cpu-tiles':assert rows[0]['tiledRendering'] and rows[0]['framesReadBack']==6 and rows[0]['tilesKeptInMemory']>=72,rows
   for i in range(3):compare(folder/f'run.{i}.rgba.bin',out/'cpu'/f'run.{i}.rgba.bin','same-host CPU lifecycle',bitwise=True)
  # cpu-tiled: tiled images (PROVISIONAL cbc_host_image_create_tiled) served by a C reader render the plain image's film
  # bit for bit; a failing read fails by name and is read again; release runs once per image. cpu-tiled-exit: a host
  # with a tiled image live at process exit, whose release callback prints its row from the exit hook.
  folder=out/'cpu-tiled-consumer';folder.mkdir();rows=report('codebrix_host_cpu_test',[folder/'run','--tiled'],'cpu-tiled')
  assert rows[0]['tiledImages'] and rows[0]['tiledEqualsPlainBitwise'] and rows[0]['reads']>0,rows
  compare(folder/'run.tiled.rgba.bin',folder/'run.plain.rgba.bin','same-host CPU tiled image vs plain image',bitwise=True)
  rows=report('codebrix_host_cpu_test',[folder/'exit','--tiled-exit'],'cpu-tiled-exit')
  assert len(rows)==2 and rows[0].get('tiledImageLiveAtExit') and rows[1].get('releasedAtProcessExit') and rows[1]['releases']==1,rows
  # Thin .NET wrapper: same films as the native Session, bit for bit.
  managed=out/'managed-bin'
  subprocess.run(['dotnet','build',str(REPO/'tools/hosttests/HostLifecycle/HostLifecycle.csproj'),'-c','Release','-o',str(managed)],check=True,capture_output=True)
  folder=out/'managed-run'
  rows=[json.loads(line) for line in call(['dotnet',managed/'HostLifecycle.dll',out/'cpu',folder],'managed',900,
   {'DYLD_LIBRARY_PATH':str(build/'lib')}).splitlines() if line.strip().startswith('{')]
  assert rows and rows[-1]['passed'] and rows[-1]['cpuReferenceBitwiseEqual'],rows;results['managed']=rows;save(out/'results.json',results)
  for i in range(3):compare(folder/f'run.{i}.rgba.bin',out/'cpu'/f'run.{i}.rgba.bin','managed CPU lifecycle',bitwise=True)
  # Metal: shipped settings, then MetalRT forced on (the default path on M3+), then progressive results and tiled rendering
  # (PROVISIONAL).
  for mode,extra,flag in [('metal',{},[]),('metal-metalrt',{'CYCLES_METALRT':'1'},[]),('metal-progressive',{},['--progressive']),('metal-tiles',{},['--tiles'])]:
   folder=out/(mode+'-consumer');folder.mkdir()
   rows=report('codebrix_host_gpu_test',[folder/'run',*flag],mode,1800,extra);assert rows[0].get('explicitMetal'),rows
   if flag==['--progressive']:assert rows[0]['progressiveResults'] and rows[0]['filmEvents']>=6,rows
   if flag==['--tiles']:assert rows[0]['tiledRendering'] and rows[0]['framesReadBack']==6 and rows[0]['tilesKeptInMemory']>=72,rows
   for i in range(3):compare(folder/f'run.{i}.rgba.bin',out/'cpu'/f'run.{i}.rgba.bin',f'{mode} vs same-host CPU Session reference')
  # Image oracle (memory images only in this profile), one per OCIO configuration.
  refs=out/'references';refs.mkdir();cases=256
  for variant in ['a','b','c']:
   rows=report('codebrix_host_image_reference',[REPO/'native/tests/host/configs'/f'{variant}.ocio',refs/variant/'output'],'image-reference-'+variant,1800)
   assert rows[0]['originalNativeCpuSession'] and rows[0]['cases']==cases and rows[0]['fileAndRebindCases']==0,rows
  reload=out/'reload'
  for i in range(6):(reload/f'round.{i}').mkdir(parents=True)
  rows=report('codebrix_host_reload_test',[refs,reload,'6'],'reload',2400);assert len(rows)==7 and rows[-1]['films']==6*cases,rows
  for i in range(6):
   for j in range(cases):compare(reload/f'round.{i}/case.{j}.rgba.bin',refs/chr(ord('a')+i%3)/'output'/f'case.{j}.rgba.bin','same-host color reload',bitwise=True)
  # The same image modes and colour reloads through the thin .NET wrapper.
  for project in ['ImageModes','ColorReload']:
   subprocess.run(['dotnet','build',str(REPO/'tools/hosttests'/project/(project+'.csproj')),'-c','Release','-o',str(out/'managed-bin'/project),
    '--configfile',str(REPO/'tools/hosttests/NuGet.Config')],check=True,capture_output=True)
  managed_reload=out/'managed-reload'
  for i in range(6):(managed_reload/f'round.{i}').mkdir(parents=True)
  rows=[json.loads(line) for line in call(['dotnet',out/'managed-bin/ColorReload/ColorReload.dll',out/'managed-bin/ImageModes/ImageModes.dll',refs,managed_reload],
   'managed-reload',2400,{'DYLD_LIBRARY_PATH':str(build/'lib')}).splitlines() if line.strip().startswith('{')]
  assert rows and rows[-1]['passed'] and rows[-1]['sameManagedProcess'] and rows[-1]['films']==6*cases,rows;results['managed-reload']=rows;save(out/'results.json',results)
  for i in range(6):
   for j in range(cases):compare(managed_reload/f'round.{i}/case.{j}.rgba.bin',refs/chr(ord('a')+i%3)/'output'/f'case.{j}.rgba.bin','managed color reload',bitwise=True)
  stress_input=out/'stress-input';stress_input.mkdir();stress_output=out/'stress-output';stress_output.mkdir()
  for variant in ['a','b']:(stress_input/f'{variant}.ocio').write_bytes((REPO/'native/tests/host/configs'/f'{variant}.ocio').read_bytes())
  (stress_input/'input.5.bin').write_bytes((refs/'a/output/input.5.bin').read_bytes())
  rows=report('codebrix_host_concurrency_test',[stress_input,stress_output],'concurrency',1200)
  assert all(rows[0][key]==value for key,value in dict(concurrentHostLifetimes=192,destroyDuringRender=12,racingReaders=48,guardedConfigurationRejections=256,hostLiveConfigurationRejections=192).items()),rows
  for name in ['guarded','after-invalid']:compare(stress_output/(name+'.rgba.bin'),refs/'a/output/case.177.rgba.bin','same-host concurrent lifetime',bitwise=True)
  assert all(sha(Path(path))==digest for path,digest in pins.items())
  assert tile_files()==tile_files_before,tile_files()
  required=[r for r in comparisons if (r['bitwiseRequired'] and not r['bitwiseEqual'])]
  metal=[r for r in comparisons if r['kind'].startswith('metal')]
  state.update(complete=True,phase='complete',passed=not required,filmsCompared=len(comparisons),sameHostBitwiseFailures=len(required),
   metalPracticalFailures=sum(r['practicalFailures'] for r in metal),metalStrictDifferences=sum(r['strictDifferences'] for r in metal),
   metalMaximumAbsoluteError=max(r['maximumAbsoluteError'] for r in metal),
   notApplicable=dict(fileAndRebindCases='4 per round: this profile reads no image files; texture filenames are verified as rejected instead'),
   inputsUnchanged=True)
  print(json.dumps(state),flush=True)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(out/'comparisons.json',comparisons);save(out/'status.json',state)
 return 0 if state['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
