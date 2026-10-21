================================================================================
AGENT-README: CodeBrix.Graphics3D.Cycles
A Guide for AI Coding Agents - CONSUMING the
CodeBrix.Graphics3D.Cycles.ApacheLicenseForever NuGet package
================================================================================

OVERVIEW
========
CodeBrix.Graphics3D.Cycles builds and renders 3D scenes with the Cycles
path-tracing renderer from .NET 10 or later. Rendering runs in a native scene
host on a GPU backend (WebGPU, or Cycles' own Metal device on Apple Silicon)
or on an explicit CPU reference backend.

The package offers three tiers over the same native host. Use the highest one
that does what you need; they can be mixed on one scene.

  1. Typed facade (start here). CyclesScene owns a scene and hands out typed
     nodes: scene.Camera, scene.CreateShader(), shader.Add<DiffuseBsdfNode>(),
     shader.Connect(...), scene.CreateMesh(...), scene.CreateLight<T>(),
     scene.CreateImage<T>(...), await scene.RenderAsync(...).
  2. Generated object model. One class per native node type (scene nodes,
     geometry, lights and every shader graph node), generated from the
     native registry, with strongly typed properties, enums and shader graph
     sockets. The facade returns these classes.
  3. Interop tier. CyclesHost (by-name sockets, exceptions, spans),
     Interop.CyclesAbi (one method per C function, status codes, raw 64-bit
     handles) and Interop.Native (the raw P/Invoke declarations). This tier is
     the stable low-level surface; it addresses node types and sockets by
     their native names and mirrors the C ABI one to one.

Everything happens in the native library: scene compilation, BVH construction,
image handling, color management, scheduling and rendering. The managed code
only owns, marshals and types. Nothing is rendered, cached or simulated in
managed code; a typed property read is a native socket read.

RAW PIXELS ONLY. Textures go in as decoded pixels in one of eight layouts
(ImageFormat); renders come out as raw scene-linear float32 RGBA. This package
reads and writes no image files. Convert any image format (PNG, JPEG, JPEG XL,
TIFF, EXR, ...) to and from these buffers with CodeBrix.Imaging.

STATUS: experimental. The package is not yet published to nuget.org, and it
does not yet carry native runtime assets (see INSTALLATION).


INSTALLATION
============
PackageId:  CodeBrix.Graphics3D.Cycles.ApacheLicenseForever

    dotnet add package CodeBrix.Graphics3D.Cycles.ApacheLicenseForever

Dependencies: none other than .NET.
License:      Apache-2.0.
Requirements:
  - A 64-bit process (constructors throw PlatformNotSupportedException
    otherwise).
  - The native scene host library, named codebrix_cycles_host
    (libcodebrix_cycles_host.so on Linux), resolvable by the .NET native
    library loader. The package does not ship it yet: the process must find it
    on its library search path, or the application must register a
    NativeLibrary.SetDllImportResolver for this assembly that loads it.
  - On macOS, the minimum macOS release the native library supports; the
    wrapper checks it before the native library loads and throws
    PlatformNotSupportedException on an older system.


KEY NAMESPACES / USINGS
=======================
    using CodeBrix.Graphics3D.Cycles;                     // CyclesScene, CyclesHost, CyclesImage,
                                                          // CyclesBackend, Backend, ImageFormat,
                                                          // Transform3x4, CyclesException, CyclesVersion
    using CodeBrix.Graphics3D.Cycles.Rendering;           // RenderSettings, FilmBuffer, RenderProgress
    using CodeBrix.Graphics3D.Cycles.Generated.Scene;     // CameraNode, FilmNode, IntegratorNode,
                                                          // BackgroundNode, ObjectNode, ShaderNode, ...
    using CodeBrix.Graphics3D.Cycles.Generated.Geometry;  // GeometryBaseNode, MeshNode, HairNode,
                                                          // PointCloudNode, VolumeNode
    using CodeBrix.Graphics3D.Cycles.Generated.Lights;    // LightNode, PointLightNode, SpotLightNode,
                                                          // AreaLightNode, SunLightNode, BackgroundLightNode
    using CodeBrix.Graphics3D.Cycles.Generated.Shaders;   // DiffuseBsdfNode, PrincipledBsdfNode,
                                                          // ImageTextureNode, OutputNode, ...
    using CodeBrix.Graphics3D.Cycles.Generated;           // NodeType (the catalogue)
    using CodeBrix.Graphics3D.Cycles.Nodes;               // CyclesNode, ShaderGraphNode, ShaderInput,
                                                          // ShaderOutput, NodeTypeInfo, SocketInfo
    using CodeBrix.Graphics3D.Cycles.Diagnostics;         // ICapabilityReport
    using CodeBrix.Graphics3D.Cycles.Interop;             // CyclesAbi, Native (low-level tier)


CORE API REFERENCE
==================
TIER 1 - TYPED FACADE
---------------------
CyclesScene : IDisposable, IAsyncDisposable                     (sealed)
    CyclesScene(CyclesBackend backend, uint threads = 1)
    CyclesScene(Backend backend, uint threads = 1)    explicit native backend
    static Backend ResolveBackend(CyclesBackend)      Gpu -> Metal in osx-arm64
                                                      processes, WebGpu elsewhere;
                                                      CpuReference -> CpuReference
    CyclesHost Host            the by-name host this scene owns (tier 3)
    Backend Backend            the concrete backend
    CameraNode Camera / FilmNode Film / IntegratorNode Integrator /
    BackgroundNode Background  the scene singletons (typed)
    T Create<T>() where T : CyclesNode
                               objects, geometry, lights, shader graphs,
                               particle systems, passes (NodeCreation.Scene);
                               ArgumentException for any other type
    ShaderNode CreateShader()  a new, empty shader graph
    T CreateLight<T>() where T : LightNode
    MeshNode CreateMesh(ReadOnlySpan<float> xyz, ReadOnlySpan<uint> indices,
                        ShaderNode shader = null)
    ObjectNode CreateObject(GeometryBaseNode geometry, Transform3x4? transform = null)
                               places a mesh, other geometry or a LIGHT in the scene
    CyclesImage CreateImage<T>(uint width, uint height, ImageFormat format,
                               ReadOnlySpan<T> pixels) where T : unmanaged
                               T = float, Half, byte or ushort (the format's
                               component type) or byte for raw bytes
    static int ComponentBytes(ImageFormat) / static int Channels(ImageFormat)
    CyclesNode Wrap(Node) / T Wrap<T>(Node)
                               typed view of a node created through Host;
                               the same object is returned for the same node
    ICapabilityReport GetCapabilities()
    void Render(RenderSettings, Span<float> destination)
    FilmBuffer Render(RenderSettings)
    Task<FilmBuffer> RenderAsync(RenderSettings, IProgress<RenderProgress> = null,
                                 CancellationToken = default)
    Task RenderAsync(RenderSettings, Memory<float> destination,
                     IProgress<RenderProgress> = null, CancellationToken = default)
    TimeSpan ProgressInterval  progress poll interval (default 50 ms)
    bool IsRendering / bool Cancel()
    void Dispose() / ValueTask DisposeAsync()
                               cancel any active render, wait, destroy; every
                               later member (and every node) throws
                               ObjectDisposedException

RenderSettings(uint Width, uint Height, uint Samples)   readonly record struct
    long FloatCount            Width * Height * 4

FilmBuffer                                              (sealed)
    int Width, Height, Stride (= Width * 4 floats), RenderPass Pass
    ReadOnlyMemory<float> Pixels / ReadOnlySpan<float> Span
    ReadOnlySpan<float> GetRow(int y)    y = 0 is the BOTTOM row
    void CopyTo(Span<float>) / float[] ToArray()
  THE FILM CONTRACT: raw scene-linear float32 RGBA, four floats per pixel,
  rows bottom to top (lower-left origin), unclamped, not display-encoded,
  alpha = coverage. Flip rows and encode with CodeBrix.Imaging for files.

RenderProgress(RenderStage Stage, TimeSpan Elapsed, uint TotalSamples,
               uint? CompletedSamples, double? Fraction)
RenderStage   Starting, Rendering, Completed, Cancelled, Failed
RenderPass    Combined (more passes join when the native host exposes them)
  Progress is POLLED today: the native host has no progress callback yet, so
  Rendering reports carry only Elapsed (CompletedSamples and Fraction are
  null) and arrive every ProgressInterval. Completed reports Fraction = 1.

CyclesImage : IDisposable                               (sealed)
    Image Handle, uint Width, uint Height, ImageFormat Format, bool IsReleased
    Dispose() releases YOUR reference; bound textures keep the pixels.

CyclesBackend   Gpu, CpuReference
CyclesVersion (static)
    uint Abi                   ABI integer of the loaded native host
    const uint ExpectedAbi     the ABI this assembly was built against
    bool IsAbiCompatible
    string NativeLibraryVersion, string CyclesRevision
                               null until the native host reports them
    Version ManagedVersion

ICapabilityReport (from GetCapabilities)
    uint AbiVersion, string BackendName ("webgpu", "metal", "cpu"),
    Backend Backend, bool IsGpu, string PipelineTier ("full", "conservative",
    or null off WebGPU), IReadOnlyList<string> Warnings, string Json (the full
    native report)

Transform3x4(M11..M34)    readonly record struct; Cycles 3x4 row-major,
                          translation in the 4th column
    Identity, CreateTranslation(x, y, z), FromMatrix(Matrix4x4),
    ToMatrix(), FromSpan(ReadOnlySpan<float>), CopyTo(Span<float>)

CyclesException : Exception
    NativeStatus NativeStatus  Invalid, Unsupported, Failed, Cancelled,
                               NotReady, SmallBuffer
NativeFailure : CyclesException   (sealed; what every failing native call throws)
    int Status                 the same status as an integer

TIER 2 - GENERATED OBJECT MODEL
-------------------------------
One class per native node type, generated from the native registry. Class
names are the PascalCase native name plus "Node" (diffuse_bsdf ->
DiffuseBsdfNode, image_texture -> ImageTextureNode, pointlight ->
PointLightNode, geometry_base -> GeometryBaseNode). Each class has:
  const string NativeTypeName
  one property per readable, writable, non-internal input socket, named in
  PascalCase after the native socket name (tex_mapping.scale ->
  TexMappingScale):
      float, int, bool, uint, ulong, string     scalar sockets
      Vector3                                   color, vector, point, normal
      Vector2                                   point2
      Transform3x4                              transform
      generated enum                            enum sockets (values are the
                                                native values; enum type name
                                                = class name without "Node" +
                                                property name, e.g.
                                                ImageTextureInterpolation)
      typed node class                          node sockets (e.g.
                                                ObjectNode.Geometry is a
                                                GeometryBaseNode)
      T[]                                       array sockets (copies in/out)
  shader graph nodes also have, per linkable input and per output:
      ShaderInput  <UiName>Input     e.g. DiffuseBsdfNode.ColorInput
      ShaderOutput <UiName>Output    e.g. DiffuseBsdfNode.BsdfOutput
Hand-maintained conveniences on a few generated classes:
  ShaderNode.Output (OutputNode), ShaderNode.Add<T>(),
  ShaderNode.Connect(ShaderOutput from, ShaderInput to)
  GeometryBaseNode.SetAttribute / SetAttributeBytes / GetAttribute /
  RemoveAttribute
  MeshNode.SetTriangles(ReadOnlySpan<float> or ReadOnlySpan<Vector3>, indices)
  ImageTextureNode / EnvironmentTextureNode .Bind(CyclesImage) / .Unbind()

CyclesNode (base of every generated class)
    CyclesScene Scene, Node Handle, NodeTypeInfo TypeInfo,
    PropertyInfo Describe(string socket)
ShaderGraphNode : CyclesNode     ShaderNode Graph

NodeType (static catalogue)
    IReadOnlyList<NodeTypeInfo> All, NodeTypeInfo Find(string nativeName),
    NodeTypeInfo Of(Type) / Of<T>(), const uint RegistryAbiVersion
NodeTypeInfo: Name, Category (Scene, Camera, Film, Integrator, Background,
    Geometry, Light, Shader, Output), Creation (Singleton, Scene, ShaderGraph,
    GraphOutput, Unavailable), BaseName, HasFactory, ClrType, Inputs, Outputs
SocketInfo: Name, UiName, TypeName, Kind (ValueKind), Components, Flags
    (PropertyFlags), NodeTypeName, EnumValues (EnumEntry(Name, Value))
  Creation = Unavailable marks base types and types the native host does not
  create directly (buffer_params, buffer_pass, denoise_params,
  scene_attributes); they are catalogued for completeness.

TIER 3 - INTEROP (STABLE LOW-LEVEL TIER)
----------------------------------------
CyclesHost : IDisposable  (root namespace; by name, throws NativeFailure)
    CyclesHost(Backend backend, uint threads = 1); Dispose; IsRendering; Cancel
    Node SceneNode(string) / CreateNode(string type) / ShaderOutput(Node) /
         ShaderNode(Node shader, string type)
    void Connect(Node from, string output, Node to, string input)
                               output/input are socket UI names ("BSDF",
                               "Color", "Surface")
    SetFloats / SetInts / SetUInt / SetString / SetStrings / SetNodes
                               socket names are native names ("value",
                               "roughness", "used_shaders")
    Property / GetFloats / GetInts / GetUInt / GetUtf8 / GetNodes
                               span readers: Span<T>.Empty = size query;
                               false = too small, destination unchanged
    string NodeTypeName(Node)
    Mesh, SetAttribute, SetAttributeBytes, GetAttribute, GetAttributeBytes,
    RemoveAttribute, CreateImage, BindImage, ReleaseImage
    void Render(uint w, uint h, uint samples, Span<float> rgba)
    static ConfigureColor(string path)       process-wide OCIO config
    static ConfigureTiers(PipelineTier, string recordDirectory, PipelineTierFlags)
    static string RegistryJson() / string DeviceReportJson()
  Float sockets take flattened components: float2 = 2, color/vector/point/
  normal = 3, transform = 12 (row-major 3x4) per element. Booleans are 0/1
  via SetInts. Enum sockets accept the item name via SetString or the value
  via SetInts.

Interop.CyclesAbi (static) - one method per cbc_host_* C function
    NativeStatus Create(Backend, uint threads, out ulong host) / Destroy(host)
    NativeStatus SceneNode / CreateNode / ShaderOutput / ShaderNode / Connect
    NativeStatus SetFloats / SetInts / SetUInts / SetString / SetStrings /
                 SetNodes
    NativeStatus PropertyInfo / GetFloats / GetInts / GetUInts / GetString /
                 GetNodes / NodeType / RegistryJson / DeviceReportJson
    NativeStatus SetAttributeFloats / SetAttributeBytes / GetAttributeFloats /
                 GetAttributeBytes / RemoveAttribute
    NativeStatus CreateImage / BindImage / ReleaseImage / Mesh / Render /
                 Cancel / IsRendering / ConfigureColor / ConfigureTiers
    uint AbiVersion(); string LastError()   (thread-local; read right after
                                             the failing call)
    const uint ExpectedAbiVersion
  Nothing throws for a native failure; you own every host handle and must
  Destroy it exactly once.

Interop.Native (static) - the raw [DllImport] declarations and ABI structs
(HostConfig, RenderConfig, PropertyInfo, AttributeConfig, ImageConfig,
TierConfig; every struct is 32 bytes with Size first). Use it to write your
own binding; the C header native/host/cycles_host.h is the contract.

Shared value types and enums (root namespace)
    Node(ulong Value), Image(ulong Value)  opaque per-host identifiers
    PropertyInfo(ValueType, Flags, Components, Count) + Kind, PropertyFlags
    Backend         WebGpu, CpuReference, Metal (osx-arm64 builds only)
    NativeStatus    Ok, Invalid, Unsupported, Failed, Cancelled, NotReady,
                    SmallBuffer
    ValueKind       Unsupported, Boolean, Int, UInt, UInt64, Float, Color,
                    Vector, Point, Normal, Point2, String, Enum, Transform,
                    Node, Closure, Float4, Byte4
    PropertyFlags   None, Array, Linkable, Internal, Readable, Writable
    AttributeValue, AttributeDomain, AttributeFlags (geometry attributes)
    ImageFormat     Float, Float4, Half, Half4, Byte, Byte4, UShort, UShort4
    PipelineTier, PipelineTierFlags (WebGPU pipeline-tier selection)
  Every enum value equals the C constant of the same meaning.

BACKEND SELECTION
-----------------
Use CyclesBackend.Gpu for "the GPU of this platform" and CyclesBackend.
CpuReference for the CPU. A GPU render never falls back to the CPU: if no
usable GPU exists, scene creation or the render fails with a CyclesException.
Which GPU backend renders (WebGPU or Metal) is visible only through
GetCapabilities(); write backend-neutral code against CyclesBackend.

DETERMINISM
-----------
On the CPU reference backend, rendering the same scene with the same settings
in the same process is bit-identical, and the three managed tiers produce
bit-identical films for the same scene. GPU accumulation is not guaranteed to
be bitwise repeatable. The native determinism statement in the repository's
native host documentation is authoritative.


COMPLETE EXAMPLES
=================
Render a lit triangle with the typed facade
-------------------------------------------
    using System.Numerics;
    using CodeBrix.Graphics3D.Cycles;
    using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
    using CodeBrix.Graphics3D.Cycles.Generated.Scene;
    using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
    using CodeBrix.Graphics3D.Cycles.Rendering;

    await using CyclesScene scene = new(CyclesBackend.CpuReference);
    scene.Camera.Fov = 1.1f;
    scene.Camera.Matrix = Transform3x4.CreateTranslation(0, 0, -4);

    ShaderNode material = scene.CreateShader();
    DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
    diffuse.Color = new Vector3(.7f, .2f, .1f);
    material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);

    MeshNode mesh = scene.CreateMesh([-1.5f,-1,0, 1.5f,-1,0, 0,1.5f,0], [0, 1, 2], material);
    scene.CreateObject(mesh);

    ShaderNode world = scene.CreateShader();
    BackgroundShaderNode sky = world.Add<BackgroundShaderNode>();
    sky.Color = new Vector3(.25f, .4f, .6f);
    world.Connect(sky.BackgroundOutput, world.Output.SurfaceInput);
    scene.Background.Shader = world;

    FilmBuffer film = await scene.RenderAsync(new RenderSettings(320, 240, 16));
    // film.Span: scene-linear RGBA floats, bottom row first

