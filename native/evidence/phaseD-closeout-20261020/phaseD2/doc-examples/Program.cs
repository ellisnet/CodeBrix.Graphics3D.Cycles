// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Threading;
using CodeBrix.Graphics3D.Cycles;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using CodeBrix.Graphics3D.Cycles.Generated;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Lights;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Interop;
using CodeBrix.Graphics3D.Cycles.Rendering;

NativeLibrary.SetDllImportResolver(typeof(CyclesHost).Assembly, (name, assembly, path) =>
    name == "codebrix_cycles_host" ? NativeLibrary.Load(args[0]) : IntPtr.Zero);

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

FilmBuffer film = await scene.RenderAsync(new RenderSettings(32, 24, 4));
Console.WriteLine("film " + film.Width + "x" + film.Height + " first=" + film.Span[0]);

using (CancellationTokenSource timeout = new(TimeSpan.FromSeconds(1)))
{
    Progress<RenderProgress> progress = new(p => Console.WriteLine($"{p.Stage} {p.Elapsed}"));
    try
    {
        FilmBuffer big = await scene.RenderAsync(new RenderSettings(64, 48, 1000000), progress, timeout.Token);
    }
    catch (OperationCanceledException)
    {
        Console.WriteLine("cancelled");
    }
}

RenderSettings settings = new(64, 48, 4);
float[] rgba = new float[settings.FloatCount];
await scene.RenderAsync(settings, rgba);
scene.Render(settings, rgba);

PointLightNode lamp = scene.CreateLight<PointLightNode>();
lamp.Strength = new Vector3(100, 100, 100);
ShaderNode emission = scene.CreateShader();
EmissionNode emit = emission.Add<EmissionNode>();
emission.Connect(emit.EmissionOutput, emission.Output.SurfaceInput);
lamp.UsedShaders = [emission];
scene.CreateObject(lamp, Transform3x4.CreateTranslation(2, 3, -2));

uint width = 2, height = 2;
byte[] rgba8 = new byte[16];
float[] uvs = [0, 0, 1, 0, 0, 1];
ImageTextureNode texture = material.Add<ImageTextureNode>();
using (CyclesImage image = scene.CreateImage<byte>(width, height, ImageFormat.Byte4, rgba8))
    texture.Bind(image);
texture.Colorspace = "__builtin_srgb";
material.Connect(texture.ColorOutput, diffuse.ColorInput);
mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, uvs);
using CyclesImage halves = scene.CreateImage<Half>(1, 1, ImageFormat.Half4, [(Half)1, (Half)0, (Half)0, (Half)1]);

Node camera = scene.Host.SceneNode("camera");
scene.Host.SetFloats(camera, "fov", [0.9f]);
float fov = scene.Camera.Fov;
Console.WriteLine("fov " + fov);
scene.Render(settings, rgba);

if (CyclesAbi.Create(Backend.CpuReference, 1, out ulong host) != NativeStatus.Ok)
    throw new InvalidOperationException(CyclesAbi.LastError());
try
{
    CyclesAbi.SceneNode(host, "camera", out ulong rawCamera);
    NativeStatus status = CyclesAbi.SetFloats(host, rawCamera, "fov", [1.1f]);
    Console.WriteLine("raw " + status);
}
finally
{
    CyclesAbi.Destroy(host);
}

scene.Integrator.MaxBounce = 8;
ICapabilityReport caps = scene.GetCapabilities();
Console.WriteLine(caps.BackendName + " abi " + CyclesVersion.Abi + " " + CyclesVersion.IsAbiCompatible + " inputs " + NodeType.Find("principled_bsdf").Inputs.Count);
try { scene.Create<CameraNode>(); } catch (ArgumentException) { Console.WriteLine("create camera rejected"); }
try { new CyclesScene(CyclesBackend.Gpu).Dispose(); Console.WriteLine("gpu scene ok"); }
catch (CyclesException e) when (e.NativeStatus == NativeStatus.Unsupported) { Console.WriteLine("no gpu"); }
catch (CyclesException e) { Console.WriteLine("gpu " + e.NativeStatus + ": " + e.Message); }
