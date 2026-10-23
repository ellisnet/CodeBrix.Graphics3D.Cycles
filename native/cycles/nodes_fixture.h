// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Real native shader graphs (ShaderGraph -> SVM compiler) for the texture, colour
// and utility nodes: procedural noise in every dimension and fractal type, white
// noise, wave, magic, brick, mapping/texture mapping, map range, every mix blend
// mode, HSV, colour separate/combine, ramps/curves, Fresnel/layer weight, vector
// rotate, blackbody, normal, wavelength, wireframe, bump and Gabor. Inputs vary per path (world position) so nothing
// constant-folds; the primary output drives the diffuse colour, the secondary
// scalar the emission strength.
// The render variant has 68 triangles (lane L, 2026-10-22; M2-M4 kept 60): graph
// 60 (wavelength) takes the slot of a repeated noise variant, the two Gabor graphs
// (65, 66) and six repeated continuous graphs follow M2's 60 slots, and the
// wireframe graphs stay out (a step function of the hit point, which the
// independent CPU image reproduces only to the last bits).
#ifdef CBC_NODES_RENDER
static constexpr unsigned NODES_GRAPH_COUNT=68;
#else
static constexpr unsigned NODES_GRAPH_COUNT=67;
#endif
// The render gate (CBC_NODES_RENDER) also compares an independent CPU image of
// whole paths, where hit points legitimately differ in the last bits. There the
// graphs stay continuous and well conditioned: no position quantization, white
// noise and stepped/constant variants are replaced, noise detail is at most 3,
// and the camera-visible triangles are ordered to show every node family.
static ShaderOutput *nodes_material(ShaderGraph &g,unsigned test){
#ifdef CBC_NODES_RENDER
  constexpr bool render=true;
  static constexpr unsigned order[NODES_GRAPH_COUNT]={0,52,24,30,36,40,53,60,25,31,37,43,54,2,26,32,38,44,55,3,28,33,39,45,56,48,49,50,51,57,46,47,41,42,58,34,35,29,27,59,
    4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,1,65,66,31,37,32,38,33,39};
  test=order[test];if(test>=20&&test<24)test-=16;
#else
  constexpr bool render=false;
#endif
  auto geo=g.create_node<GeometryNode>();ShaderOutput *raw=geo->output("Position");
  auto vmath=[&](NodeVectorMathType type,ShaderOutput *a,float3 b){auto n=g.create_node<VectorMathNode>();n->set_math_type(type);n->set_vector2(b);g.connect(a,n->input("Vector1"));return n->output("Vector");};
  // Procedural textures read the position quantized to 1/64 (exact on GPU and
  // CPU): hashes of float bits and high-octave noise amplify a one-ulp position
  // difference, which would compare the shared-data path, not the node.
  ShaderOutput *pos=!render&&(test<30||test>=52)?vmath(NODE_VECTOR_MATH_MULTIPLY,vmath(NODE_VECTOR_MATH_FLOOR,vmath(NODE_VECTOR_MATH_MULTIPLY,raw,make_float3(64,64,64)),zero_float3()),make_float3(1.f/64,1.f/64,1.f/64)):raw;
  auto component=[&](ShaderOutput *v,const char *name){auto n=g.create_node<SeparateXYZNode>();g.connect(v,n->input("Vector"));return n->output(name);};
  // Fraction of a scaled position: per-path factors in [0,1).
  ShaderOutput *unit=vmath(NODE_VECTOR_MATH_FRACTION,vmath(NODE_VECTOR_MATH_MULTIPLY,pos,make_float3(.37f,.53f,.71f)),zero_float3());
  ShaderOutput *fx=component(unit,"X"),*fy=component(unit,"Y");
  ShaderOutput *color=nullptr,*value=nullptr;
  if(test<20){
    auto n=g.create_node<NoiseTextureNode>();const unsigned dims=1+test%4,kind=test/4;
    const NodeNoiseType types[]={NODE_NOISE_FBM,NODE_NOISE_MULTIFRACTAL,NODE_NOISE_HYBRID_MULTIFRACTAL,NODE_NOISE_RIDGED_MULTIFRACTAL,NODE_NOISE_HETERO_TERRAIN};
    const float detail[]={0,2.5f,5.75f,1,15,3.3f,.4f,8};const float roughness[]={.5f,.75f,.3f,1.2f,-.2f};
    n->set_dimensions(dims);n->set_type(types[kind]);n->set_use_normalize(test%3!=1);n->set_detail(render?std::min(detail[test%8],3.f):detail[test%8]);n->set_roughness(roughness[test%5]);
    n->set_lacunarity(test%2?2.f:1.7f);n->set_offset(test%5==2?.6f:1.f);n->set_gain(test%3?1.f:2.4f);n->set_distortion(test%4==1?0.f:.35f*float(test%3));n->set_scale(test%2?3.1f:.8f);
    if(test==7){n->tex_mapping.translation=make_float3(.2f,-.3f,.1f);n->tex_mapping.rotation=make_float3(.3f,.1f,-.2f);n->tex_mapping.scale=make_float3(1.5f,.7f,1.1f);}
    if(test==11){n->tex_mapping.use_minmax=true;n->tex_mapping.min=make_float3(-2,-1,-3);n->tex_mapping.max=make_float3(3,2,1);}
    g.connect(pos,n->input("Vector"));g.connect(component(pos,"Z"),n->input("W"));color=n->output("Color");value=n->output("Fac");
  }
  else if(test<24){auto n=g.create_node<WhiteNoiseTextureNode>();n->set_dimensions(test-19);g.connect(pos,n->input("Vector"));g.connect(fx,n->input("W"));color=n->output("Color");value=n->output("Value");}
  else if(test<26){auto n=g.create_node<WaveTextureNode>();n->set_wave_type(test==24?NODE_WAVE_BANDS:NODE_WAVE_RINGS);n->set_bands_direction(NODE_WAVE_BANDS_DIRECTION_DIAGONAL);
    n->set_rings_direction(NODE_WAVE_RINGS_DIRECTION_SPHERICAL);n->set_profile(test==24?NODE_WAVE_PROFILE_SIN:NODE_WAVE_PROFILE_TRI);n->set_scale(1.3f);n->set_distortion(render?1.f:2.5f);n->set_detail(render?2.f:3.5f);
    n->set_detail_scale(1.4f);n->set_detail_roughness(.6f);n->set_phase(.3f);g.connect(pos,n->input("Vector"));color=n->output("Color");value=n->output("Fac");}
  else if(test<28){auto n=g.create_node<MagicTextureNode>();n->set_depth(test==26?2:render?4:10);n->set_scale(2.7f);n->set_distortion(1.f);g.connect(pos,n->input("Vector"));color=n->output("Color");value=n->output("Fac");}
  else if(test<30){auto n=g.create_node<BrickTextureNode>();n->set_offset(.5f);n->set_offset_frequency(2);n->set_squash(test==28?1.f:.6f);n->set_squash_frequency(test==28?2:3);
    n->set_color1(make_float3(.8,.3,.2));n->set_color2(make_float3(.3,.6,.2));n->set_mortar(make_float3(.1,.1,.1));n->set_scale(render?1.5f:4.1f);n->set_mortar_size(render?.1f:.03f);n->set_mortar_smooth(render?1.f:test==28?0.f:.4f);
    n->set_bias(.1f);n->set_brick_width(.6f);n->set_row_height(.25f);g.connect(pos,n->input("Vector"));color=n->output("Color");value=n->output("Fac");}
  else if(test<34){auto n=g.create_node<MappingNode>();n->set_mapping_type(NodeMappingType(test-30));n->set_location(make_float3(.3,-.2,.5));n->set_rotation(make_float3(.4f,-.7f,1.3f));
    n->set_scale(test==33?make_float3(1.5f,0,.5f):make_float3(1.5f,-.8f,.5f));g.connect(pos,n->input("Vector"));color=vmath(NODE_VECTOR_MATH_FRACTION,n->output("Vector"),zero_float3());value=component(n->output("Vector"),"Y");}
  else if(test<36){
    if(test==34){ShaderOutput *chain=fx;const NodeMapRangeType types[]={NODE_MAP_RANGE_LINEAR,render?NODE_MAP_RANGE_LINEAR:NODE_MAP_RANGE_STEPPED,NODE_MAP_RANGE_SMOOTHSTEP,NODE_MAP_RANGE_SMOOTHERSTEP};
      ShaderOutput *outs[4];for(unsigned k=0;k<4;k++){auto n=g.create_node<MapRangeNode>();n->set_range_type(types[k]);n->set_from_min(k%2?.8f:.1f);n->set_from_max(k%2?.2f:.9f);n->set_to_min(-.5f);n->set_to_max(1.5f);n->set_steps(4);n->set_clamp(k==0);
        g.connect(k==0?chain:outs[k-1],n->input("Value"));outs[k]=n->output("Result");}
      auto comb=g.create_node<CombineXYZNode>();g.connect(outs[1],comb->input("X"));g.connect(outs[2],comb->input("Y"));g.connect(outs[3],comb->input("Z"));color=comb->output("Vector");value=outs[0];}
    else{auto n=g.create_node<VectorMapRangeNode>();n->set_range_type(render?NODE_MAP_RANGE_SMOOTHSTEP:NODE_MAP_RANGE_STEPPED);n->set_use_clamp(true);n->set_from_min(make_float3(.1,.9,.2));n->set_from_max(make_float3(.9,.1,.2));n->set_to_min(make_float3(0,1,-1));n->set_to_max(make_float3(1,0,1));n->set_steps(make_float3(3,0,5));
      g.connect(unit,n->input("Vector"));auto n2=g.create_node<VectorMapRangeNode>();n2->set_range_type(NODE_MAP_RANGE_SMOOTHERSTEP);n2->set_from_min(make_float3(0,.2,.4));n2->set_from_max(make_float3(1,.8,.4));n2->set_to_min(make_float3(.2,.2,.2));n2->set_to_max(make_float3(.9,.8,.7));
      g.connect(n->output("Vector"),n2->input("Vector"));color=n2->output("Vector");value=component(n->output("Vector"),"Z");}
  }
  else if(test<40){
    // Five blend modes per graph: all twenty NodeMix values over four graphs.
    color=unit;
    for(unsigned k=0;k<5;k++){auto n=g.create_node<MixColorNode>();n->set_blend_type(NodeMix((test-36)*5+k));n->set_use_clamp(k%2==0);n->set_use_clamp_result(k==3);n->set_b(make_float3(.3f+.1f*k,.7f,.2f));
      g.connect(color,n->input("A"));g.connect(k%2?fy:fx,n->input("Factor"));color=n->output("Result");}
    auto f=g.create_node<MixFloatNode>();f->set_a(-.4f);f->set_b(1.3f);f->set_use_clamp(test%2);g.connect(component(pos,"X"),f->input("Factor"));value=f->output("Result");
  }
  else if(test==40){auto h=g.create_node<HSVNode>();h->set_hue(.3f);h->set_saturation(1.4f);h->set_value(.8f);g.connect(fy,h->input("Fac"));g.connect(unit,h->input("Color"));
    auto legacy=g.create_node<MixNode>();legacy->set_mix_type(NODE_MIX_OVERLAY);legacy->set_use_clamp(true);legacy->set_color2(make_float3(.6,.2,.9));g.connect(h->output("Color"),legacy->input("Color1"));g.connect(fx,legacy->input("Fac"));
    auto v=g.create_node<MixVectorNode>();v->set_b(make_float3(.9,.1,.4));g.connect(legacy->output("Color"),v->input("A"));g.connect(fy,v->input("Factor"));
    auto nu=g.create_node<MixVectorNonUniformNode>();nu->set_b(make_float3(.1,.8,.3));nu->set_use_clamp(true);g.connect(v->output("Result"),nu->input("A"));g.connect(vmath(NODE_VECTOR_MATH_MULTIPLY,unit,make_float3(1.4f,-.3f,.9f)),nu->input("Factor"));color=nu->output("Result");value=fx;}
  else if(test==41){ShaderOutput *c=unit;ShaderOutput *last=nullptr;
    for(NodeCombSepColorType kind:{NODE_COMBSEP_COLOR_HSV,NODE_COMBSEP_COLOR_HSL,NODE_COMBSEP_COLOR_RGB}){
      auto sep=g.create_node<SeparateColorNode>();sep->set_color_type(kind);g.connect(c,sep->input("Color"));
      auto comb=g.create_node<CombineColorNode>();comb->set_color_type(kind==NODE_COMBSEP_COLOR_RGB?NODE_COMBSEP_COLOR_HSV:kind==NODE_COMBSEP_COLOR_HSV?NODE_COMBSEP_COLOR_HSL:NODE_COMBSEP_COLOR_RGB);
      g.connect(sep->output("Green"),comb->input("Red"));g.connect(sep->output("Blue"),comb->input("Green"));g.connect(sep->output("Red"),comb->input("Blue"));c=comb->output("Color");last=sep->output("Red");}
    color=c;value=last;}
  else if(test==42){auto r=g.create_node<RGBRampNode>();array<packed_float3> ramp;array<float> alpha;
    for(unsigned i=0;i<RAMP_TABLE_SIZE;i++){const float t=float(i)/(RAMP_TABLE_SIZE-1);ramp.push_back_slow(make_float3(t*t,1-t,.5f+.4f*std::sin(9*t)));alpha.push_back_slow(1-t*t);}
    r->set_ramp(ramp);r->set_ramp_alpha(alpha);r->set_interpolate(true);g.connect(component(pos,"X"),r->input("Fac"));
    auto step=g.create_node<RGBRampNode>();array<packed_float3> ramp2;array<float> alpha2;for(unsigned i=0;i<9;i++){ramp2.push_back_slow(make_float3(i*.1f,.9f-i*.1f,i%2));alpha2.push_back_slow(.1f*i);}
    step->set_ramp(ramp2);step->set_ramp_alpha(alpha2);step->set_interpolate(render);g.connect(fy,step->input("Fac"));
    auto mix=g.create_node<MixColorNode>();mix->set_blend_type(NODE_MIX_BLEND);mix->set_fac(.5f);g.connect(r->output("Color"),mix->input("A"));g.connect(step->output("Color"),mix->input("B"));color=mix->output("Result");value=r->output("Alpha");}
  else if(test==43){auto c=g.create_node<RGBCurvesNode>();array<packed_float3> curves;for(unsigned i=0;i<RAMP_TABLE_SIZE;i++){const float t=float(i)/(RAMP_TABLE_SIZE-1);curves.push_back_slow(make_float3(t*t,std::sqrt(t),1-t));}
    c->set_curves(curves);c->set_min_x(-.2f);c->set_max_x(1.1f);c->set_extrapolate(true);c->set_fac(.85f);g.connect(vmath(NODE_VECTOR_MATH_MULTIPLY,unit,make_float3(1.6f,1.3f,-.4f)),c->input("Color"));
    auto f=g.create_node<FloatCurveNode>();array<float> curve;for(unsigned i=0;i<RAMP_TABLE_SIZE;i++){const float t=float(i)/(RAMP_TABLE_SIZE-1);curve.push_back_slow(t*t*(3-2*t));}
    f->set_curve(curve);f->set_min_x(.1f);f->set_max_x(.9f);f->set_extrapolate(true);f->set_fac(.9f);g.connect(component(pos,"Y"),f->input("Value"));color=c->output("Color");value=f->output("Value");}
  else if(test==44){auto ior=g.create_node<MathNode>();ior->set_math_type(NODE_MATH_MULTIPLY_ADD);ior->set_value2(1.5f);ior->set_value3(.9f);g.connect(fx,ior->input("Value1"));
    auto fr=g.create_node<FresnelNode>();g.connect(ior->output("Value"),fr->input("IOR"));
    auto lw=g.create_node<LayerWeightNode>();g.connect(fy,lw->input("Blend"));
    auto comb=g.create_node<CombineXYZNode>();g.connect(fr->output("Fac"),comb->input("X"));g.connect(lw->output("Fresnel"),comb->input("Y"));g.connect(lw->output("Facing"),comb->input("Z"));color=comb->output("Vector");
    auto lw2=g.create_node<LayerWeightNode>();lw2->set_blend(.5f);value=lw2->output("Facing");}
  else if(test==45){ShaderOutput *v=pos;
    for(NodeVectorRotateType kind:{NODE_VECTOR_ROTATE_TYPE_AXIS,NODE_VECTOR_ROTATE_TYPE_AXIS_X,NODE_VECTOR_ROTATE_TYPE_AXIS_Y,NODE_VECTOR_ROTATE_TYPE_AXIS_Z,NODE_VECTOR_ROTATE_TYPE_EULER_XYZ}){
      for(bool invert:{false,true}){auto n=g.create_node<VectorRotateNode>();n->set_rotate_type(kind);n->set_invert(invert);n->set_center(make_float3(.2,-.1,.3));n->set_axis(make_float3(.3,.8,-.5));
        n->set_rotation(make_float3(.5f,-.3f,.9f));g.connect(fx,n->input("Angle"));g.connect(v,n->input("Vector"));v=n->output("Vector");}}
    color=vmath(NODE_VECTOR_MATH_FRACTION,v,zero_float3());value=component(v,"Z");}
  else if(test==46){auto m=g.create_node<MathNode>();m->set_math_type(NODE_MATH_MULTIPLY_ADD);m->set_value2(15000.f);m->set_value3(-700.f);g.connect(fx,m->input("Value1"));
    auto b=g.create_node<BlackbodyNode>();g.connect(m->output("Value"),b->input("Temperature"));color=b->output("Color");value=fy;}
  else if(test==48){auto info=g.create_node<ObjectInfoNode>();auto comb=g.create_node<CombineXYZNode>();g.connect(info->output("Random"),comb->input("X"));g.connect(info->output("Alpha"),comb->input("Y"));
    auto m=g.create_node<MathNode>();m->set_math_type(NODE_MATH_MULTIPLY_ADD);m->set_value2(.1f);g.connect(info->output("Material Index"),m->input("Value1"));g.connect(info->output("Object Index"),m->input("Value3"));g.connect(m->output("Value"),comb->input("Z"));
    auto add=g.create_node<VectorMathNode>();add->set_math_type(NODE_VECTOR_MATH_ADD);g.connect(comb->output("Vector"),add->input("Vector1"));g.connect(info->output("Color"),add->input("Vector2"));
    color=add->output("Vector");value=component(info->output("Location"),"X");}
  else if(test==49){auto lp=g.create_node<LightPathNode>();auto comb=g.create_node<CombineXYZNode>();
    g.connect(lp->output("Is Camera Ray"),comb->input("X"));g.connect(lp->output("Is Shadow Ray"),comb->input("Y"));g.connect(lp->output("Ray Length"),comb->input("Z"));
    // Ray-type flags in every graph; bounce depths only in the render variant
    // (the SVM gate also evaluates these graphs with shadow visibility).
    const char *flags[]={"Is Diffuse Ray","Is Glossy Ray","Is Singular Ray","Is Reflection Ray","Is Transmission Ray","Is Volume Scatter Ray"};
    const char *depths[]={"Ray Depth","Diffuse Depth","Glossy Depth","Transparent Depth","Transmission Depth"};
    ShaderOutput *sum=fx;
    auto accumulate=[&](ShaderOutput *o,float k){auto m=g.create_node<MathNode>();m->set_math_type(NODE_MATH_MULTIPLY_ADD);m->set_value2(k);g.connect(o,m->input("Value1"));g.connect(sum,m->input("Value3"));sum=m->output("Value");};
    for(unsigned k=0;k<6;k++)accumulate(lp->output(flags[k]),.1f*(k+1));
    if(render)for(unsigned k=0;k<5;k++)accumulate(lp->output(depths[k]),.07f*(k+1));
    color=comb->output("Vector");value=sum;}
  else if(test==50){auto cam=g.create_node<CameraNode>();color=vmath(NODE_VECTOR_MATH_ABSOLUTE,cam->output("View Vector"),zero_float3());
    auto m=g.create_node<MathNode>();m->set_math_type(NODE_MATH_MULTIPLY_ADD);m->set_value2(.05f);g.connect(cam->output("View Z Depth"),m->input("Value1"));g.connect(cam->output("View Distance"),m->input("Value3"));value=m->output("Value");}
  else if(test==51){ShaderOutput *v=raw;
    const NodeVectorTransformConvertSpace spaces[]={NODE_VECTOR_TRANSFORM_CONVERT_SPACE_WORLD,NODE_VECTOR_TRANSFORM_CONVERT_SPACE_OBJECT,NODE_VECTOR_TRANSFORM_CONVERT_SPACE_CAMERA};
    for(unsigned k=0;k<9;k++){auto n=g.create_node<VectorTransformNode>();n->set_transform_type(NodeVectorTransformType(k%3));n->set_convert_from(spaces[k/3]);n->set_convert_to(spaces[(k/3+1+k%2)%3]);g.connect(v,n->input("Vector"));v=n->output("Vector");}
    color=vmath(NODE_VECTOR_MATH_FRACTION,v,zero_float3());value=component(v,"Y");}
  else if(test>=52&&test<60){
    // Voronoi: every feature, metric and dimension count across eight graphs.
    auto n=g.create_node<VoronoiTextureNode>();const unsigned k=test-52;
    const NodeVoronoiFeature features[]={NODE_VORONOI_F1,NODE_VORONOI_F2,NODE_VORONOI_SMOOTH_F1,NODE_VORONOI_DISTANCE_TO_EDGE,NODE_VORONOI_N_SPHERE_RADIUS,NODE_VORONOI_F1,NODE_VORONOI_F1,NODE_VORONOI_SMOOTH_F1};
    const NodeVoronoiDistanceMetric metrics[]={NODE_VORONOI_EUCLIDEAN,NODE_VORONOI_MANHATTAN,NODE_VORONOI_CHEBYCHEV,NODE_VORONOI_EUCLIDEAN,NODE_VORONOI_EUCLIDEAN,NODE_VORONOI_MINKOWSKI,NODE_VORONOI_EUCLIDEAN,NODE_VORONOI_EUCLIDEAN};
    const int dims[]={3,2,3,3,2,4,1,2};const float detail[]={0,2.5f,0,1.5f,0,1,3,2};
    n->set_feature(features[k]);n->set_metric(metrics[k]);n->set_dimensions(dims[k]);n->set_detail(detail[k]);n->set_use_normalize(k%2==1);n->set_scale(k==6?3.7f:2.2f);
    n->set_roughness(.6f);n->set_lacunarity(2.1f);n->set_smoothness(k==7?.5f:1.f);n->set_exponent(1.5f);n->set_randomness(k==3?.7f:1.f);
    g.connect(pos,n->input("Vector"));g.connect(component(pos,"Z"),n->input("W"));
    if(k==3||k==4){color=vmath(NODE_VECTOR_MATH_ADD,unit,zero_float3());value=n->output(k==3?"Distance":"Radius");}
    else{auto mix=g.create_node<MixVectorNode>();mix->set_fac(.4f);g.connect(n->output("Color"),mix->input("A"));g.connect(vmath(NODE_VECTOR_MATH_FRACTION,n->output("Position"),zero_float3()),mix->input("B"));
      color=mix->output("Result");auto add=g.create_node<MathNode>();add->set_math_type(NODE_MATH_MULTIPLY_ADD);add->set_value2(.1f);g.connect(n->output("W"),add->input("Value1"));g.connect(n->output("Distance"),add->input("Value3"));value=add->output("Value");}
  }
  else if(test==60){auto m=g.create_node<MathNode>();m->set_math_type(NODE_MATH_MULTIPLY_ADD);m->set_value2(render?380.f:500.f);m->set_value3(render?390.f:330.f);g.connect(fx,m->input("Value1"));
    // The SVM variant crosses both table ends (zero colour, extrapolation below 380 nm).
    auto w=g.create_node<WavelengthNode>();g.connect(m->output("Value"),w->input("Wavelength"));color=w->output("Color");value=fy;}
  else if(test==61||test==62){
    // Wireframe in object and pixel units, constant and per-path sizes (SVM gate
    // only: it is a step function of the hit point, see the render order).
    auto w=g.create_node<WireframeNode>();w->set_use_pixel_size(test==62);w->set_size(test==62?40.f:.3f);
    auto size=g.create_node<MathNode>();size->set_math_type(NODE_MATH_MULTIPLY_ADD);size->set_value2(test==62?60.f:.6f);size->set_value3(test==62?2.f:.05f);g.connect(fy,size->input("Value1"));
    auto w2=g.create_node<WireframeNode>();w2->set_use_pixel_size(test==62);g.connect(size->output("Value"),w2->input("Size"));
    auto comb=g.create_node<CombineXYZNode>();g.connect(w->output("Fac"),comb->input("X"));g.connect(w2->output("Fac"),comb->input("Y"));g.connect(fx,comb->input("Z"));color=comb->output("Vector");value=w->output("Fac");}
  else if(test==63||test==64){
    // Bump mapping (SVM gate only): the height sub-graph is copied for the dx/dy
    // taps, so its geometry/texture-coordinate nodes compile to the derivative
    // variants with bump offsets. Heights are smooth, well-conditioned functions
    // of the unquantized hit point and the filter width is large, so a last-bit
    // difference in the shared shading point stays far inside the tolerance (the
    // bump normal of a linear height does not depend on the width; rounding does).
    auto bump=g.create_node<BumpNode>();bump->set_filter_width(2000.f);ShaderOutput *height;
    if(test==63){auto dot=g.create_node<VectorMathNode>();dot->set_math_type(NODE_VECTOR_MATH_DOT_PRODUCT);
      dot->set_vector2(make_float3(.7f,-1.3f,.4f));g.connect(raw,dot->input("Vector1"));height=dot->output("Value");bump->set_strength(.8f);bump->set_distance(1.f);}
    else{auto tc=g.create_node<TextureCoordinateNode>();auto len=g.create_node<VectorMathNode>();len->set_math_type(NODE_VECTOR_MATH_LENGTH);g.connect(tc->output("Object"),len->input("Vector1"));
      height=len->output("Value");bump->set_invert(true);bump->set_use_object_space(true);bump->set_strength(1.4f);bump->set_distance(.6f);
      auto n=g.create_node<GeometryNode>();g.connect(n->output("Normal"),bump->input("Normal"));}
    g.connect(height,bump->input("Height"));
    auto diffuse=g.create_node<DiffuseBsdfNode>();diffuse->set_roughness(.43f);g.connect(unit,diffuse->input("Color"));g.connect(bump->output("Normal"),diffuse->input("Normal"));
    auto emission=g.create_node<EmissionNode>();emission->set_color(make_float3(.3,.5,.7));g.connect(fx,emission->input("Strength"));
    auto add=g.create_node<AddClosureNode>();g.connect(diffuse->output("BSDF"),add->input("Closure1"));g.connect(emission->output("Emission"),add->input("Closure2"));
    return add->output("Closure");}
  else if(test==65||test==66){
    // Gabor noise: 2D with a per-path orientation and partial anisotropy, and 3D
    // isotropic (random impulse orientations) with an explicit base orientation.
    auto n=g.create_node<GaborTextureNode>();n->set_type(test==65?NODE_GABOR_TYPE_2D:NODE_GABOR_TYPE_3D);
    n->set_scale(test==65?3.1f:1.7f);n->set_frequency(test==65?2.2f:1.4f);n->set_anisotropy(test==65?.6f:0.f);n->set_orientation_3d(make_float3(.3f,-.8f,.5f));
    g.connect(pos,n->input("Vector"));if(test==65)g.connect(fx,n->input("Orientation 2D"));
    auto comb=g.create_node<CombineXYZNode>();g.connect(n->output("Value"),comb->input("X"));g.connect(n->output("Phase"),comb->input("Y"));g.connect(n->output("Intensity"),comb->input("Z"));
    color=comb->output("Vector");value=n->output("Value");}
  else{auto n=g.create_node<NormalNode>();n->set_direction(make_float3(.3f,-.5f,.8f));g.connect(vmath(NODE_VECTOR_MATH_SUBTRACT,unit,make_float3(.5f,.5f,.5f)),n->input("Normal"));
    color=vmath(NODE_VECTOR_MATH_ABSOLUTE,n->output("Normal"),zero_float3());value=n->output("Dot");}
  auto diffuse=g.create_node<DiffuseBsdfNode>();diffuse->set_roughness(.43f);g.connect(color,diffuse->input("Color"));
  auto emission=g.create_node<EmissionNode>();emission->set_color(make_float3(.3,.5,.7));g.connect(value,emission->input("Strength"));
  auto add=g.create_node<AddClosureNode>();g.connect(diffuse->output("BSDF"),add->input("Closure1"));g.connect(emission->output("Emission"),add->input("Closure2"));
  if(test%6!=5)return add->output("Closure");
  // Node-driven closure mix with a transparent BSDF (transparent bounces in the render).
  auto transparent=g.create_node<TransparentBsdfNode>();transparent->set_color(make_float3(.8,.85,.9));auto mix=g.create_node<MixClosureNode>();
  g.connect(fy,mix->input("Fac"));g.connect(add->output("Closure"),mix->input("Closure1"));g.connect(transparent->output("BSDF"),mix->input("Closure2"));
  return mix->output("Closure");
}