Progress and cancellation
-------------------------
    using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(5));
    Progress<RenderProgress> progress = new(p => Console.WriteLine($"{p.Stage} {p.Elapsed}"));
    try
    {
        FilmBuffer film = await scene.RenderAsync(new RenderSettings(1920, 1080, 4096), progress, timeout.Token);
    }
    catch (OperationCanceledException)
    {
        // the native render stopped; the scene is reusable
    }

Render into your own buffer (no allocation per frame)
-----------------------------------------------------
    RenderSettings settings = new(640, 480, 64);
    float[] rgba = new float[settings.FloatCount];
    await scene.RenderAsync(settings, rgba);          // or scene.Render(settings, rgba)

A light
-------
    PointLightNode lamp = scene.CreateLight<PointLightNode>();
    lamp.Strength = new Vector3(100, 100, 100);
    ShaderNode emission = scene.CreateShader();
    EmissionNode emit = emission.Add<EmissionNode>();
    emission.Connect(emit.EmissionOutput, emission.Output.SurfaceInput);
    lamp.UsedShaders = [emission];
    scene.CreateObject(lamp, Transform3x4.CreateTranslation(2, 3, -2));

A texture from decoded pixels (decode the file with CodeBrix.Imaging)
---------------------------------------------------------------------
    // rgba8: tightly packed width*height*4 bytes, row 0 at v = 0
    ImageTextureNode texture = material.Add<ImageTextureNode>();
    using (CyclesImage image = scene.CreateImage<byte>(width, height, ImageFormat.Byte4, rgba8))
        texture.Bind(image);                  // the binding keeps the pixels
    texture.Colorspace = "__builtin_srgb";
    material.Connect(texture.ColorOutput, diffuse.ColorInput);
    mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner,
                      AttributeFlags.Standard, uvs);   // 2 floats per corner

    // float data: scene.CreateImage<float>(w, h, ImageFormat.Float4, floats)
    // half data:  scene.CreateImage<Half>(w, h, ImageFormat.Half4, halves)

