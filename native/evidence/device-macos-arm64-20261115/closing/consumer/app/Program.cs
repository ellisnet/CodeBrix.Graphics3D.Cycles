// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Scratch consumer test: the AGENT-README lit triangle on the CPU, on Metal, and on Metal with
// Open Image Denoise on the GPU; each film must be finite and plausible. One JSON line per render.
using System.Diagnostics;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using CodeBrix.Graphics3D.Cycles;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Rendering;

const int Width = 160, Height = 120, Samples = 16;
var binary = CyclesScene.GetBinaryCapabilities();
Console.WriteLine(JsonSerializer.Serialize(new
{
    kind = "binary",
    compiledBackends = binary.CompiledBackends,
    gpuBackend = binary.GpuBackendName,
    library = binary.LibraryVersion,
    cycles = binary.CyclesRevision,
    baseDirectory = AppContext.BaseDirectory,
    framework = RuntimeInformation.FrameworkDescription,
    runtimeDirectory = RuntimeEnvironment.GetRuntimeDirectory(),
}));

int failures = 0;
float[]? cpu = null;
foreach (var (label, backend, denoise) in new[] { ("cpu", CyclesBackend.CpuReference, false), ("metal", CyclesBackend.Gpu, false), ("metal-oidn", CyclesBackend.Gpu, true) })
{
    var clock = Stopwatch.StartNew();
    try
    {
        await using CyclesScene scene = new(backend);
        scene.Camera.Fov = 1.1f;
        scene.Camera.Matrix = Transform3x4.CreateTranslation(0, 0, -4);
        ShaderNode material = scene.CreateShader();
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        diffuse.Color = new Vector3(.7f, .2f, .1f);
        material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);
        MeshNode mesh = scene.CreateMesh([-1.5f, -1, 0, 1.5f, -1, 0, 0, 1.5f, 0], [0, 1, 2], material);
        scene.CreateObject(mesh);
        ShaderNode world = scene.CreateShader();
        BackgroundShaderNode sky = world.Add<BackgroundShaderNode>();
        sky.Color = new Vector3(.25f, .4f, .6f);
        world.Connect(sky.BackgroundOutput, world.Output.SurfaceInput);
        scene.Background.Shader = world;
        if (denoise)
        {
            scene.Integrator.UseDenoise = true;
            scene.Integrator.DenoiseUseGpu = true;
        }
        var capabilities = scene.GetCapabilities();
        FilmBuffer film = await scene.RenderAsync(new RenderSettings(Width, Height, Samples));
        float[] pixels = film.ToArray();
        bool finite = pixels.All(float.IsFinite);
        float[] Pixel(int x, int y) => pixels.AsSpan((y * Width + x) * 4, 4).ToArray();
        float[] corner = Pixel(2, 2), centre = Pixel(Width / 2, Height / 2);
        double[] mean = Enumerable.Range(0, 4).Select(c => pixels.Where((_, i) => i % 4 == c).Average(v => (double)v)).ToArray();
        bool skyOk = Math.Abs(corner[0] - .25f) < .02f && Math.Abs(corner[1] - .4f) < .02f && Math.Abs(corner[2] - .6f) < .02f;
        bool triangleOk = centre[0] > centre[1] && centre[0] > centre[2] && centre[0] > .02f && centre[0] < 1f;
        double? maxCpu = null, meanCpu = null;
        if (cpu is not null)
        {
            maxCpu = pixels.Zip(cpu, (a, b) => Math.Abs((double)a - b)).Max();
            meanCpu = pixels.Zip(cpu, (a, b) => Math.Abs((double)a - b)).Average();
        }
        bool closeToCpu = denoise || cpu is null || meanCpu < 0.01;
        bool ok = finite && skyOk && triangleOk && closeToCpu && film.Width == Width && film.Height == Height;
        if (label == "cpu") cpu = pixels;
        if (!ok) failures++;
        Console.WriteLine(JsonSerializer.Serialize(new
        {
            kind = "render",
            label,
            ok,
            backend = capabilities.BackendName,
            device = capabilities.DeviceDescription,
            denoiseDevices = capabilities.DenoiseDevices,
            seconds = Math.Round(clock.Elapsed.TotalSeconds, 2),
            filmSha256 = Convert.ToHexStringLower(SHA256.HashData(MemoryMarshal.AsBytes(pixels.AsSpan()))),
            finite,
            mean,
            corner,
            centre,
            maxAbsVsCpu = maxCpu,
            meanAbsVsCpu = meanCpu,
        }));
    }
    catch (Exception error)
    {
        failures++;
        Console.WriteLine(JsonSerializer.Serialize(new { kind = "render", label, ok = false, seconds = Math.Round(clock.Elapsed.TotalSeconds, 2), error = error.GetType().FullName + ": " + error.Message }));
    }
}
return failures == 0 ? 0 : 1;
