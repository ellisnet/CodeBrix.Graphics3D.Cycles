================================================================================
AGENT-README: CodeBrix.Graphics3D.Cycles
A Guide for AI Coding Agents - CONSUMING the
CodeBrix.Graphics3D.Cycles.ApacheLicenseForever NuGet package
================================================================================

OVERVIEW
========
CodeBrix.Graphics3D.Cycles builds and renders 3D scenes with the Cycles
path-tracing renderer from .NET 10 or later. Rendering runs in a native scene
host on a GPU backend or on an explicit CPU reference backend. The GPU backend
depends on the platform: Cycles' own Metal device on macOS on Apple Silicon
(osx-arm64), and the CodeBrix WGSL device on WebGPU everywhere else, including
macOS on Intel (osx-x64).

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

Dependencies: CodeBrix.WebGpu.Native.MitLicenseForever, exactly version
              1.0.300.210 (the WebGPU runtime the GPU backend uses; NuGet
              places codebrix_wgpu beside the application). No other package.
License:      Apache-2.0.
Requirements:
  - A 64-bit process (constructors throw PlatformNotSupportedException
    otherwise).
  - The native scene host library, named codebrix_cycles_host
    (libcodebrix_cycles_host.so on Linux, libcodebrix_cycles_host.dylib on
    macOS, codebrix_cycles_host.dll on Windows), resolvable by the .NET native
    library loader. The package does not ship it yet: the process must find it
    on its library search path, or the application must register a
    NativeLibrary.SetDllImportResolver for this assembly that loads it.
  - On Windows (x64 and ARM64), these files beside codebrix_cycles_host.dll:
    the DLLs of its native dependencies, codebrix_wgpu.dll (the
    CodeBrix.WebGpu.Native runtime) and dxcompiler.dll (Microsoft's DirectX
    Shader Compiler, which the Direct3D 12 backend loads from the host's own
    folder). Nothing has to be installed on the machine: no Visual C++
    Redistributable is needed, and Direct3D 12 is part of Windows 10 and 11.
  - On macOS, the minimum macOS release the native library supports; the
    wrapper checks it before the native library loads and throws
    PlatformNotSupportedException on an older system.


KEY NAMESPACES / USINGS
=======================
    using CodeBrix.Graphics3D.Cycles;                     // CyclesScene, CyclesHost, CyclesImage,
                                                          // CyclesBackend, Backend, ImageFormat,
                                                          // Transform3x4, CyclesException, CyclesVersion
    using CodeBrix.Graphics3D.Cycles.Rendering;           // RenderSettings, FilmBuffer, RenderProgress,
                                                          // RenderEvent, RenderPassInfo
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
    using CodeBrix.Graphics3D.Cycles.Bundles;             // OracleSceneBundle (scene bundles)