Dropping to the by-name tier for a socket
-----------------------------------------
    Node camera = scene.Host.SceneNode("camera");
    scene.Host.SetFloats(camera, "fov", [0.9f]);
    float fov = scene.Camera.Fov;             // 0.9: both tiers see one native socket

The status-code interop tier
----------------------------
    using CodeBrix.Graphics3D.Cycles.Interop;

    if (CyclesAbi.Create(Backend.CpuReference, 1, out ulong host) != NativeStatus.Ok)
        throw new InvalidOperationException(CyclesAbi.LastError());
    try
    {
        CyclesAbi.SceneNode(host, "camera", out ulong camera);
        NativeStatus status = CyclesAbi.SetFloats(host, camera, "fov", [1.1f]);
        // ... CreateNode, ShaderNode, Connect, Mesh, Render as in the C header
    }
    finally
    {
        CyclesAbi.Destroy(host);
    }


MINIMUM VIABLE PROJECT TEMPLATE
===============================
    <Project Sdk="Microsoft.NET.Sdk">
      <PropertyGroup>
        <OutputType>Exe</OutputType>
        <TargetFramework>net10.0</TargetFramework>
      </PropertyGroup>
      <ItemGroup>
        <PackageReference Include="CodeBrix.Graphics3D.Cycles.ApacheLicenseForever" Version="*" />
      </ItemGroup>
    </Project>

