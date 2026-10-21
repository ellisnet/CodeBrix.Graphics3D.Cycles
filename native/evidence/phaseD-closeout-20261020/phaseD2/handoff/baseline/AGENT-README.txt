================================================================================
AGENT-README: CodeBrix.Graphics3D.Cycles
A Guide for AI Coding Agents - CONSUMING the
CodeBrix.Graphics3D.Cycles.ApacheLicenseForever NuGet package
================================================================================

OVERVIEW
========
CodeBrix.Graphics3D.Cycles builds and renders 3D scenes with the Cycles
path-tracing renderer from .NET 10 or later. A CyclesHost owns one native scene
host: you create nodes, set their sockets, upload meshes, attributes and raw
image pixels, and render into a scene-linear RGBA float buffer. Rendering runs
on WebGPU or on an explicit CPU reference backend.

The managed assembly is a thin ownership and marshalling layer. Scene
compilation, BVH construction, image handling, color management, scheduling and
rendering all happen in the native host library. The C-level contract is
experimental, and this managed API is the low-level foundation that a typed,
higher-level scene API will be built on; expect it to grow.

STATUS: experimental. The package is not yet published to nuget.org, and it
does not yet carry native runtime assets (see INSTALLATION).


INSTALLATION
============
PackageId:  CodeBrix.Graphics3D.Cycles.ApacheLicenseForever

    dotnet add package CodeBrix.Graphics3D.Cycles.ApacheLicenseForever

Dependencies: none other than .NET.
License:      Apache-2.0.
Requirements:
  - A 64-bit process (the constructor throws PlatformNotSupportedException
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
    using CodeBrix.Graphics3D.Cycles;

Every public type is in this one namespace.


CORE API REFERENCE
==================
CyclesHost : IDisposable                          (sealed)
---------------------------------------------------------
Construction and lifetime
    CyclesHost(Backend backend, uint threads = 1)
    void Dispose()          cancels active work, waits for native cleanup;
                            safe to call more than once. Any later member call
                            throws ObjectDisposedException.
    bool IsRendering        true while a render runs on this host.
    bool Cancel()           cancels the active render (callable from another
                            thread); false when nothing was rendering.

Scene graph
    Node SceneNode(string name)            singletons: "camera", "background",
                                           "film", "integrator"
    Node CreateNode(string type)           native type names, e.g. "object",
                                           "mesh", "light", "shader"
    Node ShaderOutput(Node shader)         the graph's output node (inputs
                                           "Surface", "Volume", "Displacement")
    Node ShaderNode(Node shader, string type)
                                           e.g. "diffuse_bsdf", "color",
                                           "background_shader", "image_texture"
    void Connect(Node from, string output, Node to, string input)

Socket writes (values are copied before the call returns)
    void SetFloats(Node node, string socket, ReadOnlySpan<float> values)
    void SetInts(Node node, string socket, ReadOnlySpan<long> values)
    void SetUInt(Node node, string socket, ulong value)
    void SetString(Node node, string socket, string value)
    void SetNodes(Node node, string socket, ReadOnlySpan<Node> values)

  Float sockets take flattened components: float2 = 2, float3/color/vector = 3,
  a transform = 12 floats (row-major 3x4) per element. Booleans are 0 or 1 via
  SetInts. Enum sockets take the item name via SetString.

Socket reads
    PropertyInfo Property(Node node, string socket)
    bool GetFloats(Node node, string socket, Span<float> values, out ulong required)
    bool GetInts(Node node, string socket, Span<long> values, out ulong required)
    ulong GetUInt(Node node, string socket)
    bool GetUtf8(Node node, string socket, ulong index, Span<byte> values, out ulong required)
    bool GetNodes(Node node, string socket, Span<ulong> values, out ulong required)

  The span readers follow one protocol: pass Span<T>.Empty to query the size;
  a false return means the destination was too small, was left unchanged, and
  'required' holds the size needed. For GetUtf8, 'required' includes the
  trailing NUL byte.

Geometry
    void Mesh(Node mesh, ReadOnlySpan<float> positions, ReadOnlySpan<uint> indices)
        positions are x,y,z triples; indices three per triangle (both lengths
        must be multiples of 3, else ArgumentException). Every face starts on
        material 0.
    void SetAttribute(Node geometry, string name, AttributeValue value,
                      AttributeDomain domain, AttributeFlags flags,
                      ReadOnlySpan<float> values)
    void SetAttributeBytes(Node geometry, string name, AttributeDomain domain,
                           AttributeFlags flags, ReadOnlySpan<byte> values)
    bool GetAttribute(Node geometry, string name, AttributeFlags flags,
                      Span<float> values, out ulong required)
    bool GetAttributeBytes(Node geometry, string name, AttributeFlags flags,
                           Span<byte> values, out ulong required)
    void RemoveAttribute(Node geometry, string name,
                         AttributeFlags flags = AttributeFlags.None)

  AttributeFlags.Standard selects a standard attribute by name ("P", "N", "uv",
  "generated", "radius", "tangent", "tangent_sign"); any other name with
  AttributeFlags.None is a custom attribute. The element count derives from the
  current geometry, so upload the mesh first.

