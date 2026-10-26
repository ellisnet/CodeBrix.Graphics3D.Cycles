// Scratch micro-repro of the SHADER_SMOOTH_NORMAL branch of surface_setup_hit
// (native/shaders/wavefront_shader_data.wgsl), with the real float.wgsl helpers
// prepended. Input words in an atomic heap like the product.
@group(0) @binding(0) var<storage,read_write> heap:array<atomic<u32>>;
@group(0) @binding(1) var<storage,read_write> outp:array<f32>;
fn load_word(address:u32)->u32{return atomicLoad(&heap[address/4u]);}
fn surface_normalize(v:vec3<f32>)->vec3<f32>{return rounded_safe_normalize(v);}
fn surface_decode_normal(value:u32)->vec3<f32>{
    let raw=vec2<f32>(f32(value&65535u),f32(value>>16u))*(2.0/65535.0);
    let xy=vec2<f32>(rounded_add(raw.x,-1.0),rounded_add(raw.y,-1.0));
    let z=rounded_add(rounded_add(1.0,-abs(xy.x)),-abs(xy.y));let t=max(-z,0.0);
    let shift=select(vec2<f32>(t),vec2<f32>(-t),xy>vec2<f32>(0));
    return surface_normalize(vec3<f32>(rounded_add(xy.x,shift.x),rounded_add(xy.y,shift.y),z));
}
// Case layout: 3 packed normals, u bits, v bits (5 words) after a count word.
// Output per case: 16 floats = n0, n1, n2, interpolated, weight, pad, pad, pad.
@compute @workgroup_size(64) fn smooth_normal(@builtin(global_invocation_id) id:vec3<u32>){
    let i=id.x;if(i>=load_word(0u)){return;}
    let base=4u+i*20u;
    var normals:array<vec3<f32>,3>;
    for(var j=0u;j<3u;j++){
        normals[j]=surface_decode_normal(load_word(base+j*4u));
    }
    let u=bitcast<f32>(load_word(base+12u));let v=bitcast<f32>(load_word(base+16u));
    let weight=rounded_add(rounded_add(1.0,-u),-v);
    let interpolated=surface_normalize(rounded_add3(rounded_add3(weight*normals[0],u*normals[1]),v*normals[2]));
    let o=i*16u;
    for(var j=0u;j<3u;j++){outp[o+j*3u]=normals[j].x;outp[o+j*3u+1u]=normals[j].y;outp[o+j*3u+2u]=normals[j].z;}
    outp[o+9u]=interpolated.x;outp[o+10u]=interpolated.y;outp[o+11u]=interpolated.z;outp[o+12u]=weight;
}