Program.cs: the "Render a lit triangle with the typed facade" example above.
Run it with the native host on the library search path (for example
LD_LIBRARY_PATH on Linux).


PERFORMANCE TIPS
================
- Reuse one CyclesScene for repeated renders; change properties between
  renders instead of rebuilding the scene. Host creation and scene
  compilation are the expensive steps.
- Render into a buffer you keep (Render(settings, span) or
  RenderAsync(settings, memory, ...)) instead of allocating a FilmBuffer per
  frame.
- Array properties copy the whole array in and out on every access. Read an
  array property once into a local; set it once with the finished array.
- Upload a mesh once (SetTriangles) and change materials or transforms
  through properties.
- Set ProgressInterval to a longer interval when progress is only for display.


COMMON PITFALLS TO AVOID
========================
- FORGETTING THE OBJECT. A mesh or light renders only when an object places it:
  scene.CreateObject(meshOrLight). Lights are geometry in Cycles.
- CONNECTING ACROSS GRAPHS. Shader links stay inside one ShaderNode graph;
  connecting sockets from two graphs throws ArgumentException.
- PASSING THE WRONG COMPONENT TYPE. CreateImage<float> with a Half or Byte
  format throws ArgumentException; use the format's component type or byte.
- EXPECTING ENCODED OR TOP-DOWN OUTPUT. Films are scene-linear float RGBA with
  the BOTTOM row first. Flip the rows and display-encode (CodeBrix.Imaging)
  before writing a file.