Images (raw pixels only)
    Image CreateImage(uint width, uint height, ImageFormat format,
                      ReadOnlySpan<byte> pixels)
    void BindImage(Node texture, Image image)
    void ReleaseImage(Image image)

  Pixels are tightly packed, row zero at v = 0, and copied. Float and half data
  are native-endian. Sampling, alpha and colorspace come from the texture node's
  own sockets. ReleaseImage drops your identifier; a bound texture keeps the
  image alive. Binding new Image(0) detaches the in-memory input.

Rendering
    void Render(uint width, uint height, uint samples, Span<float> pixels)
        synchronous; scene-linear RGBA, lower-left origin; pixels must hold at
        least width*height*4 floats. The buffer changes only on success.

Process-wide configuration (static)
    static void ConfigureColor(string path)
        selects the OpenColorIO configuration file. Call it before creating any
        host, or after every host and its in-flight calls have finished.
    static void ConfigureTiers(PipelineTier tier, string recordDirectory,
                               PipelineTierFlags flags = PipelineTierFlags.None)
        WebGPU pipeline tier for hosts created afterwards. recordDirectory is an
        app-writable folder for the persisted device record (null disables it).
    static string RegistryJson()
        the native node/property registry: {"nodeTypes":[{..."inputs":[...],
        "outputs":[...]}...]}. It describes native properties, NOT which
        features a GPU backend implements.
    string DeviceReportJson()      (instance)
        backend, adapter, chosen tier and reason, record status, canary
        result, compile budget, warnings and compiled pipelines.

Value types and enums
    Node(ulong Value)       opaque node identifier, valid only in its own host
    Image(ulong Value)      opaque image identifier, valid only in its own host
    PropertyInfo(uint ValueType, uint Flags, uint Components, ulong Count)
        ValueType is the native value kind (5 float, 6 color, 7 vector,
        8 point, 9 normal, 10 point2, 11 string, 12 enum, 13 transform,
        14 node, ...); Flags bit 1 = array, 2 = linkable, 4 = internal,
        8 = readable, 16 = writable.
    Backend                 WebGpu, CpuReference, Metal (osx-arm64 builds only)
    AttributeValue          Float, Color, Vector, Point, Normal, Point2,
                            Transform, Float4, Byte4
    AttributeDomain         Mesh, Face, Vertex, Corner, Curve, CurveKey,
                            VertexNormal, CornerNormal, CornerByte,
                            CurveKeyNormal
    AttributeFlags [Flags]  None, Standard, Subdivision
    ImageFormat             Float, Float4, Half, Half4, Byte, Byte4, UShort,
                            UShort4
    PipelineTier            Auto, Full, Conservative
    PipelineTierFlags [Flags]  None, RecordOff, RecordReadOnly, Canary

Error model
    NativeFailure : Exception
        int Status    1 invalid argument, 2 unsupported, 3 failed,
                      4 cancelled, 5 not ready, 6 buffer too small
        Message       the native error text
    Every failing native call throws NativeFailure, except the documented
    'false' returns of the span readers and Cancel.


COMPLETE EXAMPLES
=================
Render a lit triangle on the CPU reference backend
--------------------------------------------------
    using CodeBrix.Graphics3D.Cycles;

    using CyclesHost host = new(Backend.CpuReference);

    Node camera = host.SceneNode("camera");
    host.SetFloats(camera, "fov", [1.1f]);
    host.SetFloats(camera, "matrix", [1,0,0,0, 0,1,0,0, 0,0,1,-4]);

    Node material = host.CreateNode("shader");
    Node diffuse = host.ShaderNode(material, "diffuse_bsdf");
    Node color = host.ShaderNode(material, "color");
    host.SetFloats(color, "value", [.7f, .2f, .1f]);
    host.Connect(color, "Color", diffuse, "Color");
    host.Connect(diffuse, "BSDF", host.ShaderOutput(material), "Surface");

    Node mesh = host.CreateNode("mesh");
    host.SetNodes(mesh, "used_shaders", [material]);
    host.Mesh(mesh, [-1.5f,-1,0, 1.5f,-1,0, 0,1.5f,0], [0, 1, 2]);
    Node triangle = host.CreateNode("object");
    host.SetNodes(triangle, "geometry", [mesh]);

    Node world = host.CreateNode("shader");
    Node background = host.ShaderNode(world, "background_shader");
    host.SetFloats(background, "color", [.25f, .4f, .6f]);
    host.Connect(background, "Background", host.ShaderOutput(world), "Surface");
    host.SetNodes(host.SceneNode("background"), "shader", [world]);

    float[] rgba = new float[32 * 24 * 4];
    host.Render(32, 24, 8, rgba);

Read a string socket safely (size query, then copy)
---------------------------------------------------
    host.GetUtf8(node, "asset_name", 0, Span<byte>.Empty, out ulong size);
    byte[] utf8 = new byte[(int)size];
    while (!host.GetUtf8(node, "asset_name", 0, utf8, out size))
        utf8 = new byte[(int)size];          // another edit resized it
    string name = System.Text.Encoding.UTF8.GetString(utf8, 0, (int)size - 1);

