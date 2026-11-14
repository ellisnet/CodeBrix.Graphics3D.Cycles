================================================================================
AGENT-README: CodeBrix.Graphics3D.Cycles
A Guide for AI Coding Agents - CONSUMING the
CodeBrix.Graphics3D.Cycles.Core.ApacheLicenseForever NuGet package and the
CodeBrix.Graphics3D.Cycles.<Platform>.ApacheLicenseForever native packages
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

STATUS: experimental. The packages are not yet published to nuget.org.


INSTALLATION
============
Reference the package for your platform. It brings in the managed library and
everything else it needs:

    dotnet add package CodeBrix.Graphics3D.Cycles.LinuxX64.ApacheLicenseForever

PackageIds:
    CodeBrix.Graphics3D.Cycles.Core.ApacheLicenseForever
        The managed assembly, with no native binaries.
    CodeBrix.Graphics3D.Cycles.<Platform>.ApacheLicenseForever
        The native scene host and its native dependencies for one platform:

        <Platform>       .NET RID
        LinuxX64         linux-x64
        LinuxArm64       linux-arm64
        LinuxRiscv64     linux-riscv64
        WindowsX64       win-x64
        WindowsArm64     win-arm64
        MacOSX64         osx-x64
        MacOSArm64       osx-arm64
        Android          android-x64 and android-arm64

A platform package is published once that platform has been validated. An
application that targets several platforms references several platform packages;
they share one Core package. All packages of one release have the same version.

Dependencies: every platform package depends on the Core package at exactly its
              own version. Every platform package except MacOSArm64, whose GPU
              backend is Cycles' own Metal device, also depends on the exact
              version of CodeBrix.WebGpu.Native.MitLicenseForever its native host
              was built and validated against; NuGet places codebrix_wgpu beside
              the native host. The Core package has no dependencies.