- LOADING IMAGE FILES THROUGH THIS LIBRARY. It takes raw pixel buffers only;
  decode files with CodeBrix.Imaging first. Setting a texture's Filename is a
  development convenience of some native builds, not a product feature.
- EXPECTING A CPU FALLBACK. CyclesBackend.Gpu never falls back to the CPU;
  choose CyclesBackend.CpuReference explicitly when you want the CPU.
- CALLING ConfigureColor WHILE A HOST EXISTS. Color configuration is
  process-wide; it fails with NotReady while any scene, host or in-flight call
  is alive. Configure first, then create scenes.
- MIXING NODES BETWEEN SCENES. Nodes and images belong to the scene that
  created them; assigning one to another scene throws ArgumentException (or a
  NativeFailure with Invalid on the low-level tiers).
- TOUCHING THE DESTINATION DURING RenderAsync. The buffer is written by the
  native render on another thread and only on success; wait for the task.
- EXPECTING PER-SAMPLE PROGRESS. Progress is polled; Fraction and
  CompletedSamples stay null while a render runs. System.Progress<T> posts
  reports to the thread pool, so they can arrive out of order; implement
  IProgress<T> yourself when the order matters.
- READING THE REGISTRY AS A FEATURE LIST. A node type being registered (and
  generated) does not mean a GPU backend implements it; the CPU reference is
  the complete implementation.