Bind a raw RGBA8 texture
------------------------
    Node texture = host.ShaderNode(material, "image_texture");
    Image image = host.CreateImage(2, 2, ImageFormat.Byte4, pixels);  // 16 bytes
    host.BindImage(texture, image);
    host.ReleaseImage(image);       // the binding keeps the pixels alive
    host.Connect(texture, "Color", diffuse, "Color");

Cancel a long render from another thread
----------------------------------------
    Task render = Task.Run(() => host.Render(256, 256, 100000, rgba));
    // ... later
    host.Cancel();
    // the render throws NativeFailure with Status == 4; rgba is unchanged


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

Program.cs: the "Render a lit triangle" example above. Run it with the native
host on the library search path (for example LD_LIBRARY_PATH on Linux).


PERFORMANCE TIPS
================
- Reuse one CyclesHost for repeated renders; edit sockets between renders
  instead of rebuilding the scene. Host creation and scene compilation are the
  expensive steps.
- Allocate the RGBA buffer once and reuse it; Render writes it only on success.
- Upload a mesh once and change materials or transforms through sockets.
- Keep span-reader buffers around between calls and grow them only when a
  call returns false.


COMMON PITFALLS TO AVOID
========================
- CALLING ConfigureColor WHILE A HOST EXISTS. Color configuration is
  process-wide; it fails with Status 5 while any host or in-flight call is
  alive. Configure first, then create hosts.
- EXPECTING A CPU FALLBACK. A host created with Backend.WebGpu never falls back
  to CPU shading; choose Backend.CpuReference explicitly when you want the CPU.
- READING RegistryJson AS A FEATURE LIST. The registry lists native node types
  and sockets; a node being registered does not mean a GPU kernel implements it.
- MIXING NODES BETWEEN HOSTS. Node and Image identifiers belong to the host
  that created them; passing one to another host throws NativeFailure (Status 1).
- UNDERSIZED RENDER BUFFERS. Render needs width*height*4 floats; a smaller span
  is rejected with Status 1 and the buffer is not touched.
- ASSUMING A QUERY-THEN-COPY CANNOT RACE. Another edit can change a socket's
  size between the size query and the copy; loop until the copy returns true.
- LOADING IMAGE FILES THROUGH THIS LIBRARY. It takes raw pixel buffers only;
  decode files with CodeBrix.Imaging first.


WHAT THIS PACKAGE DOES NOT DO
=============================
- It does not encode or decode image files (PNG, JPEG, EXR, ...). It accepts
  and returns raw pixel buffers; use CodeBrix.Imaging for files.
- It does not import model formats (glTF and others); convert their data into
  nodes, meshes and attributes yourself.
- It does not display images or present to a window or swap chain.
- It does not yet ship the native host library inside the package.
- It does not provide a typed scene object model; scenes are built through
  node type names and socket names.


WORKING EXAMPLES ON GITHUB
==========================
- Host lifetime, socket round trips, registry and device report JSON, mesh
  validation and rendering:
  https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/tests/CodeBrix.Graphics3D.Cycles.Tests/CyclesHostTests.cs
- Enum values and their native constants:
  https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/tests/CodeBrix.Graphics3D.Cycles.Tests/NativeHeaderConstantMatching.cs
- The error type:
  https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/tests/CodeBrix.Graphics3D.Cycles.Tests/NativeFailureTests.cs


QUICK REFERENCE CARD
====================
Create:        using CyclesHost host = new(Backend.CpuReference);
Singleton:     Node cam = host.SceneNode("camera");
New node:      Node obj = host.CreateNode("object");
Shader graph:  Node sh = host.CreateNode("shader");
               Node bsdf = host.ShaderNode(sh, "diffuse_bsdf");
               host.Connect(bsdf, "BSDF", host.ShaderOutput(sh), "Surface");
Set values:    host.SetFloats(n, "socket", [..]); SetInts / SetUInt /
               SetString / SetNodes
Get values:    host.GetFloats(n, "socket", span, out ulong required)
               (Span<T>.Empty = size query; false = too small)
Mesh:          host.Mesh(mesh, xyz, triangleIndices);
Attribute:     host.SetAttribute(mesh, "uv", AttributeValue.Point2,
                   AttributeDomain.Corner, AttributeFlags.Standard, values);
Image:         Image img = host.CreateImage(w, h, ImageFormat.Byte4, bytes);
               host.BindImage(textureNode, img);
Render:        host.Render(w, h, samples, rgbaFloats);
Cancel:        host.Cancel();
Errors:        catch (NativeFailure e) when (e.Status == 4) { /* cancelled */ }
Process-wide:  CyclesHost.ConfigureColor(path); CyclesHost.ConfigureTiers(...);
               CyclesHost.RegistryJson();

Target: .NET 10 or later
License: Apache-2.0


================================================================================
END OF AGENT-README