License:      Apache-2.0.
Requirements:
  - A 64-bit process (constructors throw PlatformNotSupportedException
    otherwise).
  - The native scene host library, named codebrix_cycles_host
    (libcodebrix_cycles_host.so on Linux, libcodebrix_cycles_host.dylib on
    macOS, codebrix_cycles_host.dll on Windows), resolvable by the .NET native
    library loader. The platform package provides it. Without one, the wrapper
    throws DllNotFoundException naming the package to add. Instead of a platform
    package, the process can find a host it built itself on its library search
    path, or the application can register a NativeLibrary.SetDllImportResolver
    for this assembly that loads it. The raw Interop.Native tier reports a missing
    library with the runtime's own message.
  - On Linux, system C and C++ runtime libraries (glibc, libstdc++) at least as new
    as those of the machine that built the package. On an older system the
    DllNotFoundException message quotes the loader's complaint about the version.
  - On Windows (x64 and ARM64), when not using a platform package, these files
    beside codebrix_cycles_host.dll:
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
                                                          // RenderEvent, RenderPassInfo, BakeSettings,
                                                          // DenoiserGuides
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
    SceneAttributesNode SceneAttributes
                               the scene's Time (seconds) and Frame, which a
                               SceneTimeNode reads (0 and 1 until set)
    TimeSpan RenderTimeLimit   wall-clock limit of later renders and bakes
                               (Cycles' time limit); Zero (default) renders
                               every sample, otherwise a render stops after
                               the limit with the samples done by then (such
                               a film is not reproducible)
    T Create<T>() where T : CyclesNode
                               objects, geometry, lights, shader graphs,
                               particle systems, passes (NodeCreation.Scene);
                               ArgumentException for any other type
    ShaderNode CreateShader()  a new, empty shader graph
    T CreateLight<T>() where T : LightNode
    HairNode CreateHair(ReadOnlySpan<float> xyz, ReadOnlySpan<float> radii, ReadOnlySpan<uint> firstKeys,
                        CurveShape shape = Ribbon, ShaderNode shader = null)
    PointCloudNode CreatePointCloud(ReadOnlySpan<float> xyz, ReadOnlySpan<float> radii,
                        ShaderNode shader = null)
    ParticleSystemNode CreateParticleSystem(ReadOnlySpan<Particle> particles)
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
    FilmBuffer Bake(ObjectNode target, BakeSettings settings)
    Task<FilmBuffer> BakeAsync(ObjectNode target, BakeSettings settings,
                               IProgress<RenderProgress> progress = null,
                               CancellationToken cancellationToken = default)
    BakeMap ReadBakeMap(ObjectNode target, BakeSettings settings)
                               see BAKING TEXTURES
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
    PassNode AddPass(PassType type, string name, PassMode mode)
                               the same with a denoising mode: PassMode.Noisy
                               = the undenoised version of a denoised pass
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

PASSES, LIGHT GROUPS, AOVS AND CRYPTOMATTE. Enable each pass with
AddPass(type, name) and read it with ReadPass(name) after the render; the
values come through Cycles' own pass accessor (for example Mist is already
1 - accumulated mist, Depth is 1e10 where nothing was hit).
  - Light passes: Emission, Background, Ao, DiffuseDirect/Indirect/Color,
    GlossyDirect/Indirect/Color, TransmissionDirect/Indirect/Color and the
    combined Diffuse, Glossy and Transmission passes.
  - Data passes: Depth, Position, Normal, Roughness, Uv (the mesh's standard
    "uv" attribute), ObjectId (ObjectNode.PassId), MaterialId
    (ShaderNode.PassId), Mist (FilmNode.MistStart, MistDepth, MistFalloff),
    SampleCount (the samples each pixel received; FilmNode.UseSampleCount is
    Cycles' own session state, set by every render: setting a different value
    throws), RenderTime (GPU backends do not measure it: it reads zero there).
  - Volume passes: VolumeDirect, VolumeIndirect and Volume (their sum), and
    VolumeMajorant and VolumeMajorantSampleCount (the volume integrator's
    majorant statistics).
  - Internal passes: AdaptiveAuxBuffer (adaptive sampling's work buffer) and
    VolumeScatter / VolumeTransmit (volume scattering guiding's buffers) are
    Cycles' own working passes; reading them is possible but they are not
    images.
  - PassNode.IncludeAlbedo keeps the surface colour in a Diffuse, Glossy or
    Transmission light pass (by default those passes are divided by it).
  - Motion and MotionWeight: written only when IntegratorNode.MotionBlur is
    false (Cycles' motion pass mode); object and camera motion transforms
    give the vectors.
  - Light groups: a pass of PassType.Combined whose Lightgroup names the
    group; lamps, emissive objects and the world join it through
    ObjectNode.Lightgroup or BackgroundNode.Lightgroup.
  - AOVs: an AovOutputNode in the shader graph and an AovColor or AovValue
    pass of the same name.
  - Cryptomatte: FilmNode.CryptomattePasses is a flag set
    (FilmCryptomattePasses.Object | Material | Asset) and CryptomatteDepth the
    number of four-component Cryptomatte passes per type, added in the order
    objects, materials, assets (for example "CryptoObject00",
    "CryptoObject01", "CryptoMaterial00", ...). The ids hash ObjectNode.Name,
    ShaderNode.Name and ObjectNode.AssetName: give them distinct names, or
    every object (material) shares one id.
  - Denoising features (the denoiser's guides, written with or without
    denoising): DenoisingAlbedo, DenoisingNormal,
    DenoisingSpecularAlbedo, DenoisingRoughness, DenoisingDepth,
    DenoisingBackwardMotion and DenoisingSpecularMotion (the last two with
    MotionBlur false), shaped by FilmNode.DenoisingPassFollowReflections and
    DenoisingPassUseAlbedoRoughnessWeighting.

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

BakeSettings(uint Width, uint Height, uint Samples, BakeType Type)  (record struct)
    string UvAttribute, string AovName, bool UseCamera, bool IncludeAlbedo   (init)
    int Components
BakeType        Combined, Emission, Background, AmbientOcclusion, Diffuse,
                DiffuseDirect, DiffuseIndirect, DiffuseColor, Glossy, GlossyDirect,
                GlossyIndirect, GlossyColor, Transmission, TransmissionDirect,
                TransmissionIndirect, TransmissionColor, Position, Normal, Uv,
                Roughness, ObjectId, MaterialId, Depth, Mist, AovColor, AovValue
                (= CBC_BAKE_*)
BakeMap         (from scene.ReadBakeMap)
    int Width, int Height, ReadOnlySpan<int> Triangles, ReadOnlySpan<float> Coordinates
    int GetTriangle(int x, int y)   -1 where no triangle covers the pixel centre

CyclesImage : IDisposable                               (sealed)
    Image Handle, uint Width, uint Height, ImageFormat Format, bool IsReleased
    void Update<T>(ReadOnlySpan<T> pixels)             same size and format
    void Update<T>(uint w, uint h, ImageFormat, ReadOnlySpan<T> pixels)
                               re-uploads raw pixels; every texture still
                               bound to the image renders them next time
    Dispose() releases YOUR reference; bound textures keep the pixels
    (textures bound before Dispose keep the pixels they had).

CyclesBackend   Gpu, CpuReference, Cpu (the CPU device with Embree ray
                tracing; CpuReference keeps BVH2 and stays the reference)
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
    bool PipelineCacheActive, string PipelineCacheDirectory (null when not
    active), string PipelineCacheReason (null without a device)
    bool CanDenoise, IReadOnlyDictionary<string, string> DenoiseDevices
    (backend -> denoiser device), IReadOnlyDictionary<string, string>
    BakingBackends, IReadOnlyList<string> BakeTypes
    IReadOnlyList<string> CompiledBackends, string GpuBackendName,
    bool GpuFallsBackToCpu (always false)
    IReadOnlyDictionary<string, BackendScope> BackendScopes, BackendScope Scope
    IReadOnlyDictionary<string, bool> OptionalLibraries (osl, embree, ...)
    IReadOnlyList<string> ImageFormats, Events
    IReadOnlyDictionary<string, int> PassTypes      registered pass types
    IReadOnlyDictionary<string, long> Limits        maxRenderWidth, maxMotionSteps...
    string DeterminismReference, string Json (the full native report)
BackendScope: Device, CameraTypes, Primitives, WrittenPasses, PassesNote,
    RenderTimeRejections (see RENDER-TIME REJECTIONS), Reference
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
  HairNode.SetCurves(ReadOnlySpan<float> xyz, ReadOnlySpan<float> radii,
    ReadOnlySpan<uint> firstKeys) (one radius per key, first key of each curve;
    set curve attributes after it), HairNode.SetShape(CurveShape),
    scene.SetCurveSubdivisions(0..4) (2^n ribbon sections per segment),
    HairNode.SetMotionKeys(uint steps, ReadOnlySpan<float> xyz,
    ReadOnlySpan<float> radii = default) / ClearMotionKeys()   deformation motion
  PointCloudNode.SetPoints(ReadOnlySpan<float> xyz, ReadOnlySpan<float> radii)
    (one radius per point; set point attributes after it; the Shader socket picks
    a material per point), PointCloudNode.SetMotion(uint steps, xyz, radii)
    (time-ordered, the center step equal to the points) / RemoveMotion()
  ParticleSystemNode.SetParticles(ReadOnlySpan<Particle>): what the Particle Info
    node reads through ObjectNode.ParticleSystem and ObjectNode.ParticleIndex
  MeshNode.SetTriangles(ReadOnlySpan<float> or ReadOnlySpan<Vector3>, indices)
  MeshNode.SetSubdivisionCage(positions, faceCorners, corners)   polygon faces
    for SubdivisionType Linear (see SCENE SETTINGS)
  MeshNode.SetMotionPositions(uint steps, ReadOnlySpan<float> or
  ReadOnlySpan<Vector3>) / ClearMotionPositions()   deformation motion
  (before or after a render; see MOTION)
  ImageTextureNode / EnvironmentTextureNode .Bind(CyclesImage) / .Unbind()
  ObjectNode.RayVisibility / BackgroundNode.RayVisibility   the Visibility
    socket as RayVisibility flags (Camera, Transmit, Diffuse, Glossy,
    VolumeScatter, ShadowOpaque, ShadowTransparent, Shadow, Raycast, All)
  IntegratorNode.DenoiserGuides   the DenoiserPasses socket as DenoiserGuides
    flags (Open Image Denoise reads Albedo and Normal, the default)

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
  create directly (buffer_params, buffer_pass, denoise_params); they are
  catalogued for completeness.

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
    void SetRenderTimeLimit(double seconds)   0 = every sample
    void SubdivisionMesh(Node mesh, positions, faceCorners, corners)   a subdivision cage
    bool Bake(Node, in BakeSettings, Span<float>, out required, out components)
    bool BakeMap(Node, in BakeSettings, Span<int>, Span<float>, out required)
    static ConfigureColor(string path)       process-wide OCIO config
    static ConfigureTiers(PipelineTier, string recordDirectory, PipelineTierFlags)
    static ConfigurePipelineCache(PipelineCacheMode, string directory = null)
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
                 Cancel / IsRendering / ConfigureColor / ConfigureTiers /
                 ConfigurePipelineCache
    NativeStatus Version(out uint abi, out string library, out string revision)
    NativeStatus ResolveBackend / GetBackend / CapabilitiesJson / DeleteNode /
                 UpdateImage / MotionPositions / SetEventCallback /
                 PassFloats / RenderPassesJson / Bake / BakeMap
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
                    resolves it, a host never reports it), Cpu (the CPU
                    device with Embree; capability name cpu-embree)
    NativeStatus    Ok, Invalid, Unsupported, Failed, Cancelled, NotReady,
                    SmallBuffer
    ValueKind       Unsupported, Boolean, Int, UInt, UInt64, Float, Color,
                    Vector, Point, Normal, Point2, String, Enum, Transform,
                    Node, Closure, Float4, Byte4
    PropertyFlags   None, Array, Linkable, Internal, Readable, Writable
    AttributeValue, AttributeDomain, AttributeFlags (geometry attributes)
    CurveShape      Ribbon, Thick, ThickLinear
    RayVisibility   object and world ray visibility flags
    ImageFormat     Float, Float4, Half, Half4, Byte, Byte4, UShort, UShort4
    PipelineTier, PipelineTierFlags (WebGPU pipeline-tier selection)
    PipelineCacheMode (the persistent GPU pipeline cache's switch)
  Every enum value equals the C constant of the same meaning.

SCENE BUNDLES (LOAD AN ORACLE SCENE BUNDLE)
--------------------------------------------
A scene bundle is a self-contained scene description: one JSON document in the
cbcOracleScene version 1 format (scene singletons, created nodes, shader graphs,
meshes, curves, attributes and images, addressed by their registered Cycles node,
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
CpuReference for the CPU. CyclesBackend.Cpu is the CPU device with Embree ray
tracing (Cycles' automatic BVH): usually faster on large scenes; its films agree
with CpuReference closely but not bit for bit (a different BVH visits primitives
in another order, and a rare single pixel can differ visibly), and
CpuReference (BVH2) stays the reference the GPU backends are validated
against. A GPU render never falls back to the CPU: if no
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
dxcompiler.dll from the host library's folder. There is no automatic fallback
to another graphics API, nor to the CPU: if Direct3D 12 is unavailable, scene
creation fails with a CyclesException. GetCapabilities().Json names the
graphics API in use.
Windows x64 is tested on Direct3D 12 and on Vulkan. On Intel GPUs with Direct3D 12
the shaders that use the light tree are compiled anew in every process (a
workaround for an Intel driver shader-cache bug), so the first render of such a
scene takes longer there; the image is unaffected. Windows ARM64 binaries are
built (the same code and layout) but not yet validated: on the one ARM64 GPU
tested so far (Qualcomm Adreno 680, Direct3D 12) some scenes render wrongly
because of driver problems (smooth-shaded surfaces render black, among others),
and that work is in progress.

WHAT DIFFERS BETWEEN THE GPU BACKENDS. The scene API, the edits, the events,
the film layout and the device-error contract are the same on every platform.
What a consumer can observe:
  - Feature coverage. Metal is Cycles' own device with upstream's GPU feature
    set. The WebGPU device renders the same scenes, with the few exceptions
    listed in RENDER-TIME REJECTIONS (below), each refused by name. Check
    GetCapabilities().Scope instead of the platform.
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

WHAT RENDERS WHERE. Every backend renders the same scene API: Cycles' cameras,
closures, shader nodes, lights, world, volumes, subsurface scattering, curves,
point clouds, motion blur, passes, light groups, AOVs, Cryptomatte, denoising,
light and shadow linking, shadow caustics, the shadow catcher and baking
(SCENE SETTINGS below says how to reach each). The exceptions are few and each
is refused by name when the render runs, never approximated: they are listed in
RENDER-TIME REJECTIONS, and GetCapabilities().Scope.RenderTimeRejections lists
the ones of the scene's backend.

DEVICE ERRORS. After a render fails with NativeStatus.Failed (scene.IsFaulted
is true), remove the cause (for example a custom
camera on WebGPU, or an image wider or taller than Metal's 16384-pixel texture
limit on Metal) and render again: the native host recovers the GPU device
(WebGPU or Metal), uploads the whole scene again and raises a
RenderEventReceived status event that starts with "Device recovered"; a
successful render clears IsFaulted. Only a device that cannot recover (a lost
WebGPU device, or an error on the CPU device) keeps failing, with a message
that starts with "Host faulted": then dispose the scene and build a new one.

SCENE SETTINGS
--------------
Every setting is a typed property of a generated node (TIER 2); what each one
means is Cycles' meaning. This list says how to reach the features and what is
special here.

Cameras (scene.Camera):
  - CameraType Perspective (Fov, radians), Orthograph or Panorama. The view is
    the camera's viewplane in camera space: by default 2 units across the
    film's shorter side, so scale Matrix (or set the viewplane, below) to
    change an orthographic view's size.
  - PanoramaType: Equirectangular (LatitudeMin/Max, LongitudeMin/Max),
    EquiangularCubemapFace, Mirrorball, FisheyeEquidistant (FisheyeFov),
    FisheyeEquisolid (FisheyeLens, FisheyeFov, Sensorwidth, Sensorheight),
    FisheyeLensPolynomial (FisheyeFov, FisheyePolynomialK0..K4) and
    PanoramaCentralCylindrical (CentralCylindricalRangeUMin/UMax/VMin/VMax).
  - Depth of field: Aperturesize, Focaldistance, Blades, Bladesrotation and
    ApertureRatio (anamorphic bokeh). Clipping: Nearclip, Farclip.
  - Lens shift and sensor fit: ViewplaneLeft/Right/Bottom/Top. A viewplane you
    set is kept for every render; setting all four to 0 returns to the
    automatic one (for a W x H film: +-W/H by +-1 when W >= H, else +-1 by
    +-H/W), which a render otherwise writes there. FullWidth and FullHeight are
    the render's size: a different value throws (NativeStatus.Invalid).
  - Render region: BorderLeft/Right/Bottom/Top (fractions of the film,
    bottom-left origin; 0..1, the default, or all 0 is the whole film).
    Only the region renders; the film returned is the whole film, zero
    outside the region, every pass too. Its pixel edges are the fractions
    times the film size, rounded to the nearest pixel.
  - Stereo: StereoEye (Left or Right), InterocularDistance,
    ConvergenceDistance, UseSphericalStereo, UsePoleMerge,
    PoleMergeAngleFrom/To.
  - Motion: Motion (transforms), Shuttertime, MotionPosition (Start, Center,
    End), RollingShutterType and RollingShutterDuration, ShutterCurve,
    UsePerspectiveMotion with FovPre and FovPost (see MOTION).
  - ViewportCameraBorder* map a viewport onto the camera; keep the default
    0..1 for renders. OffscreenDicingScale scales the dicing rate of
    subdivision faces outside the view.

Film (scene.Film): Exposure, FilterType (Box, Gaussian, BlackmanHarris) and
FilterWidth (pixels), PassAlphaThreshold, MistStart/MistDepth/MistFalloff, the
Cryptomatte settings (see PASSES) and the denoising-pass options. DisplayPass
and ShowActivePixels are viewport display settings with no effect on the film
or the passes: setting a different value throws (NativeStatus.Invalid).

Sampling (scene.Integrator):
  - RenderSettings.Samples is the sample count; AaSamples follows it (setting
    a different value throws). SamplingPattern (TabulatedSobol, SobolBurley,
    BlueNoisePure, BlueNoiseRound, BlueNoiseFirst), Seed, ScramblingDistance.
  - Adaptive sampling: UseAdaptiveSampling, AdaptiveThreshold (0 = chosen
    from the sample count) and AdaptiveMinSamples (0 = chosen from the
    threshold). Pixels stop when their noise is below the threshold.
  - UsePixelJitter (off by default) replaces the per-sample pixel filter with
    one sub-pixel offset per render, for temporal anti-aliasing (successive
    renders of a scene step through a Halton sequence);
    UseCustomPixelJitterSample with CustomPixelJitterSample (two floats)
    chooses the offset.
  - UseSampleSubset with SampleSubsetOffset and SampleSubsetLength renders only
    those samples of RenderSettings.Samples (for splitting one image's samples
    between renders). scene.RenderTimeLimit stops a render early.
  - Bounces and path controls: MinBounce, MaxBounce and the per-type maxima
    (MaxDiffuseBounce, MaxGlossyBounce, MaxTransmissionBounce, MaxVolumeBounce,
    TransparentMinBounce, TransparentMaxBounce), FilterGlossy (blurs glossy
    reflections after blurry bounces: less noise, less accuracy; 1 by default,
    as in Cycles, 0 switches it off),
    CausticsReflective and CausticsRefractive (off removes those caustic
    paths), SampleClampDirect, SampleClampIndirect, LightSamplingThreshold,
    UseLightTree (on by default).
  - Path guiding (UseGuiding and the other guiding settings) is off by default
    and is not rendered by the WebGPU device (RENDER-TIME REJECTIONS).

Denoising (scene.Integrator): UseDenoise (off by default), DenoiserType
Openimagedenoise (Dlss is refused: RENDER-TIME REJECTIONS), DenoiserPrefilter,
DenoiserQuality, DenoiseStartSample and DenoiserGuides (the guide passes:
Albedo, SpecularAlbedo, Normal, Roughness, Depth, BackwardMotion,
SpecularMotion; Open Image Denoise reads Albedo and Normal, the default; a
DenoiserPasses value with a bit outside these fails a denoising render).
DenoiseUseGpu lets Open Image Denoise use the GPU where it can (Metal); the
CPU and WebGPU backends denoise on the CPU (GetCapabilities().DenoiseDevices).
DenoiserUpscaleFactor other than 1 is refused on the CPU and WebGPU backends
(RENDER-TIME REJECTIONS).

Lights (CreateLight<T>, placed by an object; see A light): every LightNode has
Strength, CastShadow, UseMis, UseCaustics, MaxBounces, IsEnabled, Normalize
and UsedShaders (the emission shader; a LightFalloffNode or an IesLightNode in
it shapes the emission: pass the IES file's text in IesLightNode.Ies).
PointLightNode Radius and IsSphere; SpotLightNode Angle and Smooth (plus the
point radius); AreaLightNode Sizeu, Sizev, Ellipse, Spread and IsPortal (see
the HDRI example); SunLightNode Angle; BackgroundLightNode MapResolution.

Objects (ObjectNode): Tfm, Motion, Visibility / RayVisibility, Color, Alpha,
RandomId (the Object Info node's Random), PassId, AssetName, UseHoldout (the
object cuts a hole in the film's alpha; the HoldoutNode does it per material),
IsShadowCatcher, IsCausticsCaster and IsCausticsReceiver (shadow caustics,
with the light's UseCaustics), DupliGenerated and DupliUv (the Texture
Coordinate node's From Instancer values), HideOnMissingMotion, AoDistance,
ShadowTerminatorShadingOffset and ShadowTerminatorGeometryOffset, Lightgroup,
ParticleSystem and ParticleIndex (the Particle Info node).
  - Light linking: an emitter object's LightSetMembership is a bit mask of
    light sets, a receiver object's ReceiverLightSet one set number; the
    emitter lights the receiver when bit ReceiverLightSet of its membership is
    set. Defaults: every bit, set 0 (everything lit).
  - Shadow linking: a light object's ShadowSetMembership is a bit mask, a
    blocker object's BlockerShadowSet a set number; the blocker casts that
    light's shadow when that bit is set. Defaults: every bit, set 0.

World (scene.Background): Shader, Transparent (alpha 0 where the world is
seen), TransparentGlass and TransparentRoughnessThreshold, UseShader,
Visibility / RayVisibility, Lightgroup; a BackgroundLightNode samples a
varying world (see the HDRI example).

Geometry attributes (GeometryBaseNode.SetAttribute): with
AttributeFlags.Standard these names are Cycles' standard attributes:
  - meshes: P, N (vertex or corner normal), uv, generated, tangent,
    tangent_sign, pointiness (per vertex) and random_per_island (per face),
    which the Geometry node's Pointiness and Random Per Island outputs read
    (compute them yourself; Cycles does not);
  - curves (HairNode): uv, generated, radius, curve_intercept (per key),
    curve_length and curve_random (per curve), for the Hair Info node;
  - point clouds: uv, generated, radius, point_random (the Point Info node).
  Any other name is a custom attribute, read by the Attribute node.
  A node that asks for an attribute by name (AttributeNode, UvMapNode) gets
  the attribute of that name, also when it is one of the names above set
  without AttributeFlags.Standard; a node that asks for a standard attribute
  (the Texture Coordinate node's UV, the Geometry node's Pointiness) gets the
  one set with AttributeFlags.Standard, else an attribute with that name, value
  type and domain.
  AttributeNode, ImageTextureNode and EnvironmentTextureNode have Missing and
  MissingAlpha: the colour and alpha the node returns when its attribute or
  image does not exist (by default alpha 1 with black for the attribute and
  magenta 1, 0, 1 for the textures).
  PointCloudNode.RenderAs stays Points (GaussianSplats is refused:
  RENDER-TIME REJECTIONS).

Shader graphs: a link between different value types (a colour into a float
input, a float into a colour or vector input, a value into a shader input)
gets Cycles' own conversion node, as in Blender; the Convert*Node classes do
the same explicitly. A shader output links only into a shader input.
Displacement: ShaderNode.DisplacementMethod Bump, True or Both, with a
DisplacementNode or VectorDisplacementNode into OutputNode.DisplacementInput;
BumpNode for bump mapping inside a graph.

Volumes: a volume shader (ScatterVolumeNode, AbsorptionVolumeNode,
PrincipledVolumeNode, VolumeCoefficientsNode, emission) into
OutputNode.VolumeInput of a closed mesh's shader. Settings:
ShaderNode.VolumeSamplingMethod, VolumeInterpolationMethod and VolumeStepRate;
IntegratorNode.VolumeRayMarching (biased ray marching), VolumeMaxSteps,
VolumeStepRate and MaxVolumeBounce. VolumeNode is Cycles' volume geometry for
voxel grids (OpenVDB), which this package does not read: use a mesh. The
VolumeInfoNode reads voxel grids, so here it returns its defaults.

Subdivision surfaces: MeshNode.SetSubdivisionCage(positions, faceCorners,
corners) gives a mesh polygon faces (a control cage) instead of triangles;
with SubdivisionType Linear every render tessellates them with Cycles'
adaptive dicing (SubdDicingRate, in pixels of the render camera with
SubdAdaptiveSpace Pixel or in object units with Object, and SubdMaxLevel).
Subdivision attributes go on the cage with
AttributeFlags.Subdivision. Catmull-Clark needs OpenSubdiv and is refused.

Subsurface: SubsurfaceScatteringNode or PrincipledBsdfNode with the
RandomWalk, RandomWalkLegacy or Burley (Christensen-Burley) method, also on a
thin-walled PrincipledBsdfNode.

MOTION
------
Object motion is ObjectNode.Motion, camera motion CameraNode.Motion (with
CameraNode.Shuttertime), deformation MeshNode.SetMotionPositions(steps,
positions) (odd steps 3..129; the center step must equal the mesh positions
bit for bit); all need Integrator.MotionBlur = true. Motion can be set, changed
or cleared before or after a render. Curves and point clouds have their own
deformation motion (HairNode.SetMotionKeys, PointCloudNode.SetMotion).
Camera exposure: CameraNode.MotionPosition puts the frame's moment at the
start, centre or end of the shutter; RollingShutterType Top with
RollingShutterDuration exposes the rows one after another; ShutterCurve (a
float array) weights the exposure over the shutter time; UsePerspectiveMotion
with FovPre and FovPost gives a perspective camera's field of view at shutter
open and close (a zoom during the exposure).

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
of this, also together with a volume (world or object) and with shadow
caustics (MNEE) and with ambient occlusion (the AO node, the AO pass and the
integrator's fast-GI ambient occlusion).

BAKING TEXTURES
---------------
scene.Bake bakes one mesh's UV layout into a raw float film through Cycles' bake
kernel, one BakeType at a time. The CPU backends and the WebGPU device bake,
every BakeType on each, also of meshes with deformation motion (baked at the
mesh's static positions, as Cycles bakes). On the WebGPU device a bake of a
mesh whose UV layout covers triangles with no area throws NativeFailure
(status 3) naming it, for every BakeType but Position, Background and Normal
(Normal through a shader with a bump is refused too); a flat Normal bake of
such a texel fails on every backend (its normal is not a number). A bake never
falls back to the CPU. On Apple Silicon it runs Cycles' Metal bake kernel,
which is not validated yet.
GetCapabilities().BakingBackends states which (validated, unvalidated),
BakeTypes the types.

    // The mesh needs a UV layout: a "uv" corner attribute (standard), or any
    // float2 corner or vertex attribute named by BakeSettings.UvAttribute.
    mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, uvs);
    ObjectNode target = scene.CreateObject(mesh);

    FilmBuffer color = await scene.BakeAsync(target, new BakeSettings(1024, 1024, 64, BakeType.Combined));
    FilmBuffer albedo = scene.Bake(target, new BakeSettings(1024, 1024, 16, BakeType.DiffuseColor));
    FilmBuffer light = scene.Bake(target, new BakeSettings(1024, 1024, 256, BakeType.Diffuse) { IncludeAlbedo = true });
    FilmBuffer normal = scene.Bake(target, new BakeSettings(1024, 1024, 1, BakeType.Normal));

Output: film.Components floats per pixel (BakeSettings.Components gives it up
front), row 0 at v = 0, the same order as CreateImage takes, so a baked image binds
as a texture unchanged. Pixel (x, y) samples UV ((x + .5) / w, (y + .5) / h), jittered
over the texel from the second sample on.

Types: Combined (RGBA; alpha 1 where covered, 0 elsewhere), Emission, Background (the
world, in the direction of the surface point seen from the world origin, not the
surface's shading: a closed mesh around the origin bakes the environment),
AmbientOcclusion, Diffuse/Glossy/Transmission with Direct, Indirect and Color variants
(the light variants divided by the colour unless IncludeAlbedo), Position
(world space, always one sample), Normal (world space), Uv, Roughness, ObjectId, MaterialId,
Depth, Mist, AovColor and AovValue (set BakeSettings.AovName to an AOV pass the scene
already has). BakeSettings.UseCamera points the view direction at the scene camera
instead of along the normal, which matters for view-dependent shading.

Empty pixels: a pixel no triangle covers reads zero (Depth 1e10, Mist 1). There is no
margin fill. Texture filtering across a UV seam will read those pixels, so an
application that needs a margin dilates its image from scene.ReadBakeMap(target,
settings), which renders nothing and works on any backend: map.GetTriangle(x, y) is
-1 where uncovered, and map.Coordinates holds u, v and their per-pixel steps, where the
point is u * V0 + v * V1 + (1 - u - v) * V2. Where UV triangles overlap the lowest
triangle index wins, so give each surface its own UV area for lighting bakes.

A bake is a render for the scene's purposes: it runs one at a time with renders,
raises RenderEventReceived, honours cancellation, and uses the integrator's settings. It
leaves the scene as it was (ObjectNode.IsBakeTarget included; the bake sets it itself) and
clears the last render's passes (ReadPass throws InvalidOperationException until a
render succeeds), and a later render is unaffected. Pass nodes other than AOVs take no
part in a bake. With UseDenoise the Combined bake is denoised as an image of the UV
layout, so values bleed across island edges and into empty pixels: for bakes
you will sample, denoise with care. In a scene with an approximate shadow catcher and an
opaque background a Combined bake is the world's, not the object's, as Cycles bakes it: each
covered texel holds the background in the direction of its surface point seen from the origin,
and every alpha is 1. Baked normals are world space; tangent or object
space is yours to convert.

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
authoritative. Exception on Intel integrated graphics: see KNOWN DRIVER
LIMITATION: HETEROGENEOUS VOLUMES ON INTEL GRAPHICS.


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
error. A world with a volume (a closure on the world shader's Volume input)
needs no background light of its own, and a background light the scene has
need not list the world shader in UsedShaders for the volume to render.

Light portals guide the world's light samples through openings such as
windows: an area light with IsPortal = true, placed by its object's transform
(the portal faces the object's -Z axis, towards the interior it lights;
SizeU and SizeV give its extent, Ellipse makes it round). A portal emits
nothing itself and needs the background light object above; with a portal
in the scene Cycles uses that light for any world, constant or not and with or
without UseMis.

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
        <PackageReference Include="CodeBrix.Graphics3D.Cycles.LinuxX64.ApacheLicenseForever" Version="*" />
      </ItemGroup>
    </Project>

Program.cs: the "Render a lit triangle with the typed facade" example above.
Run it with `dotnet run`; the platform package puts the native host beside the
application. Change the platform in the package name to match the target.


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
- GPU pipelines are compiled the first time a scene needs them (seconds to a
  minute per material on the GPUs tested). On Linux and Android (Vulkan) they are
  kept on disk, so a later process or host does not compile them again. Within
  one process, repeated renders of a scene reuse its compiled pipelines unless
  an edit makes the scene use shader features it did not use before.
  - The cache changes compile time only, never a film, and a cache problem is
    never an error: a missing, damaged or out-of-date file is ignored and rebuilt.
  - Where: CyclesHost.ConfigurePipelineCache(PipelineCacheMode.Default, directory)
    chooses the directory, before the host is created and for the whole process.
    Without a directory it is the record directory of CyclesHost.ConfigureTiers
    if you set one, else on Linux the user's cache directory
    (~/.cache/cycles/webgpu-pipelines, moved as the XDG base-directory rule says).
  - Android has no user cache directory: pass your app's cache or files
    directory, or the app compiles every time (it still renders normally).
  - CyclesHost.ConfigurePipelineCache(PipelineCacheMode.Off) turns it off; use
    that when you measure compile times.
  - GetCapabilities() says what happened: PipelineCacheActive,
    PipelineCacheDirectory, PipelineCacheReason ("active", "off", "no-directory",
    "not-offered" on macOS on Intel and Windows, "backend" on Metal and the CPU).
  - It grows as new shader variants are compiled, up to 1 GiB per GPU; past that
    the least recently used pipelines go first. Delete it whenever you like;
    several processes may share the directory.


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
- EXPECTING DISTINCT CRYPTOMATTE IDS WITHOUT NAMES. The ids hash the object
  and shader names (ObjectNode.Name, ShaderNode.Name, ObjectNode.AssetName);
  unnamed nodes all share one id.
- EXPECTING A MOTION PASS WITH MOTION BLUR. Cycles writes the motion pass
  only when IntegratorNode.MotionBlur is false; with motion blur on, the
  Motion pass reads zero on every backend.
- READING THE REGISTRY AS A FEATURE LIST. The registry describes Cycles' node
  types and sockets, not what a backend renders: a few registered types and
  settings are refused when a scene uses them (RENDER-TIME REJECTIONS), and
  some sockets are Cycles' own state that a render sets (FilmNode.UseSampleCount,
  CameraNode.FullWidth and FullHeight, IntegratorNode.AaSamples).
- SETTING A SOCKET THE RENDER OWNS. CameraNode.FullWidth/FullHeight and
  IntegratorNode.AaSamples come from RenderSettings, FilmNode.UseSampleCount
  is Cycles' session state, and FilmNode.DisplayPass and ShowActivePixels are
  viewport settings: setting a different value throws (NativeStatus.Invalid).
  Set the size and samples in RenderSettings; read the sample count through a
  SampleCount pass.
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
- TRUSTING ONE RENDER OF A HETEROGENEOUS VOLUME ON AN INTEL GPU. A driver fault
  could compute a path wrongly and put a NaN in the film; see KNOWN DRIVER
  LIMITATION: HETEROGENEOUS VOLUMES ON INTEL GRAPHICS.


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
the RGBA8 bytes to CreateImage. Every backend renders PrincipledBsdfNode
(every lobe, thin-walled too; subsurface with the random walk and
Christensen-Burley methods).


KNOWN DRIVER LIMITATION: HETEROGENEOUS VOLUMES ON INTEL GRAPHICS
================================================================
*** READ THIS IF YOU RENDER VOLUMES WHOSE DENSITY, COLOUR OR EMISSION VARIES ***
*** IN SPACE ON AN INTEL GPU.                                                ***

On Intel integrated graphics with the Mesa Vulkan driver (found on
12th-generation Intel UHD Graphics under Linux; Intel Xe Graphics under Android
x64 shows the same behaviour at a similar rate), a WebGPU render of a scene with
a heterogeneous volume could, rarely and unpredictably, compute one light path
wrongly. Two renders of the same scene can then differ in a pixel, and the wrong
path can put a non-finite value (NaN) into the film.
- It is a fault of the GPU driver or the hardware, not of the scene and not of
  this package: the same compute shader with the same inputs, run outside this
  package, shows the fault on the Linux machine's GPU and never on a software
  Vulkan implementation.
- The package now integrates heterogeneous volumes in smaller GPU kernels, and
  since then the fault has not been observed in repeated testing on that Linux
  machine (before, a few evaluations in a hundred of the affected kernel). The
  driver fault itself is not fixed and the package does not detect it, so it
  may still occur with other Intel drivers or GPUs.
- It has been observed only in scenes with heterogeneous volumes. Homogeneous
  volumes and scenes without volumes have not shown it.
- What an application can do on such a machine: check the returned film for
  non-finite values and render again when it finds one; render a frame that
  matters twice and compare; or render heterogeneous volumes on the CPU backend.


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


RENDER-TIME REJECTIONS
======================
A scene that uses one of these fails when it renders, with a CyclesException
whose message names it (NativeStatus.Unsupported from the native host,
NativeStatus.Failed from the GPU device); nothing is approximated, and a GPU
render never falls back to the CPU. Remove the cause and render again (see
DEVICE ERRORS). GetCapabilities().Scope.RenderTimeRejections lists the entries
of the scene's backend in exactly these words, and
GetBinaryCapabilities().BackendScopes those of every backend in the library.

Every backend:
  - Catmull-Clark subdivision surfaces (they need OpenSubdiv, which this library does not include)
  - the dlss denoiser
  - point clouds rendered as Gaussian splats
The CPU backends and the WebGPU backend:
  - a denoiser upscale factor other than 1
The WebGPU backend:
  - dispersion (a Principled BSDF whose Transmission Dispersion Scale and Transmission Weight are each linked or not 0)
  - custom (OSL) cameras
  - path guiding (the integrator's use_guiding) and its passes
  - curves and points with a Both displacement shader
  - a Radial Tiling node with at most two sides and a roundness strictly between 0 and 1
  - scenes whose device data does not fit the GPU (there is no host-memory fallback)
  - BVH time steps
  - volume velocity motion blur
  - sparse or tiled images
  - area lights whose axis length underflows to zero, with a spread of zero or below 1e-5, with an axis of infinities, or reached by the light tree at a transmissive surface
  - background light portals of zero extent
  - bakes of texels on triangles with no area, for every bake type but Position, Background and Normal (Normal through a shader with a bump is refused too)
  - texture, colour and utility shader nodes on Adreno 650 GPUs
  - ambient occlusion shader nodes and ambient occlusion shadow rays on Adreno 650 GPUs
  - Boolean Math, Integer Math and Get Vector Component shader nodes with a linked input
  - shadow catchers with transparent shadows on Adreno 650 GPUs
  - shadow caustics (manifold next-event estimation) on Adreno 650 GPUs

Why. OSL, OpenSubdiv and OpenVDB/NanoVDB (whose sparse images and velocity
grids two of the WebGPU entries concern) are outside this package. Dispersion
(a Principled BSDF with TransmissionDispersionScale and TransmissionWeight
each linked or not 0) renders on the CPU and Metal backends and is not
implemented by the WebGPU device, whose message is "WebGPU dispersion not
implemented"; there, leave TransmissionDispersionScale at 0 and unlinked (with
TransmissionWeight at 0 and unlinked nothing disperses, and the scale is not
refused). The dlss denoiser is not part of
the library (use Openimagedenoise). A Gaussian splat needs radiance, scale and
rotation attributes of value kinds this API cannot create, and Cycles would
render the cloud as plain points. BooleanMathNode, IntegerMathNode and
GetVectorComponentNode are not implemented by the WebGPU device; with constant
inputs Cycles folds them away and every backend renders them, with a linked
input the CPU and Metal backends render them. Path
guiding is not part of Cycles' Metal device either. A Both displacement shader
on curves or points makes Cycles' own kernels read values they never set, so
that scene has no defined result. The Radial Tiling case is binary32 rounding
residue in Cycles' CPU kernels that Cycles' Metal device does not reproduce
either; when Sides or Roundness is linked, the render fails at run time with an
unsupported-parameter error instead. BVH time steps and volume velocity cannot
be set through this API; the entries name what the device would refuse. A
scene larger than the GPU's storage binding fails (see EXCEEDING THE GPU
STORAGE-BINDING LIMIT) instead of moving device data to host memory, as on
Cycles' Metal device. Open Image Denoise on the CPU, which the CPU and WebGPU
backends use, does not upscale. For the area light, portal and bake entries
Cycles' own kernels evaluate the light or the texel at a direction that is not
a number, and the CPU result depends on how each comparison and minimum drops
that value, which WGSL does not define. The Adreno 650 entries (for example the
Samsung Galaxy S20) are refused before rendering starts, because the shader
compiler of that GPU class miscompiles those programs or renders them
incorrectly; render such scenes on another GPU or on the CPU backend.

The one documented limitation of correct scenes: a driver fault on Intel
integrated graphics could compute a path wrongly in a WebGPU render of a
heterogeneous volume (KNOWN DRIVER LIMITATION: HETEROGENEOUS VOLUMES ON INTEL
GRAPHICS).


WHAT THIS PACKAGE DOES NOT DO
=============================
- It does not encode or decode image or video files (PNG, JPEG, JPEG XL, TIFF,
  EXR, ...). It accepts and returns raw pixel buffers; use CodeBrix.Imaging.
- It does not import model formats (glTF and others); convert their data into
  nodes, meshes and attributes yourself (see BRINGING GLTF SCENES).
- It does not display images or present to a window or swap chain, and a
  render returns its film only when it completes (render events carry
  progress, not pixels).
- It never renders OSL shaders or OSL cameras, VDB volume files (OpenVDB or
  NanoVDB grids) or Catmull-Clark subdivision surfaces (OpenSubdiv), and it never imports
  USD or Alembic files or acts as a Hydra render delegate, on any backend.
  These are outside the package's scope. Volumes defined by shader nodes and
  displacement of the base mesh are in scope (SCENE SETTINGS).
- It does not fill a margin around baked UV islands, project a bake from other
  meshes (selected to active), or convert baked normals to tangent or object
  space. See BAKING TEXTURES.
- It does not render in tiles (a render is one tile, the whole film or its
  camera border), bind several images to one texture node (UDIM tiles),
  or offer Cycles' BVH build and texture-size settings (spatial splits,
  texture limit): every backend builds its BVH with Cycles' defaults and uses
  the full images you pass.
- It never falls back from a GPU backend to the CPU.
- The Core package does not contain the native host library; that is the
  platform package's job (see INSTALLATION).
- It does not cache compiled GPU pipelines on macOS on Intel or on Windows: the
  WebGPU runtime offers no cache on Metal or Direct3D 12 (see PERFORMANCE TIPS).
  Each process there compiles what its scene needs; whether that is faster the
  second time depends only on the platform's driver.
- It does not cache compiled GPU pipelines on Android unless the app gives it a
  directory (CyclesHost.ConfigurePipelineCache or CyclesHost.ConfigureTiers).
- Everything else Cycles renders, every backend renders: see SCENE SETTINGS
  for how to reach it and RENDER-TIME REJECTIONS for the exceptions.


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
Capabilities:  ICapabilityReport caps = scene.GetCapabilities();   // caps.Scope.RenderTimeRejections
Time limit:    scene.RenderTimeLimit = TimeSpan.FromSeconds(30);
Version:       CyclesVersion.NativeLibraryVersion, CyclesRevision, IsAbiCompatible
Progress:      scene.RenderEventReceived += (s, e) => Log(e.Kind, e.SamplesDone);  // no scene calls inside
Passes:        scene.AddPass(PassType.Depth, "Depth"); scene.Render(s); scene.ReadPass("Depth");
Bake:          FilmBuffer film = scene.Bake(target, new BakeSettings(w, h, spp, BakeType.Combined));
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