- USING SOCKET NAMES FOR LINKS. On the low-level tiers, Connect takes socket
  UI names ("Color", "BSDF"); the setters take native socket names ("color",
  "value"). The typed tier handles both for you.


WHAT THIS PACKAGE DOES NOT DO
=============================
- It does not encode or decode image or video files (PNG, JPEG, JPEG XL, TIFF,
  EXR, ...). It accepts and returns raw pixel buffers; use CodeBrix.Imaging.
- It does not import model formats (glTF and others); convert their data into
  nodes, meshes and attributes yourself.
- It does not display images or present to a window or swap chain.
- It does not yet read back render passes other than the combined pass, report
  per-sample progress, delete nodes or images, or report the native library
  version; those arrive as additive native exports.
- It does not yet ship the native host library inside the package.


WORKING EXAMPLES ON GITHUB
==========================
- The typed facade: scene building, sync and async rendering, cancellation,
  all eight image formats, disposal:
  https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/tests/CodeBrix.Graphics3D.Cycles.Tests/CyclesSceneTests.cs
- One scene built through all three tiers:
  https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/tests/CodeBrix.Graphics3D.Cycles.Tests/TestScenes.cs
- The generated model and catalogue, and a round trip of every property:
  https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/tests/CodeBrix.Graphics3D.Cycles.Tests/NodeTypeTests.cs