CORE API REFERENCE
==================
TIER 1 - TYPED FACADE
---------------------
CyclesScene : IDisposable, IAsyncDisposable                     (sealed)
    CyclesScene(CyclesBackend backend, uint threads = 1)
    CyclesScene(Backend backend, uint threads = 1)    explicit native backend
    static Backend ResolveBackend(CyclesBackend)      asks the native binary:
                                                      Gpu -> Metal in the osx-arm64
                                                      build, WebGpu in every other
                                                      build (osx-x64 included);
                                                      CpuReference -> CpuReference
    static ICapabilityReport GetBinaryCapabilities()  binary-level report, no device
    CyclesHost Host            the by-name host this scene owns (tier 3)
    Backend Backend            the concrete backend the native host reports
                               (never Backend.Gpu)
    bool IsFaulted             the last render failed with NativeStatus.Failed;
                               the next render recovers the device (see
                               DEVICE ERRORS); cleared by a successful render
    event EventHandler<RenderEvent> RenderEventReceived
                               native status/progress/error events while a
                               render runs (see CALLBACKS AND THREADING)
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
    void Remove(CyclesNode node)
                               deletes objects, geometry, lights, passes,
                               shader graphs (and their nodes) and shader
                               graph nodes; node.IsRemoved becomes true.
                               InvalidOperationException while another node
                               still references it (an object's Geometry, a
                               mesh's UsedShaders, Background.Shader): clear
                               the reference or remove the referrer first.
                               ArgumentException for singletons and shader
                               output nodes.
    PassNode AddPass(PassType type, string name)
                               enables a pass/AOV (e.g. PassType.Depth)
    IReadOnlyList<RenderPassInfo> GetRenderedPasses()
                               passes of the last successful render
    FilmBuffer ReadPass(string name)
                               copy of one pass ("Combined" always present,
                               bit-identical to the rendered film);
                               InvalidOperationException before a successful
                               render, NotSupportedException when the device
                               did not write it
    ICapabilityReport GetCapabilities()   the scene's device + binary report
    void Render(RenderSettings, Span<float> destination)
    FilmBuffer Render(RenderSettings)
    Task<FilmBuffer> RenderAsync(RenderSettings, IProgress<RenderProgress> = null,
                                 CancellationToken = default)
    Task RenderAsync(RenderSettings, Memory<float> destination,
                     IProgress<RenderProgress> = null, CancellationToken = default)
    TimeSpan ProgressInterval  how often RenderAsync re-sends a pending
                               cancellation (default 50 ms); progress itself
                               comes from native events
    bool IsRendering / bool Cancel()
    void Dispose() / ValueTask DisposeAsync()
                               cancel any active render, wait, destroy; every
                               later member (and every node) throws
                               ObjectDisposedException

RenderSettings(uint Width, uint Height, uint Samples)   readonly record struct
    long FloatCount            Width * Height * 4

FilmBuffer                                              (sealed)
    int Width, Height, Components (4 for Combined; 1, 3 or 4 for other passes),
    Stride (= Width * Components floats), RenderPass Pass, string PassName
    ReadOnlyMemory<float> Pixels / ReadOnlySpan<float> Span
    ReadOnlySpan<float> GetRow(int y)    y = 0 is the BOTTOM row
    void CopyTo(Span<float>) / float[] ToArray()
  THE FILM CONTRACT: raw scene-linear float32 RGBA, four floats per pixel,
  rows bottom to top (lower-left origin), unclamped, not display-encoded,
  alpha = coverage. Flip rows and encode with CodeBrix.Imaging for files.
  Other passes follow the same layout with Components floats per pixel.

RenderProgress(RenderStage Stage, TimeSpan Elapsed, uint TotalSamples,
               uint? CompletedSamples, double? Fraction)
RenderStage   Starting, Rendering, Completed, Cancelled, Failed
RenderPass    Combined, Named (a pass enabled with AddPass)
RenderPassInfo(string Name, string Type, int Components, bool Written)
RenderEvent(RenderEventKind Kind, ulong SamplesDone, ulong SamplesTotal,
            TimeSpan Elapsed, string Message)
RenderEventKind  Status, Progress, Error   (= CBC_EVENT_*)
  Progress comes from the native progress callback: one Rendering report per
  native progress event, with CompletedSamples and Fraction filled; Completed
  reports Fraction = 1. Reports arrive on a native render thread unless your
  IProgress<T> marshals them.

CyclesImage : IDisposable                               (sealed)
    Image Handle, uint Width, uint Height, ImageFormat Format, bool IsReleased
    void Update<T>(ReadOnlySpan<T> pixels)             same size and format
    void Update<T>(uint w, uint h, ImageFormat, ReadOnlySpan<T> pixels)
                               re-uploads raw pixels; every texture still
                               bound to the image renders them next time
    Dispose() releases YOUR reference; bound textures keep the pixels
    (textures bound before Dispose keep the pixels they had).

CyclesBackend   Gpu, CpuReference
CyclesVersion (static)
    uint Abi                   ABI integer of the loaded native host
    const uint ExpectedAbi     the ABI this assembly was built against
    bool IsAbiCompatible
    string NativeLibraryVersion  native build version, major.minor.yyyyMMdd.build
    uint NativeAbi             ABI in the native version structure
    string CyclesRevision      pinned upstream Cycles commit hash
    Version ManagedVersion

ICapabilityReport (GetCapabilities / GetBinaryCapabilities; exactly what the
native report states, nothing inferred)
    int SchemaVersion, uint AbiVersion, string LibraryVersion, CyclesRevision
    bool HasDevice             false for the binary-level report
    string BackendName ("webgpu", "metal", "cpu"; null without a device),
    Backend Backend (Backend.Gpu without a device), bool IsGpu,
    string DeviceDescription, DeviceId, PipelineTier ("full", "conservative",
    or null off WebGPU), IReadOnlyList<string> Warnings
    IReadOnlyList<string> CompiledBackends, string GpuBackendName,
    bool GpuFallsBackToCpu (always false)
    IReadOnlyDictionary<string, BackendScope> BackendScopes, BackendScope Scope
    IReadOnlyDictionary<string, bool> OptionalLibraries (osl, embree, ...)
    IReadOnlyList<string> ImageFormats, Events
    IReadOnlyDictionary<string, int> PassTypes      registered pass types
    IReadOnlyDictionary<string, long> Limits        maxRenderWidth, maxMotionSteps...
    string DeterminismReference, string Json (the full native report)
BackendScope: Device, CameraTypes, Primitives, WrittenPasses, PassesNote,
    RenderTimeRejections, Reference
Warnings also carries a notice when a scene compiles to an unusually large GPU
program that some older Intel graphics drivers may fail to compile; the notice
never changes the rendered result.

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
  MeshNode.SetMotionPositions(uint steps, ReadOnlySpan<float> or
  ReadOnlySpan<Vector3>) / ClearMotionPositions()   deformation motion
  (before or after a render; see MOTION)
  ImageTextureNode / EnvironmentTextureNode .Bind(CyclesImage) / .Unbind()

CyclesNode (base of every generated class)
    CyclesScene Scene, Node Handle, NodeTypeInfo TypeInfo,
    PropertyInfo Describe(string socket), bool IsRemoved (every accessor of a
    removed node throws ObjectDisposedException)
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
    static Backend ResolveBackend(Backend) / Backend GetBackend()
    static string CapabilitiesJson() / string HostCapabilitiesJson()
    void DeleteNode(Node), UpdateImage(Image, w, h, ImageFormat, bytes),
    MotionPositions(Node mesh, uint steps, ReadOnlySpan<float>)
    void SetEventCallback(delegate* unmanaged[Cdecl]<nint, Native.Event*, int>,
                          nint userData)    (a scene manages this itself)
    bool PassFloats(string name, Span<float>, out ulong required,
                    out uint components) / string RenderPassesJson()
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
    NativeStatus Version(out uint abi, out string library, out string revision)
    NativeStatus ResolveBackend / GetBackend / CapabilitiesJson / DeleteNode /
                 UpdateImage / MotionPositions / SetEventCallback /
                 PassFloats / RenderPassesJson
    uint AbiVersion(); string LastError()   (thread-local; read right after
                                             the failing call)
    const uint ExpectedAbiVersion
  Nothing throws for a native failure; you own every host handle and must
  Destroy it exactly once.

Interop.Native (static) - the raw [DllImport] declarations and ABI structs
(HostConfig, RenderConfig, PropertyInfo, AttributeConfig, ImageConfig,
TierConfig: 32 bytes each; VersionInfo: 96 bytes; Event: 40 bytes; Size is
always first). Use it to write your own binding; the C header
native/host/cycles_host.h and native/host/ABI.md are the contract.

Shared value types and enums (root namespace)
    Node(ulong Value), Image(ulong Value)  opaque per-host identifiers
    PropertyInfo(ValueType, Flags, Components, Count) + Kind, PropertyFlags
    Backend         WebGpu, CpuReference, Metal (osx-arm64 builds only),
                    Gpu (the backend-neutral request; the native host
                    resolves it, a host never reports it)
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

SCENE BUNDLES (LOAD AN ORACLE SCENE BUNDLE)
--------------------------------------------
A scene bundle is a self-contained scene description: one JSON document in the
cbcOracleScene version 1 format (scene singletons, created nodes, shader graphs,
meshes, attributes and images, addressed by their registered Cycles node,
socket and enum names) plus the raw files it names, all in one directory. The
same bundle can be rendered on any machine and backend, so it is a convenient
way to move a scene between processes, devices and tools.

OracleSceneBundle                                       (sealed)
    static OracleSceneBundle Load(string jsonPath)
                               the document and the raw files beside it
    static OracleSceneBundle FromJson(string json,
                               IReadOnlyDictionary<string, byte[]> files)
                               an in-memory bundle
    string Json, Id, Description, ColorConfig
    RenderSettings Render      film size and samples the document asks for
    uint Repeats               render.repeats (default 2)
    IReadOnlyList<string> FileNames
    byte[] ReadFile(string name)
    IReadOnlyDictionary<string, Node> Apply(CyclesScene scene)
                               builds the scene into a FRESH scene (by-name
                               tier) and returns the nodes by document handle
    string Save(string directory, string sceneFileName = "scene.json")
                               writes the document and every file into a new
                               or empty directory (never overwrites)
  Raw files are little-endian and tightly packed: pixel files are rows bottom
  to top in the declared format (float, float4, half, half4, byte, byte4,
  ushort, ushort4); a mesh may name positionsFile (float32 xyz), indicesFile
  (uint32) and attribute files (float32, or bytes for byte4 attributes)
  instead of inline arrays. File names are plain names beside the document.
  Apply throws FormatException for a malformed document and
  InvalidOperationException (message naming the entry and socket, inner
  NativeFailure) when the native host refuses a value. ColorConfig is not
  applied by Apply: call CyclesHost.ConfigureColor first when it is set.

    OracleSceneBundle bundle = OracleSceneBundle.Load("bench/scene.json");
    await using CyclesScene scene = new(CyclesBackend.Gpu);
    bundle.Apply(scene);
    FilmBuffer film = await scene.RenderAsync(bundle.Render);

BACKEND SELECTION
-----------------
Use CyclesBackend.Gpu for "the GPU of this platform" and CyclesBackend.
CpuReference for the CPU. A GPU render never falls back to the CPU: if no
usable GPU exists, scene creation or the render fails with a CyclesException.
CyclesBackend.Gpu resolves by platform:

    macOS on Apple Silicon (osx-arm64)    Backend.Metal  - Cycles' own Metal device
    macOS on Intel (osx-x64)              Backend.WebGpu - WGSL on WebGPU (which
                                                           reaches the GPU through Metal)
    Windows, Linux, Android (all RIDs)    Backend.WebGpu - WGSL on WebGPU

Which GPU backend renders is visible through scene.Backend and
GetCapabilities(); write backend-neutral code against CyclesBackend.

INTEL MACS (osx-x64). Supported with Intel graphics (validated on an Intel UHD
Graphics 630). AMD Radeon GPUs in Intel Macs (and AMD eGPUs) are NOT SUPPORTED
- rendering may work, but it has not been tested: on an AMD adapter the host
compiles every GPU stage with one invocation per workgroup as a precaution
(the conservative pipeline tier; slower, results within the practical
criterion), and the capability report says so. On Intel graphics a driver
workaround (intel-metal-surface-workgroup) is applied to two shading stages.
On a Mac with both Intel and AMD graphics (Intel MacBook Pros, 2016-2019) the
Intel GPU is used. The host asks the WebGPU runtime for IEEE floating point on
Metal (no fast math), which the GPU kernels need. That setting is process-wide:
if your application also renders its own WGSL through CodeBrix.WebGpu.Native,
pipelines it creates after a Cycles GPU scene exists are compiled the same way
(correct, possibly slightly slower).

WEBGPU ON WINDOWS. WebGPU reaches the GPU through Direct3D 12 by default, on
any GPU with a Direct3D 12 driver (every GPU vendor's Windows driver, including
the Qualcomm Adreno GPUs of Windows on ARM devices); shaders are compiled with
dxcompiler.dll from the host library's folder. Set the environment variable
CODEBRIX_CYCLES_WEBGPU_BACKEND=vulkan before the first scene is created to use
the Vulkan driver instead (for diagnostics or comparison; it needs a Vulkan
driver on the machine). There is no automatic fallback from one backend to the
other, nor to the CPU: if the selected one is unavailable, scene creation fails
with a CyclesException. GetCapabilities().Json names the backend in use.
Windows x64 is tested on Direct3D 12 and on Vulkan. On Intel GPUs with Direct3D 12
the shaders that use the light tree are compiled anew in every process (a
workaround for an Intel driver shader-cache bug), so the first render of such a
scene takes longer there; the image is unaffected. Windows ARM64 binaries are
built (the same code and layout) but not yet validated: on the one ARM64 GPU
tested so far (Qualcomm Adreno 680, Direct3D 12) some scenes render wrongly
because of driver problems (smooth-shaded surfaces render black, among others),
and that work is in progress. Environment variables named
CODEBRIX_CYCLES_WGSL_LOAD, CODEBRIX_CYCLES_WEBGPU_ADAPTER,
CODEBRIX_CYCLES_WEBGPU_ALLOW_NONCOMPLIANT, CODEBRIX_CYCLES_DXC_MAX_SHADER_MODEL,
CODEBRIX_CYCLES_WEBGPU_INSTANCE_FLAGS, CODEBRIX_CYCLES_WEBGPU_LOG,
CODEBRIX_CYCLES_DRIVER_WORKAROUNDS and CODEBRIX_CYCLES_DEBUG_OUTPUT are
maintainer diagnostics for such driver problems: never set them in an
application (a debug output mode replaces the rendered image).

WHAT DIFFERS BETWEEN THE GPU BACKENDS. The scene API, the edits, the events,
the film layout and the device-error contract are the same on every platform.
What a consumer can observe:
  - Feature coverage. Metal is Cycles' own device with upstream's GPU feature
    set: all cameras and passes and the integrator defaults render
    (GetCapabilities().Scope.RenderTimeRejections is empty). The WebGPU device does not yet cover all of Cycles (WEBGPU
    RENDER-TIME LIMITS below); it is being extended toward the same coverage.
    Check GetCapabilities().Scope instead of the platform.
  - First render. Metal compiles its kernels at run time the first time a
    machine renders (several seconds; the library carries the kernel sources
    as codebrix-cycles/source beside the native library) and caches them in
    the user's cache directory, so later renders start at once.
  - Pipeline tiers are a WebGPU concept: on Metal, the capability report's
    PipelineTier is null and the device report has no pipeline facts.
  - Results. The WebGPU device is validated against the Cycles CPU kernels
    (the CPU reference backend): its films agree with them within the
    practical criterion but are not bit-identical to them. The macOS
    Apple-silicon profile is Cycles' own Metal backend, a reference renderer in
    its own right: its films agree with the CPU reference backend in typical
    scenes, its repeat renders differ in the last bits (film accumulation uses
    atomics), and its hardware texture filtering can differ from the CPU's
    interpolation by more than that criterion on magnified textures. Those are
    characteristics of Cycles' Metal backend, not defects.

WEBGPU RENDER-TIME LIMITS. The WebGPU device rejects some scenes when the
render runs (the render fails with NativeStatus.Failed, not at edit time):
  - passes other than Combined, AO and the three shadow catcher passes
    (AddPass of any other type fails the render; data passes report "surface
    data passes not implemented"); see SHADOW CATCHER;
  - cameras other than perspective (orthographic, panorama);
  - the integrator defaults: switch off UseDenoise, UseAdaptiveSampling and
    UseGuiding before a WebGPU render (the light tree, UseLightTree, renders);
  - shadow linking (an object's BlockerShadowSet or ShadowSetMembership other
    than the defaults): "optional integration features not implemented";
  - a PrincipledBsdfNode with subsurface (a SubsurfaceWeight above the closure
    cutoff, 1e-5, or a linked SubsurfaceWeight input): "Unsupported feature:
    Principled BSDF subsurface scattering";
  - shader nodes the WebGPU device does not implement (for example true
    displacement, bevel, sky, IES and AOV output nodes; the
    bump node and the Gabor texture (GaborTextureNode, 2D and 3D) are
    implemented, but tangent and motion-triangle smooth-normal derivatives
    inside a bump height are rejected);
  - an AmbientOcclusionNode with OnlyLocal on an object that is not instanced
    while the scene also has instanced objects (the render fails with an
    unsupported-parameter error);
  - the classes listed in GetCapabilities().Scope.RenderTimeRejections.
The WebGPU device implements the AmbientOcclusionNode (Inside, OnlyLocal,
Samples, Distance, Normal) and fast GI ambient occlusion: IntegratorNode
AoBounces, AoFactor, AoDistance and AoAdditiveFactor, and ObjectNode.AoDistance,
as the CPU device does; AddPass(PassType.Ao, name) is written and ReadPass
returns it.
The CPU reference backend renders all of these.

DEVICE ERRORS. After a render fails with NativeStatus.Failed (scene.IsFaulted
is true), remove the cause (for example the extra pass or the non-perspective
camera on WebGPU, or an image wider or taller than Metal's 16384-pixel texture
limit on Metal) and render again: the native host recovers the GPU device
(WebGPU or Metal), uploads the whole scene again and raises a
RenderEventReceived status event that starts with "Device recovered"; a
successful render clears IsFaulted. Only a device that cannot recover (a lost
WebGPU device, or an error on the CPU device) keeps failing, with a message
that starts with "Host faulted": then dispose the scene and build a new one.

MOTION
------
Object motion is ObjectNode.Motion, camera motion CameraNode.Motion (with
CameraNode.Shuttertime), deformation MeshNode.SetMotionPositions(steps,
positions) (odd steps 3..129; the center step must equal the mesh positions
bit for bit); all need Integrator.MotionBlur = true. Motion can be set, changed
or cleared before or after a render.

EDITS AFTER A RENDER. Every edit made after a render (ObjectNode.Tfm and Motion,
CameraNode.Motion, deformation motion, MotionBlur, material values, lights,
SetTriangles, attributes, Smooth, new or removed objects) applies to the next
render exactly as if the scene had been built with it: on the CPU reference the
film is bit-identical to a new scene built with the edits. The native host
restores a baked mesh's authored positions first (the pinned Cycles static BVH
bakes single-user meshes with their object transform), so such an edit rebuilds
the BVH. The one refused edit: a baked volume (voxel data), its object's Tfm,
Motion or Geometry; that throws (NativeStatus.NotReady): remove and recreate the
object and volume.

SHADOW CATCHER
--------------
Mark the receiving object with ObjectNode.IsShadowCatcher = true. The scene
then gets the shadow catcher passes automatically; to read them, add them by
name with AddPass(PassType.ShadowCatcher, name),
AddPass(PassType.ShadowCatcherMatte, name) and
AddPass(PassType.ShadowCatcherSampleCount, name) and call ReadPass(name) after
the render. ShadowCatcher is the shadow and light the catcher receives as a
3-component multiplier for the backdrop; ShadowCatcherMatte is the RGBA of the
objects that are not catchers, to composite over the multiplied backdrop.
Combined still contains the catcher. Background.Transparent = true is the usual
setting; with an opaque background, Film.UseApproximateShadowCatcher = true
writes the matte with the shadow approximated into it (a background pass is
added for it). Lights contribute to the ShadowCatcher pass only when their own
object is also marked IsShadowCatcher, as in Cycles. Both backends render all
of this; on the WebGPU device a shadow catcher together with a volume (world
or object) fails with "WebGPU shadow catcher with volumes not implemented", and
caustics (MNEE) with a shadow catcher are refused as well. A shadow catcher
together with ambient occlusion (the AO node, the AO pass or the integrator's
fast-GI ambient occlusion) is not yet validated on the WebGPU device and is not
refused: its shadow stage writes the ambient-occlusion term without the catcher
handling the CPU applies, so the passes of such a scene may differ from the CPU
device; keep the two features apart until this note is gone.

CALLBACKS AND THREADING
-----------------------
RenderEventReceived handlers and RenderAsync progress reports run on a native
render thread (or the rendering thread) and only while a render runs, never
concurrently with each other for one scene. They must return quickly and must
NOT call the scene, its host or its nodes: the native host refuses every
re-entrant call (NativeStatus.NotReady). To stop a render from a handler,
cancel the RenderAsync CancellationToken; the native event callback returns
the request and the render stops with OperationCanceledException. A handler
that throws cancels the render and the render rethrows that exception. Renders
on one scene run one at a time; edits serialize with rendering.

DETERMINISM
-----------
On the CPU reference backend, rendering the same scene with the same settings
in the same process is bit-identical, and the three managed tiers produce
bit-identical films for the same scene. The CPU reference backend's arithmetic
is fixed rather than tuned to the machine that built the library: unmodified
Cycles CPU kernels at one instruction-set baseline, with every floating-point
operation rounded separately (no fused multiply-add, no division replaced by a
reciprocal multiply), so a CPU reference film does not depend on which computer
built the native library; the capability report states it
(backendScope.cpu.arithmetic in the JSON). GPU accumulation is not guaranteed to
be bitwise repeatable. The native determinism statement (native/host/ABI.md,
section Determinism; GetCapabilities().DeterminismReference) is
authoritative.


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
    Progress<RenderProgress> progress = new(p => Console.WriteLine($"{p.Stage} {p.CompletedSamples}/{p.TotalSamples} {p.Fraction:P0}"));
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

A texture from decoded pixels (decode the file with CodeBrix.Imaging, then hand
the pixels to the image API)
---------------------------------------------------------------------
    // rgba8: tightly packed width*height*4 bytes, row 0 at v = 0
    ImageTextureNode texture = material.Add<ImageTextureNode>();
    using (CyclesImage image = scene.CreateImage<byte>(width, height, ImageFormat.Byte4, rgba8))
        texture.Bind(image);                  // the binding keeps the pixels
    texture.Colorspace = "scene_linear_srgb";     // 8-bit sRGB colour data
    material.Connect(texture.ColorOutput, diffuse.ColorInput);
    mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner,
                      AttributeFlags.Standard, uvs);   // 2 floats per corner

    // non-colour data (normal, roughness, metallic maps): texture.Colorspace = "data"
    // float data: scene.CreateImage<float>(w, h, ImageFormat.Float4, floats)
    // half data:  scene.CreateImage<Half>(w, h, ImageFormat.Half4, halves)

