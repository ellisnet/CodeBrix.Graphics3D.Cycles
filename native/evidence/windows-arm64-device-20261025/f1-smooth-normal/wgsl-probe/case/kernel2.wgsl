// Scratch micro-repro v2: the SHADER_SMOOTH_NORMAL branch of surface_setup_hit with
// its real control flow (table_at + fail(), triangle_frame_index, early return from
// the loop filling a function-scope array, struct return). Heap layout (words):
//  0: case count   1: error word (fail)   2: normal table base (bytes)   3: normal count
//  4..: cases, 8 words each: vi0 vi1 vi2 offset(i32) stride u v pad
// Output per case: 8 floats = N.xyz, error, Ng-check, pad.
@group(0) @binding(0) var<storage,read_write> heap:array<atomic<u32>>;
@group(0) @binding(1) var<storage,read_write> outp:array<f32>;
const NONE:u32=0xffffffffu;
fn load_word(address:u32)->u32{return atomicLoad(&heap[address/4u]);}
fn fail(){atomicOr(&heap[1],1u);}
fn table_count_n()->u32{return load_word(12u);}
fn table_at_n(item:u32)->u32{
    if(item>=table_count_n()){fail();return 0u;}
    return load_word(8u)+item*4u;
}
fn triangle_frame_index(index:u32,offset:i32,frame:u32,stride:u32,count:u32)->u32{
    if(index>0x7fffffffu||(offset>0&&index>0x7fffffffu-u32(offset))){fail();return NONE;}
    let center=bitcast<i32>(index)+offset;
    if(center<0||u32(center)>=count){fail();return NONE;}
    if(stride==0u||frame>(count-1u-u32(center))/stride){fail();return NONE;}
    return u32(center)+frame*stride;
}
fn surface_normalize(v:vec3<f32>)->vec3<f32>{return rounded_safe_normalize(v);}
fn surface_decode_normal(value:u32)->vec3<f32>{
    let raw=vec2<f32>(f32(value&65535u),f32(value>>16u))*(2.0/65535.0);
    let xy=vec2<f32>(rounded_add(raw.x,-1.0),rounded_add(raw.y,-1.0));
    let z=rounded_add(rounded_add(1.0,-abs(xy.x)),-abs(xy.y));let t=max(-z,0.0);
    let shift=select(vec2<f32>(t),vec2<f32>(-t),xy>vec2<f32>(0));
    return surface_normalize(vec3<f32>(rounded_add(xy.x,shift.x),rounded_add(xy.y,shift.y),z));
}
struct TriangleTime {a:u32, b:u32, t:f32,}
struct SD {N:vec3<f32>, Ng:vec3<f32>, u:f32, v:f32, error:u32, shader:u32,}
fn sd_failure(sd:ptr<function,SD>){(*sd).error=1u;fail();}
fn setup(c:u32)->SD{
    var sd:SD;
    sd.Ng=vec3<f32>(0.0,1.0,0.0);sd.N=sd.Ng;sd.shader=0x80000000u;
    sd.u=bitcast<f32>(load_word(c+20u));sd.v=bitcast<f32>(load_word(c+24u));
    let vi=vec3<u32>(load_word(c),load_word(c+4u),load_word(c+8u));
    if((sd.shader&0x80000000u)!=0u){
        var ni=vi;
        let offset=bitcast<i32>(load_word(c+12u));
        let nc=table_count_n();var normals:array<vec3<f32>,3>;
        var frame:TriangleTime;let stride=load_word(c+16u);
        for(var j=0u;j<3u;j++){
            let at=triangle_frame_index(ni[j],offset,frame.a,stride,nc);
            if(at==NONE){sd_failure(&sd);return sd;}
            normals[j]=surface_decode_normal(load_word(table_at_n(at)));
        }
        let weight=rounded_add(rounded_add(1.0,-sd.u),-sd.v);
        let interpolated=surface_normalize(rounded_add3(rounded_add3(weight*normals[0],sd.u*normals[1]),sd.v*normals[2]));
        if(any(interpolated!=vec3<f32>(0))){sd.N=interpolated;}
    }
    return sd;
}
@compute @workgroup_size(64) fn smooth_branch(@builtin(global_invocation_id) id:vec3<u32>){
    let i=id.x;if(i>=load_word(0u)){return;}
    let sd=setup(16u+i*32u);
    let o=i*8u;
    outp[o]=sd.N.x;outp[o+1u]=sd.N.y;outp[o+2u]=sd.N.z;outp[o+3u]=f32(sd.error);outp[o+4u]=dot(sd.N,sd.Ng);
}
