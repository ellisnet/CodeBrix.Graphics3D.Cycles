# CodeBrix.Graphics3D.Cycles

A .NET library for building and rendering 3D scenes with the Cycles path-tracing renderer, through a native scene host with a WebGPU rendering backend and an explicit CPU reference backend. CodeBrix.Graphics3D.Cycles is provided as a .NET 10 library and associated `CodeBrix.Graphics3D.Cycles.ApacheLicenseForever` NuGet package.

CodeBrix.Graphics3D.Cycles supports applications and assemblies that target Microsoft .NET version 10.0 and later.
Microsoft .NET version 10.0 is a Long-Term Supported (LTS) version of .NET, and was released on Nov 11, 2025; and will be actively supported by Microsoft until Nov 14, 2028.
Please update your C#/.NET code and projects to the latest LTS version of Microsoft .NET.

## Installation

```
dotnet add package CodeBrix.Graphics3D.Cycles.ApacheLicenseForever
```

Note that the NuGet package ID and the namespace are different - there is no package named plain `CodeBrix.Graphics3D.Cycles`:

* NuGet package ID: `CodeBrix.Graphics3D.Cycles.ApacheLicenseForever`
* Assembly and primary namespace: `CodeBrix.Graphics3D.Cycles` - i.e. `using CodeBrix.Graphics3D.Cycles;`

XML documentation (IntelliSense) ships alongside the assembly.

CodeBrix.Graphics3D.Cycles has no NuGet dependencies other than .NET.

## CodeBrix.Graphics3D.Cycles supports:

* A typed scene API: `CyclesScene` with typed camera, film, integrator and background settings, objects, meshes, lights, shader graphs and images
* A complete object model generated from the native node registry: one class per native node type (scene nodes, geometry, lights and every shader graph node) with strongly typed properties, enumerations and shader graph sockets
* Backend-neutral device selection (GPU or the explicit CPU reference backend) with a capability report; a GPU render never falls back to the CPU
* Asynchronous rendering with `CancellationToken` cancellation and `IProgress<T>` progress reporting, plus synchronous rendering into caller-owned buffers
* Raw scene-linear float32 RGBA film readback as `ReadOnlyMemory<float>` / spans with width, height and stride
* In-memory image textures from raw pixel spans in eight float, half, byte and unsigned-short layouts
* Triangle mesh upload and geometry attributes (positions, normals, UVs, tangents and custom attributes)
* .NET exceptions that carry the native status code and error text
* A public low-level interop tier: by-name socket access, a status-code layer that mirrors the native C ABI one to one, and the raw P/Invoke declarations
* Process-wide color configuration and WebGPU pipeline-tier selection, and the native node and property registry as JSON

The library works on raw pixel buffers only. Image file encoding and decoding belong to the CodeBrix.Imaging library.

## Requirements

This library is experimental and is not yet published to nuget.org. The managed assembly loads a native scene host library (`codebrix_cycles_host`) that is built from this repository; the package does not yet carry native runtime assets, so the native host must be available on the library search path of the consuming process. A 64-bit process is required.

## Sample Code

### Render a Lit Triangle with the CPU Reference Backend

```csharp
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
// film.Pixels: scene-linear RGBA floats, bottom row first
```

### Address Native Sockets Directly

```csharp
using CodeBrix.Graphics3D.Cycles;

using CyclesHost host = new(Backend.CpuReference);
Node camera = host.SceneNode("camera");
host.SetFloats(camera, "fov", [1.1f]);
host.SetFloats(camera, "matrix", [1,0,0,0, 0,1,0,0, 0,0,1,-4]);
```

## Documentation

The NuGet package includes `AGENT-README.txt`, a complete API reference and usage guide written for AI coding agents - point your agent at that file when it is writing code against this library.

Additional sample code and usage examples are available in the `CodeBrix.Graphics3D.Cycles.Tests` project:
https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/tree/main/tests/CodeBrix.Graphics3D.Cycles.Tests

## License

CodeBrix.Graphics3D.Cycles is licensed under the Apache License 2.0 - see the
[LICENSE](https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/LICENSE) file.

For licensing and provenance information about the open source code included in
this package, see [THIRD-PARTY-NOTICES.txt](https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles/blob/main/THIRD-PARTY-NOTICES.txt).