An environment (HDRI) world with importance sampling
-----------------------------------------------------
    ShaderNode world = scene.CreateShader();
    EnvironmentTextureNode hdri = world.Add<EnvironmentTextureNode>();
    using (CyclesImage image = scene.CreateImage<float>(w, h, ImageFormat.Float4, floats))
        hdri.Bind(image);
    BackgroundShaderNode sky = world.Add<BackgroundShaderNode>();
    world.Connect(hdri.ColorOutput, sky.ColorInput);
    world.Connect(sky.BackgroundOutput, world.Output.SurfaceInput);
    scene.Background.Shader = world;
    // The world is light-sampled (with MIS) only through a background light:
    BackgroundLightNode worldLight = scene.CreateLight<BackgroundLightNode>();
    worldLight.UseMis = true;
    worldLight.MapResolution = 512;        // set it: 0 means 4096 x 2048 here
    worldLight.UsedShaders = [world];
    scene.CreateObject(worldLight);
Cycles uses the background light only for a world that varies with direction
(an environment texture or a procedural world); a constant world is lit by
forward rays alone. The importance map is built from the world on the render
device. Set the map resolution explicitly: on the WebGPU device the map's
evaluation buffers must fit one GPU storage binding, so a large map (0 means
4096 x 2048) needs an adapter whose limits allow a binding that large (WebGPU's
default is 128 MiB; see the pitfall EXCEEDING THE GPU STORAGE-BINDING LIMIT).
When the binding is too small, the render fails with the device's allocation
error.

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
- Keep RenderEventReceived handlers and IProgress<T> reports short: they run
  inside the native render loop.


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
- CALLING THE SCENE FROM A PROGRESS OR EVENT HANDLER. Handlers run inside the
  native render loop; any scene call fails with NotReady. System.Progress<T>
  posts reports to the thread pool, so they can arrive out of order; implement
  IProgress<T> yourself when the order matters.
