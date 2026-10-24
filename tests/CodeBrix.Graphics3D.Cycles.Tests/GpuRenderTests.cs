// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.GltfRender;
using CodeBrix.Graphics3D.Cycles.Nodes;
using CodeBrix.Graphics3D.Cycles.Rendering;
using CodeBrix.Graphics3D.Cycles.Validation;
using CodeBrix.Graphics3D.Gltf2.Schema2;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Renders the shared validation scenes through the typed API on CyclesBackend.Gpu, the platform's
// GPU backend: the WGSL device on WebGPU everywhere except Apple Silicon Macs, where it is Cycles'
// own Metal device. Each GPU film is judged against its backend's authority (GpuReferences): the
// in-process CPU film for WebGPU, the reviewed recorded Metal film for Metal. Strict differences
// are reported; GPU film accumulation uses atomic float addition, so they vary from run to run.
// Skips with a reason when the native host or a GPU device is absent. All tests are in one class
// so GPU renders run one at a time.
public class GpuRenderTests(ITestOutputHelper output)
{
    private static CancellationToken Token => TestContext.Current.CancellationToken;

    private static readonly Lazy<string> Unavailable = new(ProbeGpu);

    // Creates a GPU scene, or skips when this machine has no usable GPU device.
    private static CyclesScene CreateGpuScene()
    {
        TestScenes.RequireNativeHost();
        if (Unavailable.Value != null) Assert.Skip(Unavailable.Value);
        return new CyclesScene(CyclesBackend.Gpu);
    }

    private static string ProbeGpu()
    {
        try
        {
            using CyclesScene scene = new(CyclesBackend.Gpu);
            Backend expected = CyclesScene.ResolveBackend(CyclesBackend.Gpu);
            return scene.Backend == expected ? null : "CyclesBackend.Gpu created " + scene.Backend + ", not the resolved " + expected + ".";
        }
        catch (CyclesException error)
        {
            return "No GPU device is available to the native host: " + error.Message;
        }
    }

    private static FilmBuffer RenderCpu(string name)
    {
        using CyclesScene scene = TestScenes.CreateCpuScene();
        WebGpuValidationScenes.Build(scene, name);
        return scene.Render(WebGpuValidationScenes.Settings);
    }

    // Validation reference policy (native/DEVICE.md): with CODEBRIX_CYCLES_REFERENCE_EXPORT set, the
    // CPU reference film rendered here (Linux x64, glibc) is written for the Android app gate's
    // --linux-reference comparison. Without the variable nothing is written.
    private static void ExportReference(string name, FilmBuffer film)
    {
        string folder = Environment.GetEnvironmentVariable("CODEBRIX_CYCLES_REFERENCE_EXPORT");
        if (string.IsNullOrEmpty(folder)) return;
        Directory.CreateDirectory(folder);
        File.WriteAllBytes(Path.Combine(folder, name + ".cpu.rgba.bin"), MemoryMarshal.AsBytes(film.Span).ToArray());
    }

    // Judges a GPU film against its backend's authority (GpuReferences) and logs both comparisons.
    private FilmComparison Judge(CyclesScene gpu, string name, FilmBuffer film, FilmBuffer cpu)
    {
        ICapabilityReport report = gpu.GetCapabilities();
        Dictionary<string, string> facts = new()
        {
            ["device"] = report.DeviceDescription,
            ["backend"] = report.BackendName,
            ["cyclesMetalRt"] = Environment.GetEnvironmentVariable("CYCLES_METALRT") ?? "unset (Blender's rule: MetalRT on M3 and later)",
            ["libraryVersion"] = CyclesScene.GetBinaryCapabilities().LibraryVersion,
        };
        return GpuReferences.Judge(gpu.Backend, name, film.Span, cpu.Span, output.WriteLine, facts);
    }

