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

* Creating native scene hosts that render with WebGPU or with the explicit CPU reference backend (a requested WebGPU render never falls back to the CPU)
* Scene authoring by node and socket: scene singletons (camera, background, film, integrator), objects, meshes, lights and shader graphs
* Typed socket reads and writes for floats, integers, unsigned integers, UTF-8 strings and node references, with size queries
* Triangle mesh upload and geometry attributes (positions, normals, UVs, tangents and custom attributes)
* In-memory image textures from raw pixel buffers in float, half, byte and unsigned-short layouts
* Synchronous, cancellable renders into scene-linear RGBA float buffers
* Process-wide color configuration and WebGPU pipeline-tier selection, with a JSON device capability report
* The native node and property registry as JSON

The library works on raw pixel buffers only. Image file encoding and decoding belong to the CodeBrix.Imaging library.

## Requirements

This library is experimental and is not yet published to nuget.org. The managed assembly loads a native scene host library (`codebrix_cycles_host`) that is built from this repository; the package does not yet carry native runtime assets, so the native host must be available on the library search path of the consuming process. A 64-bit process is required.

## Sample Code

### Render a Lit Triangle with the CPU Reference Backend

```csharp
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
host.Render(32, 24, 8, rgba);   // scene-linear RGBA, lower-left origin
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