- The status-code interop tier:
  https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/tests/CodeBrix.Graphics3D.Cycles.Tests/CyclesAbiTests.cs
- The by-name host: socket round trips, registry and device report JSON:
  https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/tests/CodeBrix.Graphics3D.Cycles.Tests/CyclesHostTests.cs


QUICK REFERENCE CARD
====================
Scene:         await using CyclesScene scene = new(CyclesBackend.Gpu);   // or CpuReference
Settings:      scene.Camera.Fov = 1.1f; scene.Integrator.MaxBounce = 8;
Material:      ShaderNode m = scene.CreateShader();
               DiffuseBsdfNode d = m.Add<DiffuseBsdfNode>();
               m.Connect(d.BsdfOutput, m.Output.SurfaceInput);
Mesh:          MeshNode mesh = scene.CreateMesh(xyz, indices, m);
Place:         scene.CreateObject(mesh, Transform3x4.CreateTranslation(x, y, z));
Light:         PointLightNode l = scene.CreateLight<PointLightNode>(); scene.CreateObject(l, t);
World:         scene.Background.Shader = worldGraph;
Texture:       using CyclesImage img = scene.CreateImage<byte>(w, h, ImageFormat.Byte4, rgba8);
               imageTextureNode.Bind(img);
Render:        FilmBuffer film = await scene.RenderAsync(new RenderSettings(w, h, spp), progress, token);
Into buffer:   scene.Render(new RenderSettings(w, h, spp), rgbaFloats);
Capabilities:  ICapabilityReport caps = scene.GetCapabilities();
Version:       CyclesVersion.Abi, CyclesVersion.IsAbiCompatible
Catalogue:     NodeType.Find("principled_bsdf").Inputs
By name:       scene.Host.SetFloats(node.Handle, "socket", [..]);
Raw ABI:       CyclesAbi.Create(Backend.CpuReference, 1, out ulong host); ... CyclesAbi.Destroy(host);
Errors:        catch (CyclesException e) when (e.NativeStatus == NativeStatus.Unsupported)

Target: .NET 10 or later
License: Apache-2.0


================================================================================
END OF AGENT-README
================================================================================