    [Theory]
    [InlineData(WebGpuValidationScenes.Glass)]
    [InlineData(WebGpuValidationScenes.DiffuseSun)]
    [InlineData(WebGpuValidationScenes.TexturedPlane)]
    public async Task gpu_film_matches_its_backend_reference_with_the_practical_criterion(string name)
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, name);
        FilmBuffer cpu = RenderCpu(name);
        ExportReference(name, cpu);

        //Act
        FilmBuffer film = await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        FilmComparison comparison = Judge(gpu, name, film, cpu);

        //Assert
        gpu.Backend.Should().Be(CyclesScene.ResolveBackend(CyclesBackend.Gpu));
        gpu.IsFaulted.Should().BeFalse();
        comparison.PracticalFailures.Should().Be(0);
        // The scene is not trivially empty: the films hold more than the background colour.
        cpu.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(10);
    }

    [Fact]
    public async Task gpu_render_reports_progress_events_and_async_progress()
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        ConcurrentQueue<RenderEvent> events = new();
        ConcurrentQueue<RenderProgress> reports = new();
        gpu.RenderEventReceived += (sender, e) => events.Enqueue(e);

        //Act
        await gpu.RenderAsync(WebGpuValidationScenes.Settings, new SynchronousProgress(reports), Token);

        //Assert
        events.Should().Contain(e => e.Kind == RenderEventKind.Progress && e.SamplesTotal == WebGpuValidationScenes.Settings.Samples);
        events.Should().NotContain(e => e.Kind == RenderEventKind.Error);
        reports.Should().Contain(r => r.CompletedSamples.HasValue);
        reports.Last().Stage.Should().Be(RenderStage.Completed);
    }

    [Fact]
    public async Task gpu_render_is_cancelled_and_the_scene_renders_again_afterwards()
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        using CancellationTokenSource cancellation = CancellationTokenSource.CreateLinkedTokenSource(Token);
        int progressEvents = 0;
        void CancelOnProgress(object sender, RenderEvent e)
        {
            if (e.Kind == RenderEventKind.Progress && Interlocked.Increment(ref progressEvents) == 1) cancellation.Cancel();
        }
        gpu.RenderEventReceived += CancelOnProgress;

        //Act
        Func<Task> render = () => gpu.RenderAsync(WebGpuValidationScenes.Settings with { Samples = 1_000_000 }, null, cancellation.Token);

        //Assert
        await render.Should().ThrowAsync<OperationCanceledException>();
        gpu.IsFaulted.Should().BeFalse();
        gpu.RenderEventReceived -= CancelOnProgress;
        FilmBuffer again = await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        Judge(gpu, WebGpuValidationScenes.DiffuseSun, again, RenderCpu(WebGpuValidationScenes.DiffuseSun)).PracticalFailures.Should().Be(0);
    }

    [Fact]
    public async Task gpu_scene_recovers_after_a_rejected_render_once_the_cause_is_removed()
    {
        //Arrange: each backend rejects a scene for its own reason. WebGPU does not write a depth
        //pass; Cycles' Metal device refuses an image beyond Metal's 16384-pixel texture limit.
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        ConcurrentQueue<RenderEvent> events = new();
        gpu.RenderEventReceived += (sender, e) => events.Enqueue(e);
        Action removeCause;
        if (gpu.Backend == Backend.Metal)
        {
            (ObjectNode quad, MeshNode mesh, ShaderNode material) = OversizedTexturedQuad(gpu);
            removeCause = () => { gpu.Remove(quad); gpu.Remove(mesh); gpu.Remove(material); };
        }
        else
        {
            PassNode depth = gpu.AddPass(PassType.Depth, "Depth");
            removeCause = () => gpu.Remove(depth);
        }
        Func<Task> rejected = () => gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        await rejected.Should().ThrowAsync<NativeFailure>();
        bool faulted = gpu.IsFaulted;
        removeCause();

        //Act
        FilmBuffer film = await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);

        //Assert
        faulted.Should().BeTrue();
        gpu.IsFaulted.Should().BeFalse();
        events.Should().Contain(e => e.Kind == RenderEventKind.Error);
        events.Should().Contain(e => e.Kind == RenderEventKind.Status && e.Message.StartsWith("Device recovered", StringComparison.Ordinal));
        Judge(gpu, WebGpuValidationScenes.DiffuseSun, film, RenderCpu(WebGpuValidationScenes.DiffuseSun)).PracticalFailures.Should().Be(0);
    }

    // A small quad whose material samples a 16385 x 1 image, which Cycles' Metal device rejects.
    private static (ObjectNode, MeshNode, ShaderNode) OversizedTexturedQuad(CyclesScene scene)
    {
        ShaderNode material = scene.CreateShader();
        ImageTextureNode texture = material.Add<ImageTextureNode>();
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        material.Connect(texture.ColorOutput, diffuse.ColorInput);
        material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        using (CyclesImage image = scene.CreateImage<byte>(16385, 1, ImageFormat.Byte4, new byte[16385 * 4]))
            texture.Bind(image);
        MeshNode mesh = scene.CreateMesh([-.3f, -.3f, 0, .3f, -.3f, 0, 0, .3f, 0], [0, 1, 2], material);
        ObjectNode quad = scene.CreateObject(mesh);
        return (quad, mesh, material);
    }

    [Fact]
    public async Task gpu_object_moved_after_a_render_matches_a_scene_built_with_it()
    {
        //Arrange: the comparison scene is built with the object already moved, on the backend that
        //is the authority for this GPU (the CPU for WebGPU, a fresh Metal scene for Metal).
        Transform3x4 moved = new(.7f, 0, .2f, -.6f, 0, .9f, 0, .3f, -.2f, 0, .7f, .4f);
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        ObjectNode box = gpu.CreateObject(gpu.CreateMesh([-.4f, -.4f, 0, .4f, -.4f, 0, 0, .5f, 0], [0, 1, 2], Diffuse(gpu)));
        await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        FilmBuffer BuiltWith(CyclesScene scene)
        {
            WebGpuValidationScenes.Build(scene, WebGpuValidationScenes.DiffuseSun);
            scene.CreateObject(scene.CreateMesh([-.4f, -.4f, 0, .4f, -.4f, 0, 0, .5f, 0], [0, 1, 2], Diffuse(scene)), moved);
            return scene.Render(WebGpuValidationScenes.Settings);
        }
        FilmBuffer cpu;
        using (CyclesScene cpuScene = TestScenes.CreateCpuScene()) cpu = BuiltWith(cpuScene);
        FilmBuffer authority = cpu;
        if (gpu.Backend == Backend.Metal)
        {
            using CyclesScene fresh = new(CyclesBackend.Gpu);
            authority = BuiltWith(fresh);
        }

        //Act
        box.Tfm = moved;
        FilmBuffer film = await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        FilmComparison comparison = FilmComparison.Compare(film.Span, authority.Span);
        output.WriteLine($"moved object versus a scene built with it on {(gpu.Backend == Backend.Metal ? "Metal" : "the CPU")}: {comparison}");
        if (gpu.Backend == Backend.Metal)
            output.WriteLine("moved object CPU comparison (reported, not required on Metal): " + FilmComparison.Compare(film.Span, cpu.Span));

        //Assert
        comparison.PracticalFailures.Should().Be(0);
    }

    // A shadow catcher plane over the diffuse-sun scene: the GPU writes Combined and the three shadow
    // catcher passes; each one read back through ReadPass matches the CPU reference backend.
    private static readonly string[] ShadowCatcherPasses = ["Shadow Catcher", "Shadow Catcher Matte", "Shadow Catcher Sample Count"];

    private static void BuildShadowCatcher(CyclesScene scene, bool approximate)
    {
        WebGpuValidationScenes.Build(scene, WebGpuValidationScenes.DiffuseSun);
        scene.Background.Transparent = !approximate;
        scene.Film.UseApproximateShadowCatcher = approximate;
        ObjectNode catcher = scene.CreateObject(scene.CreateMesh([-2.5f, .01f, -2.5f, 2.5f, .01f, -2.5f, 2.5f, .01f, 2.5f, -2.5f, .01f, 2.5f],
            [0, 2, 1, 0, 3, 2], Diffuse(scene)));
        catcher.IsShadowCatcher = true;
        scene.AddPass(PassType.ShadowCatcher, ShadowCatcherPasses[0]);
        scene.AddPass(PassType.ShadowCatcherMatte, ShadowCatcherPasses[1]);
        scene.AddPass(PassType.ShadowCatcherSampleCount, ShadowCatcherPasses[2]);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task gpu_shadow_catcher_passes_match_the_cpu_reference(bool approximate)
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        BuildShadowCatcher(gpu, approximate);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        BuildShadowCatcher(cpu, approximate);
        FilmBuffer reference = cpu.Render(WebGpuValidationScenes.Settings);

        //Act
        FilmBuffer film = await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);

        //Assert
        gpu.IsFaulted.Should().BeFalse();
        WebGpuValidationScenes.Compare(film.Span, reference.Span).Practical.Should().Be(0);
        gpu.GetRenderedPasses().Where(p => ShadowCatcherPasses.Contains(p.Name)).Should().HaveCount(3).And.OnlyContain(p => p.Written);
        foreach (string name in ShadowCatcherPasses)
        {
            FilmBuffer pass = gpu.ReadPass(name), expected = cpu.ReadPass(name);
            (double maximum, int practical, int strict) = WebGpuValidationScenes.Compare(pass.Span, expected.Span);
            output.WriteLine($"{name} (approximate {approximate}): {pass.Components} components, max abs error {maximum:G6}, practical failures {practical}, strict differences {strict}");
            pass.Components.Should().Be(expected.Components);
            practical.Should().Be(0);
        }
        // The catcher is hit: the sample count pass is not empty.
        gpu.ReadPass(ShadowCatcherPasses[2]).Span.ToArray().Max().Should().BeGreaterThan(0);
    }

    // A diffuse material (the Cycles default surface shader is not implemented on WebGPU).
    private static ShaderNode Diffuse(CyclesScene scene)
    {
        ShaderNode material = scene.CreateShader();
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        diffuse.Color = new System.Numerics.Vector3(.3f, .6f, .4f);
        material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        return material;
    }

    [Fact]
    public async Task gpu_render_passes_follow_the_backend_capability_report()
    {
        //Arrange: a backend whose report lists the passes it writes (WebGPU: combined only) rejects
        //others at render time; a backend that writes every registered pass (Metal) renders them.
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        BackendScope scope = CyclesScene.GetBinaryCapabilities().BackendScopes[gpu.GetCapabilities().BackendName];
        bool writesDepth = scope.WrittenPasses.Count == 0 || scope.WrittenPasses.Contains("depth");
        gpu.AddPass(PassType.Depth, "Depth");
        gpu.AddPass(PassType.Normal, "Normal");

        //Act
        Func<Task<FilmBuffer>> render = () => gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);

        //Assert
        if (!writesDepth)
        {
            await render.Should().ThrowAsync<NativeFailure>();
            return;
        }
        await render();
        int pixels = (int)(WebGpuValidationScenes.Settings.Width * WebGpuValidationScenes.Settings.Height);
        FilmBuffer depth = gpu.ReadPass("Depth"), normal = gpu.ReadPass("Normal");
        depth.Components.Should().Be(1);
        depth.Span.Length.Should().Be(pixels);
        normal.Span.Length.Should().Be(pixels * 3);
        depth.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        normal.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        depth.Span.ToArray().Distinct().Count().Should().BeGreaterThan(10);
    }

    [Fact]
    public void gpu_capability_report_describes_the_platform_gpu_backend()
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        gpu.Render(WebGpuValidationScenes.Settings);

        //Act
        ICapabilityReport report = gpu.GetCapabilities();
        output.WriteLine($"backend {report.BackendName}; device {report.DeviceDescription}; tier {report.PipelineTier}; warnings [{string.Join("; ", report.Warnings)}]");

        //Assert
        report.HasDevice.Should().BeTrue();
        report.IsGpu.Should().BeTrue();
        report.Backend.Should().Be(gpu.Backend);
        report.DeviceDescription.Should().NotBeNullOrEmpty();
        report.GpuFallsBackToCpu.Should().BeFalse();
        if (gpu.Backend == Backend.Metal)
        {
            report.BackendName.Should().Be("metal");
            report.PipelineTier.Should().BeNull();
            report.Scope.CameraTypes.Should().Contain(["perspective", "orthograph", "panorama"]);
            report.Scope.RenderTimeRejections.Should().BeEmpty();
            report.Scope.PassesNote.Should().NotBeNullOrEmpty();
        }
        else
        {
            report.BackendName.Should().Be("webgpu");
            report.PipelineTier.Should().BeOneOf("full", "conservative");
            report.Scope.WrittenPasses.Should().Contain("combined");
        }
    }

    [Fact]
    public void gpu_device_report_states_the_backend_pipeline_facts()
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        gpu.Render(WebGpuValidationScenes.Settings);

        //Act
        using JsonDocument device = JsonDocument.Parse(gpu.Host.DeviceReportJson());
        ICapabilityReport report = gpu.GetCapabilities();

        //Assert: the WGSL device reports its pipeline-size guard; Cycles' Metal device has no
        //WebGPU pipeline tier, so its report names the backend and carries no pipelines object.
        if (gpu.Backend == Backend.Metal)
        {
            device.RootElement.GetProperty("backend").GetString().Should().Be("metal");
            device.RootElement.GetProperty("pipelines").ValueKind.Should().Be(JsonValueKind.Null);
            return;
        }
        JsonElement guard = device.RootElement.GetProperty("pipelines").GetProperty("sizeGuard");
        long threshold = guard.GetProperty("thresholdBytes").GetInt64();
        long largest = guard.GetProperty("largestEntryBytes").GetInt64();
        output.WriteLine($"size guard: threshold {threshold} bytes, largest entry {guard.GetProperty("largestEntry").GetString()} {largest} bytes, warnings {guard.GetProperty("warnings").GetInt32()}");
        // A small scene compiles entries well below the guard, so no size warning reaches the report.
        threshold.Should().Be(1110000);
        largest.Should().BeGreaterThan(0);
        largest.Should().BeLessThan(threshold);
        guard.GetProperty("warnings").GetInt32().Should().Be(0);
        report.Warnings.Should().NotContain(w => w.Contains("size guard"));
    }

    [Fact]
    public async Task gltf_sample_renders_on_the_gpu_within_the_practical_criterion()
    {
        //Arrange
        string path = Path.Combine(Path.GetTempPath(), "cycles-gltf-" + Guid.NewGuid().ToString("N") + ".glb");
        SampleModel.Create(withSun: true).SaveGLB(path);
        ModelRoot model = ModelRoot.Load(path);
        File.Delete(path);
        RenderSettings settings = new(48, 32, 16);
        GltfConversionOptions options = new() { Width = settings.Width, Height = settings.Height };
        using CyclesScene gpu = CreateGpuScene();
        GltfSceneConverter.Convert(model, gpu, options);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        GltfSceneConverter.Convert(model, cpu, options);

        //Act
        FilmBuffer reference = cpu.Render(settings);
        FilmBuffer film = await gpu.RenderAsync(settings, null, Token);
        FilmComparison comparison = Judge(gpu, "gltf-sample", film, reference);

        //Assert
        comparison.PracticalFailures.Should().Be(0);
    }

    [Fact]
    public async Task gltf_textured_plane_with_the_blender_mapping_renders_on_the_gpu_within_the_practical_criterion()
    {
        //Arrange: the textured-plane control (image textures, Mix Color, Separate Color, Math, tangent-space normal map, Principled BSDF).
        string directory = Path.Combine(Path.GetTempPath(), "cycles-gltf-textures-" + Guid.NewGuid().ToString("N"));
        SampleModel.WriteTexturedPlaneTextures(directory);
        TextureSet textures = TextureSet.Load(directory);
        Directory.Delete(directory, true);
        ModelRoot model = SampleModel.CreateTexturedPlane();
        RenderSettings settings = new(48, 32, 16);
        GltfConversionOptions options = new() { Width = settings.Width, Height = settings.Height, MaterialMapping = GltfMaterialMapping.Blender, Textures = textures };
        using CyclesScene gpu = CreateGpuScene();
        GltfSceneConverter.Convert(model, gpu, options);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        GltfSceneConverter.Convert(model, cpu, options);

        //Act
        FilmBuffer reference = cpu.Render(settings);
        FilmBuffer film = await gpu.RenderAsync(settings, null, Token);
        FilmComparison comparison = Judge(gpu, "gltf-textured-plane-blender", film, reference);

        //Assert
        comparison.PracticalFailures.Should().Be(0);
    }

    // Records reports on the reporting thread, so the last report is known when the task completes.
    private sealed class SynchronousProgress(ConcurrentQueue<RenderProgress> reports) : IProgress<RenderProgress>
    {
        public void Report(RenderProgress value) => reports.Enqueue(value);
    }
}