- RENDERING A FAULTED SCENE WITHOUT REMOVING THE CAUSE. After a
  NativeStatus.Failed render, remove what the device rejected before rendering
  again; the recovered device rejects the same scene again. A "Host faulted"
  message means the device cannot recover: recreate the scene.
- EDITING A BAKED VOLUME. Edits of a volume (or of its object's transform,
  motion or geometry) after it was rendered throw; recreate them instead. Other
  edits after a render are applied (EDITS AFTER A RENDER).
- EXPECTING CHEAP EDITS OF SINGLE-USER MESHES. Moving, adding motion to or
  editing the data of a rendered single-user mesh rebuilds the BVH on the next
  render (the host keeps one extra copy of such meshes' authored attributes).
- ENABLING NON-COMBINED PASSES ON WEBGPU. Except for the shadow catcher passes
  the render fails; read the other passes on the CPU reference backend.
- READING THE REGISTRY AS A FEATURE LIST. A node type being registered (and
  generated) does not mean a GPU backend implements it; the CPU reference is
  the complete implementation.
- SHADOW TERMINATOR SHADING OFFSETS ABOVE 1 ON WEBGPU. The WebGPU backend
  implements ObjectNode.ShadowTerminatorGeometryOffset and
  ShadowTerminatorShadingOffset in 0..1; a shading offset above 1 fails the
  render with "shadow terminator shading offset outside [0, 1]".
- JUDGING ANDROID GPU OUTPUT AGAINST AN ON-DEVICE CPU RENDER. Android's C
  library (bionic) does not round acosf/asinf/atan2f/tanf correctly; the GPU
  backend follows correctly rounded math. A CyclesBackend.CpuReference render
  made on the Android device can therefore differ from the GPU film by more than
  the practical criterion in a few samples (emissive triangles, area lights)
  although the GPU is right. Compare against a CPU reference rendered on a
  platform with a correctly rounded C library (Linux glibc), or treat the
  on-device CPU film as a diagnostic.
- STORING 8-BIT sRGB TEXTURES AS "__builtin_srgb". With that colorspace the
  native image manager converts a byte image to half-float at load (twice the
  memory, 16-bit storage). "scene_linear_srgb" keeps the 8-bit pixels and
  decodes sRGB while sampling (what Blender does with its default colour
  configuration). Use "data" for normal, roughness and other non-colour maps.
- EXCEEDING THE GPU STORAGE-BINDING LIMIT. The WebGPU device keeps a scene's
  device data (images included) in one storage buffer binding, so a scene can
  use at most the adapter's largest storage binding (the device requests the
  adapter's limit, at most 2 GiB). A scene above it fails the render with
  "WebGPU device arena exhausted (storage binding limit)" and the sizes; some
  mobile GPUs offer only 128 MiB. Keep 8-bit textures 8-bit (previous item).
- USING SOCKET NAMES FOR LINKS. On the low-level tiers, Connect takes socket
  UI names ("Color", "BSDF"); the setters take native socket names ("color",
  "value"). The typed tier handles both for you.


BRINGING GLTF SCENES
====================
The package does not read glTF; load the file with a glTF library and build the
scene through the typed API (or write a scene bundle). The repository's example
client, tools/gltf/CycleGltfRender (GltfSceneConverter.cs), does exactly that
with CodeBrix.Graphics3D.Gltf2.MitLicenseForever and is a starting point to
copy: one MeshNode per glTF mesh (positions, indices, per-face shaders for
primitives, "N" vertex normals with Smooth, "uv" corner attribute with v
flipped), one CreateObject per node instance with Transform3x4.FromMatrix(the
node's world matrix), KHR_lights_punctual lights as sun/point/spot lights with
an emission shader, and the glTF camera with its Z axis flipped (glTF cameras
look along -Z, Cycles cameras along +Z).
It has two material mappings. The default approximates metallic-roughness
factors with Diffuse/Glossy/Emission/Transparent closures. The Blender mapping
builds the node graph Blender's glTF importer builds: a PrincipledBsdfNode fed
by ImageTextureNodes (base colour image in "scene_linear_srgb" times the factor
through a MixColorNode MULTIPLY; metallic = blue and roughness = green of the
metallic-roughness image in "data" through SeparateColorNode, times their
factors; a tangent-space NormalMapNode with strength = the normal scale). Its
tangent frame is the glTF TANGENT attribute set as the "tangent" (vector) and
"tangent_sign" (float, +1/-1) corner attributes with AttributeFlags.Standard;
the Normal Map node's empty attribute uses them. The occlusion texture is not
applied (Cycles has no occlusion input, and Blender's importer leaves it out of
the shading tree). glTF sampler mapping: REPEAT -> extension "periodic",
CLAMP_TO_EDGE -> "clamp", MIRRORED_REPEAT -> "mirror"; magFilter NEAREST ->
interpolation "closest", otherwise "linear"; Cycles has no mipmaps.
Textures need decoded pixels: the example decodes the glTF's images with
CodeBrix.Imaging (Image.Load<Rgba32>, rows flipped to bottom-first) and passes
the RGBA8 bytes to CreateImage. The WebGPU backend renders PrincipledBsdfNode
(every lobe except subsurface, see WEBGPU RENDER-TIME LIMITS).


NOT SUPPORTED ON ADRENO 640 (PIXEL 4 XL CLASS DEVICES)
======================================================
The Adreno 640 class is below this package's performance floor. The lists
below are what a triage on one such device found; anything not listed is
untested there, not supported.
Android devices with a Qualcomm Adreno 640 GPU (for example the Pixel 4 XL) are
not reference devices for this package. The ARM64 Android reference is an
Adreno 730 device. The policy:
- functionality that works on the reference device but fails on an Adreno 640
  is not supported there;
- functionality that takes more than three times as long as on the reference
  device is treated as a failure and is not supported there either.
The package does not block these features on an Adreno 640. They may render
wrongly, stop with a WebGPU error or a lost device, or take too long. Do not
build a product that depends on them on this GPU. Maintainers: the evidence is
in native/DEVICE.md, "Pixel 4 XL triage under the device reference policy".
This list says nothing about other GPU families (Mali, other Adreno
generations).

Not supported (fails on the Adreno 640):
- Transparent shadows and Transparent BSDF lighting. The transparent-shadow
  stage reports invalid data, and renders with transparent surfaces lose the
  device.
- Object motion blur (moving objects). It either fails or takes more than three
  times the reference device's time.
- Deformation motion blur (vertex motion). The render reports an invalid SVM
  stack.
- World (background) shaders built from texture, colour or utility nodes. The
  world evaluation reports an invalid SVM stack.
- Texture, colour and utility nodes in surface shaders. Renders report an
  invalid SVM stack, and evaluating their shader programs takes more than three
  times the reference device's time.

Not supported (more than three times the reference device's time):
- Emissive mesh (triangle) lights, for rendering and for light sampling.
- Point, sun and mesh lighting mixed with transmission and forward-hit MIS in
  one scene.
- Shadow terminator geometry and shading offsets.
- Principled BSDF materials: shader evaluation, bounces and renders each take
  more than three times the reference device's time.
- Scenes with motion-blur-capable objects or deforming meshes, even when
  rendered without motion.
- Managed .NET WebGPU rendering of simple scenes (glass, diffuse sun, textured
  plane) is more than three times slower than on the reference device.
  Pipeline compilation accounts for most of the difference.

Supported on the Adreno 640 within the policy:
- perspective cameras, static and with camera motion;
- closest-hit and shadow ray traversal;
- glass;
- point, sun, spot and area lighting with MIS;
- point-lamp emission;
- homogeneous volumes: absorption, isotropic, forward and backward scattering,
  closed and overlapping volumes, and a camera inside a volume.
Pipeline compilation is still much slower than on the reference device. Expect
compile-time warnings over the 20 s mobile budget.


WHAT THIS PACKAGE DOES NOT DO
=============================
- It does not encode or decode image or video files (PNG, JPEG, JPEG XL, TIFF,
  EXR, ...). It accepts and returns raw pixel buffers; use CodeBrix.Imaging.
- It does not import model formats (glTF and others); convert their data into
  nodes, meshes and attributes yourself (see BRINGING GLTF SCENES).
- It does not display images or present to a window or swap chain.
- It does not render passes other than Combined, AO and the shadow catcher passes,
  non-perspective cameras, denoising, adaptive sampling, path guiding or shadow
  linking on the WebGPU device (the CPU reference backend does). The
  integrator's light tree (UseLightTree, on by default) and light linking (an
  object's ReceiverLightSet and an emitter object's LightSetMembership) do
  render there, for lamps, mesh emitters and the background light.
- It does not render background-light portals, sky-texture sun guiding,
  more than one background light, or point, spot, area and sun lamps whose
  emission shader is not constant on the WebGPU device (the render fails with
  an error naming the feature). Environment-texture and procedural worlds with
  importance sampling, and mesh emitters with textured or node emission, do
  render there.
- Shadow caustics (manifold next-event estimation: objects with
  is_caustics_caster / is_caustics_receiver and lights with use_caustics)
  render on the WebGPU device through glass and refraction BSDFs from point and
  sun lights. It does not render shadow caustics through anisotropic glass,
  from spot, area or background lights, or together with light trees, light or
  shadow linking, ambient occlusion, volumes or motion blur, on the WebGPU
  device: the render fails with an error naming the feature. On Adreno 650
  GPUs (for example the Samsung Galaxy S20) scenes with shadow caustics are
  refused with an error message before rendering starts.
- It does not render texture, colour and utility shader nodes (noise, Voronoi,
  wave, magic, brick and Gabor textures, ramps, mixing, mapping and similar) on
  Adreno 650 GPUs (for example the Samsung Galaxy S20); such scenes are refused
  with an error message before rendering starts.
- It does not render the AmbientOcclusionNode, fast GI's additive mode
  (IntegratorNode.AoAdditiveFactor) or the AO pass on Adreno 650 GPUs; such
  scenes are refused with an error message before rendering starts (fast GI's
  replace mode renders there).
- It does not yet ship the native host library inside the package.
- It does not keep a persistent cache of compiled GPU pipelines; the WebGPU
  runtime it uses offers none. The first GPU render in each process compiles
  the pipelines its scene needs (seconds to a minute per material on the GPUs
  tested). Whether a later process starts warm depends only on the GPU driver:
  on Linux with the Mesa driver, Mesa's own shader cache (default
  ~/.cache/mesa_shader_cache_db; MESA_SHADER_CACHE_DIR moves it) makes later
  runs of an unchanged scene compile almost instantly; on the Android devices
  tested, every process compiles again. Within one process, repeated renders
  of a scene reuse its compiled pipelines unless an edit makes the scene use
  shader features it did not use before.


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
- Rendering on the GPU backend (WebGPU, or Metal on macOS arm64) and judging
  the film against the backend's reference, progress, cancellation and
  device-error recovery on the GPU:
  https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/tests/CodeBrix.Graphics3D.Cycles.Tests/GpuRenderTests.cs
- Loading, saving and rendering scene bundles:
  https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/tests/CodeBrix.Graphics3D.Cycles.Tests/OracleSceneBundleTests.cs
- Converting a glTF file into a scene (example client):
  https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/tools/gltf/CycleGltfRender/GltfSceneConverter.cs


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
Version:       CyclesVersion.NativeLibraryVersion, CyclesRevision, IsAbiCompatible
Progress:      scene.RenderEventReceived += (s, e) => Log(e.Kind, e.SamplesDone);  // no scene calls inside
Passes:        scene.AddPass(PassType.Depth, "Depth"); scene.Render(s); scene.ReadPass("Depth");
Remove:        scene.Remove(obj); scene.Remove(mesh);   // referrers first
Bundle:        OracleSceneBundle b = OracleSceneBundle.Load("scene.json"); b.Apply(scene); scene.Render(b.Render);
Re-upload:     img.Update<byte>(rgba8);
Motion:        mesh.SetMotionPositions(3, xyzSteps);    // before or after a render
Catalogue:     NodeType.Find("principled_bsdf").Inputs
By name:       scene.Host.SetFloats(node.Handle, "socket", [..]);
Raw ABI:       CyclesAbi.Create(Backend.CpuReference, 1, out ulong host); ... CyclesAbi.Destroy(host);
Errors:        catch (CyclesException e) when (e.NativeStatus == NativeStatus.Unsupported)

Target: .NET 10 or later
License: Apache-2.0


================================================================================
END OF AGENT-README
================================================================================
