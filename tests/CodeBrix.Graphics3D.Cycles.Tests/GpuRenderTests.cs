// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Lights;
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

    // Judges a GPU film against its backend's authority (GpuReferences) and logs both comparisons. A film that is not an RGBA
    // render (a bake) names its layout for the recorded manifest.
    private FilmComparison Judge(CyclesScene gpu, string name, FilmBuffer film, FilmBuffer cpu, string layout = null)
    {
        ICapabilityReport report = gpu.GetCapabilities();
        Dictionary<string, string> facts = new()
        {
            ["device"] = report.DeviceDescription,
            ["backend"] = report.BackendName,
            ["cyclesMetalRt"] = Environment.GetEnvironmentVariable("CYCLES_METALRT") ?? "unset (Blender's rule: MetalRT on M3 and later)",
            ["libraryVersion"] = CyclesScene.GetBinaryCapabilities().LibraryVersion,
        };
        if (layout != null) facts["layout"] = layout;
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
    public async Task gpu_render_cancelled_during_a_batch_stops_it_and_the_scene_renders_again_afterwards()
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        using CancellationTokenSource cancellation = CancellationTokenSource.CreateLinkedTokenSource(Token);
        ConcurrentQueue<RenderProgress> reports = new();

        //Act
        // One sample first, then a batch the headless scheduler sizes to about 30 seconds: 300 ms after the first count
        // that batch is on the device, far from done.
        Task<FilmBuffer> render = gpu.RenderAsync(WebGpuValidationScenes.Settings with { Samples = 1_000_000 }, new SynchronousProgress(reports), cancellation.Token);
        Stopwatch wait = Stopwatch.StartNew();
        while (!reports.Any(report => report.Stage == RenderStage.Rendering && report.CompletedSamples >= 1) && !render.IsCompleted &&
               wait.Elapsed < TimeSpan.FromSeconds(60))
            await Task.Delay(1, Token);
        await Task.Delay(300, Token);
        uint before = reports.Last(report => report.Stage == RenderStage.Rendering).CompletedSamples ?? 0;
        Stopwatch cancelled = Stopwatch.StartNew();
        cancellation.Cancel();
        Func<Task> awaiting = () => render;

        //Assert
        await awaiting.Should().ThrowAsync<OperationCanceledException>();
        cancelled.Elapsed.Should().BeLessThan(TimeSpan.FromSeconds(10));
        gpu.IsFaulted.Should().BeFalse();
        before.Should().BeGreaterThan(0u);
        reports.Last().Stage.Should().Be(RenderStage.Cancelled);
        reports.Last().CompletedSamples.Should().Be(before);
        FilmBuffer again = await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        Judge(gpu, WebGpuValidationScenes.DiffuseSun, again, RenderCpu(WebGpuValidationScenes.DiffuseSun)).PracticalFailures.Should().Be(0);
    }

    [Fact]
    public async Task gpu_scene_recovers_after_a_rejected_render_once_the_cause_is_removed()
    {
        //Arrange: each backend rejects a scene for its own reason. WebGPU renders the perspective
        //camera only; Cycles' Metal device refuses an image beyond Metal's 16384-pixel texture limit.
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
            // Custom (OSL) cameras are outside the package; WebGPU refuses them by name.
            gpu.Camera.CameraType = CameraCameraType.Custom;
            removeCause = () => gpu.Camera.CameraType = CameraCameraType.Perspective;
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

    // The integer and component nodes with a linked input (a constant one is folded away before the device sees it):
    // every backend renders them, and the WebGPU film is judged against the CPU film.
    [Theory]
    [InlineData("Boolean Math")]
    [InlineData("Integer Math")]
    [InlineData("Get Vector Component")]
    public async Task gpu_renders_the_integer_and_component_nodes_like_the_cpu(string node)
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        foreach (CyclesScene scene in new[] { gpu, cpu })
        {
            WebGpuValidationScenes.Build(scene, WebGpuValidationScenes.DiffuseSun);
            ShaderNode material = scene.CreateShader();
            GeometryNode geometry = material.Add<GeometryNode>();
            EmissionNode emission = material.Add<EmissionNode>();
            switch (node)
            {
                case "Boolean Math":
                    BooleanMathNode boolean = material.Add<BooleanMathNode>();
                    boolean.MathType = BooleanMathMathType.Not;
                    material.Connect(geometry.BackfacingOutput, boolean.Boolean1Input);
                    material.Connect(boolean.BooleanOutput, emission.StrengthInput);
                    break;
                case "Integer Math":
                    IntegerMathNode integer = material.Add<IntegerMathNode>();
                    integer.MathType = IntegerMathMathType.Add;
                    integer.Value2 = 1;
                    material.Connect(geometry.BackfacingOutput, integer.Value1Input);
                    material.Connect(integer.ValueOutput, emission.StrengthInput);
                    break;
                default:
                    GetVectorComponentNode component = material.Add<GetVectorComponentNode>();
                    component.Index = 2;
                    material.Connect(geometry.NormalOutput, component.VectorInput);
                    material.Connect(component.ValueOutput, emission.StrengthInput);
                    break;
            }
            material.Connect(emission.EmissionOutput, material.Output.SurfaceInput);
            material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
            scene.CreateObject(scene.CreateMesh([-.4f, 1.2f, 0, .4f, 1.2f, 0, 0, 1.9f, 0], [0, 1, 2], material));
        }

        //Act
        FilmBuffer film = await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        FilmBuffer onCpu = await cpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);

        //Assert
        gpu.IsFaulted.Should().BeFalse();
        onCpu.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        // The emitter the node drives is in the picture: the film is not the scene's without it.
        onCpu.Span.SequenceEqual(RenderCpu(WebGpuValidationScenes.DiffuseSun).Span).Should().BeFalse();
        // Cycles' Metal device is judged against recorded Metal references, and none is recorded for these three scenes:
        // there the render must succeed and be finite.
        if (gpu.Backend == Backend.Metal) film.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        else Judge(gpu, "integer-and-component-nodes-" + node.Replace(' ', '-').ToLowerInvariant(), film, onCpu).PracticalFailures.Should().Be(0);
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

    // A mesh with true displacement after the device recovery's complete update, which tags every geometry modified (a
    // rejected render first), and edited after a render (DisplacementEditTests: its smooth flags, its object's transform and
    // its displacement shader): the host restores the authored attributes before Cycles displaces the mesh again each time.
    // The recovered film is judged against the film before the rejection, the edited one by the reference policy (on Metal
    // the recorded film `displacement-edit` of a scene built with the edits; on WebGPU the CPU film) and against that scene
    // rendered in the same process.
    [Fact]
    public async Task a_displaced_mesh_after_a_device_recovery_and_edited_after_a_render_renders_like_a_new_scene()
    {
        //Arrange
        RenderSettings settings = DisplacementEditTests.Settings;
        string[] edits = ["smooth", "transform", "displacement-scale"];
        FilmBuffer cpu;
        using (CyclesScene cpuScene = TestScenes.CreateCpuScene())
        {
            DisplacementEditTests.Parts cpuParts = DisplacementEditTests.Build(cpuScene);
            foreach (string edit in edits) DisplacementEditTests.Edit(cpuScene, cpuParts, edit);
            cpu = cpuScene.Render(settings);
        }
        using CyclesScene fresh = CreateGpuScene();
        DisplacementEditTests.Parts freshParts = DisplacementEditTests.Build(fresh);
        foreach (string edit in edits) DisplacementEditTests.Edit(fresh, freshParts, edit);
        FilmBuffer built = await fresh.RenderAsync(settings, null, Token);
        using CyclesScene gpu = CreateGpuScene();
        DisplacementEditTests.Parts parts = DisplacementEditTests.Build(gpu);
        FilmBuffer first = await gpu.RenderAsync(settings, null, Token);
        ConcurrentQueue<RenderEvent> events = new();
        gpu.RenderEventReceived += (sender, e) => events.Enqueue(e);

        //Act
        Action removeCause;
        if (gpu.Backend == Backend.Metal)
        {
            (ObjectNode quad, MeshNode mesh, ShaderNode material) = OversizedTexturedQuad(gpu);
            removeCause = () => { gpu.Remove(quad); gpu.Remove(mesh); gpu.Remove(material); };
        }
        else
        {
            gpu.Camera.CameraType = CameraCameraType.Custom;
            removeCause = () => gpu.Camera.CameraType = CameraCameraType.Perspective;
        }
        Func<Task> rejected = () => gpu.RenderAsync(settings, null, Token);
        await rejected.Should().ThrowAsync<NativeFailure>();
        removeCause();
        FilmBuffer recovered = await gpu.RenderAsync(settings, null, Token);
        foreach (string edit in edits) DisplacementEditTests.Edit(gpu, parts, edit);
        FilmBuffer edited = await gpu.RenderAsync(settings, null, Token);
        FilmComparison recoveredVersusFirst = FilmComparison.Compare(recovered.Span, first.Span);
        FilmComparison editedVersusBuilt = FilmComparison.Compare(edited.Span, built.Span);
        FilmComparison editedVersusFirst = FilmComparison.Compare(edited.Span, first.Span);
        output.WriteLine($"after the device recovery vs before the rejected render: {recoveredVersusFirst}");
        output.WriteLine($"edited after a render vs a scene built with the edits: {editedVersusBuilt}");
        output.WriteLine($"edited vs the first render: {editedVersusFirst}");

        //Assert
        events.Should().Contain(e => e.Kind == RenderEventKind.Status && e.Message.StartsWith("Device recovered", StringComparison.Ordinal));
        recoveredVersusFirst.PracticalFailures.Should().Be(0);
        Judge(fresh, "displacement-edit", built, cpu).PracticalFailures.Should().Be(0);
        Judge(gpu, "displacement-edit", edited, cpu).PracticalFailures.Should().Be(0);
        editedVersusBuilt.PracticalFailures.Should().Be(0);
        editedVersusFirst.PracticalFailures.Should().BeGreaterThan(100);
        gpu.IsFaulted.Should().BeFalse();
        parts.Mesh.GetAttribute("P").Should().Equal(parts.Positions);
    }

    // The largest image side the capability report states for the GPU backend (provisional BackendScope.MaxImageDimension):
    // on Metal an image of exactly that width renders and one pixel more is refused by Cycles' Metal device by name. On
    // WebGPU the stated side (INT32_MAX / 2) cannot be allocated as a test image; the report's value is checked.
    [Fact]
    public async Task the_reported_largest_image_side_renders_on_metal_and_one_pixel_more_is_refused()
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        long? stated = gpu.GetCapabilities().Scope.MaxImageDimension;
        if (gpu.Backend != Backend.Metal)
        {
            stated.Should().Be(1073741823L);
            return;
        }
        async Task<FilmBuffer> RenderWith(uint width)
        {
            using CyclesScene scene = CreateGpuScene();
            WebGpuValidationScenes.Build(scene, WebGpuValidationScenes.DiffuseSun);
            ShaderNode material = scene.CreateShader();
            ImageTextureNode texture = material.Add<ImageTextureNode>();
            DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
            material.Connect(texture.ColorOutput, diffuse.ColorInput);
            material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);
            material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
            byte[] pixels = new byte[width * 4];
            for (int i = 0; i < pixels.Length; i++) pixels[i] = (byte)(i % 4 == 3 ? 255 : 128 + i * 7 % 96);
            using (CyclesImage image = scene.CreateImage<byte>(width, 1, ImageFormat.Byte4, pixels))
                texture.Bind(image);
            scene.CreateObject(scene.CreateMesh([-.3f, -.3f, 0, .3f, -.3f, 0, 0, .3f, 0], [0, 1, 2], material));
            return await scene.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        }

        //Act
        FilmBuffer atLimit = await RenderWith(16384);
        Func<Task> beyond = () => RenderWith(16385);

        //Assert
        stated.Should().Be(16384L);
        atLimit.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        NativeFailure refused = (await beyond.Should().ThrowAsync<NativeFailure>()).Which;
        refused.NativeStatus.Should().Be(NativeStatus.Failed);
        refused.Message.Should().Contain("Texture exceeds maximum allowed size of 16384 x 16384 (requested: 16385 x 1)");
    }

    // A denoiser upscale factor other than 1 (provisional for bakes; DeliveredFilmTests): refused by name before the bake
    // starts on the GPU backend as on every backend, with GPU denoising and with denoising on the CPU (with GPU denoising
    // Cycles' Metal device would bake at half the resolution, which the delivered-film check stays behind to refuse); at a
    // factor of 1 the GPU bakes.
    [Fact]
    public void a_gpu_bake_with_a_denoiser_upscale_factor_is_refused_by_name_before_it_bakes()
    {
        //Arrange
        BakeSettings settings = new(32, 32, 8, BakeType.Combined);
        FilmBuffer Bake(float factor, bool gpuDenoising)
        {
            using CyclesScene scene = CreateGpuScene();
            (_, ObjectNode target) = DeliveredFilmTests.BuildBake(scene);
            scene.Integrator.UseDenoise = true;
            scene.Integrator.DenoiseUseGpu = gpuDenoising;
            scene.Integrator.DenoiserUpscaleFactor = factor;
            return scene.Bake(target, settings);
        }

        //Act
        FilmBuffer plain = Bake(1, true);
        Func<FilmBuffer> gpuUpscaled = () => Bake(2, true);
        Func<FilmBuffer> cpuUpscaled = () => Bake(2, false);

        //Assert
        plain.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        foreach (Func<FilmBuffer> upscaled in new[] { gpuUpscaled, cpuUpscaled })
        {
            NativeFailure refused = upscaled.Should().Throw<NativeFailure>().Which;
            refused.NativeStatus.Should().Be(NativeStatus.Unsupported);
            refused.Message.Should().Contain("Unsupported feature: a denoiser upscale factor other than 1");
        }
    }

    // Area lights with an axis of infinities (provisional; SceneSettingsTests): the host refuses them by name before the GPU
    // device sees the scene, on Metal as on every backend (survey row 7: the CPU and Metal films of such a light differ in
    // 879 practical values, up to 1.1e3); a degenerate area light whose axis holds a NaN renders on the GPU, as on the CPU.
    [Fact]
    public void gpu_area_lights_with_an_axis_of_infinities_are_refused_by_name_and_a_nan_axis_renders()
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        TestScenes.BuildTyped(gpu);
        AreaLightNode light = gpu.CreateLight<AreaLightNode>();
        light.Strength = new System.Numerics.Vector3(6, 6, 6);
        light.Sizeu = 1;
        light.Sizev = 1;
        ObjectNode holder = gpu.CreateObject(light, SceneSettingsTests.AreaLightTransform(1e-23f, 1e-23f, true));
        RenderSettings settings = new(32, 24, 8);

        //Act
        Action infinite = () => gpu.Render(settings);

        //Assert
        CyclesException error = infinite.Should().Throw<CyclesException>().Which;
        error.NativeStatus.Should().Be(NativeStatus.Unsupported);
        error.Message.Should().Contain("Unsupported feature: area lights with an axis of infinities");
        holder.Tfm = SceneSettingsTests.AreaLightTransform(1e-23f, 1, false);
        gpu.Render(settings).Span.ToArray().All(float.IsFinite).Should().BeTrue();
        gpu.IsFaulted.Should().BeFalse();
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

    // Orthographic and panoramic cameras with depth of field, the sampling patterns other than
    // tabulated Sobol and adaptive sampling, set through the managed API on both backends.
    private static void ConfigureCameraAndSampling(CyclesScene scene, string variant)
    {
        CameraNode camera = scene.Camera;
        IntegratorNode integrator = scene.Integrator;
        switch (variant)
        {
            case "orthographic-dof":
                camera.CameraType = CameraCameraType.Orthograph;
                camera.Aperturesize = 0.02f;
                camera.Focaldistance = 4f;
                break;
            case "fisheye-equisolid":
                camera.CameraType = CameraCameraType.Panorama;
                camera.PanoramaType = CameraPanoramaType.FisheyeEquisolid;
                camera.FisheyeLens = 10.5f;
                camera.FisheyeFov = MathF.PI;
                break;
            case "bokeh-sobol-burley":
                camera.Aperturesize = 0.15f;
                camera.Focaldistance = 4f;
                camera.Blades = 6;
                camera.Bladesrotation = 0.3f;
                integrator.SamplingPattern = IntegratorSamplingPattern.SobolBurley;
                break;
            case "adaptive-blue-noise":
                integrator.UseAdaptiveSampling = true;
                integrator.AdaptiveThreshold = 0.05f;
                integrator.AdaptiveMinSamples = 4;
                integrator.SamplingPattern = IntegratorSamplingPattern.BlueNoisePure;
                break;
            case "equirectangular-range":
                camera.CameraType = CameraCameraType.Panorama;
                camera.PanoramaType = CameraPanoramaType.Equirectangular;
                camera.LatitudeMin = -1.2f;
                camera.LatitudeMax = 1.2f;
                camera.LongitudeMin = -2.5f;
                camera.LongitudeMax = 2.5f;
                break;
            case "equiangular-cubemap-face":
                camera.CameraType = CameraCameraType.Panorama;
                camera.PanoramaType = CameraPanoramaType.EquiangularCubemapFace;
                break;
            case "mirrorball":
                camera.CameraType = CameraCameraType.Panorama;
                camera.PanoramaType = CameraPanoramaType.Mirrorball;
                break;
            case "fisheye-equidistant":
                camera.CameraType = CameraCameraType.Panorama;
                camera.PanoramaType = CameraPanoramaType.FisheyeEquidistant;
                camera.FisheyeFov = MathF.PI;
                break;
            case "fisheye-lens-polynomial":
                camera.CameraType = CameraCameraType.Panorama;
                camera.PanoramaType = CameraPanoramaType.FisheyeLensPolynomial;
                camera.FisheyeFov = 3.4f;
                camera.Sensorwidth = 36f;
                camera.Sensorheight = 24f;
                camera.FisheyePolynomialK0 = -1.1735e-05f;
                camera.FisheyePolynomialK1 = -0.07f;
                camera.FisheyePolynomialK2 = -3.3601e-06f;
                camera.FisheyePolynomialK3 = 3.0997e-08f;
                camera.FisheyePolynomialK4 = -6.4e-11f;
                break;
            case "central-cylindrical":
                camera.CameraType = CameraCameraType.Panorama;
                camera.PanoramaType = CameraPanoramaType.PanoramaCentralCylindrical;
                camera.CentralCylindricalRangeUMin = -2.6f;
                camera.CentralCylindricalRangeUMax = 2.6f;
                camera.CentralCylindricalRangeVMin = -1.2f;
                camera.CentralCylindricalRangeVMax = 1.2f;
                break;
            case "anamorphic-bokeh-scrambling":
                camera.Aperturesize = 0.15f;
                camera.Focaldistance = 4f;
                camera.ApertureRatio = 2f;
                integrator.SamplingPattern = IntegratorSamplingPattern.SobolBurley;
                integrator.ScramblingDistance = 0.3f;
                break;
            case "clipped-lens-shift":
                // A shifted viewplane (lens shift) and a near clip that removes part of the scene.
                camera.ViewplaneLeft = -32f / 24f + 0.3f;
                camera.ViewplaneRight = 32f / 24f + 0.3f;
                camera.ViewplaneBottom = -1f;
                camera.ViewplaneTop = 1f;
                camera.Nearclip = 3.5f;
                break;
            case "sample-subset":
                integrator.UseSampleSubset = true;
                integrator.SampleSubsetOffset = 8;
                integrator.SampleSubsetLength = 8;
                break;
            case "camera-border-inner":
                camera.BorderLeft = 0.25f;
                camera.BorderRight = 0.75f;
                camera.BorderBottom = 0.25f;
                camera.BorderTop = 0.625f;
                break;
            case "camera-border-corner":
                camera.BorderLeft = 0.5f;
                camera.BorderBottom = 0f;
                camera.BorderRight = 1f;
                camera.BorderTop = 0.5f;
                break;
            case "pixel-jitter":
                integrator.UsePixelJitter = true;
                break;
            case "custom-pixel-jitter":
                integrator.UsePixelJitter = true;
                integrator.UseCustomPixelJitterSample = true;
                integrator.CustomPixelJitterSample = [0.3f, 0.7f];
                break;
        }
    }

    [Theory]
    [InlineData("orthographic-dof")]
    [InlineData("fisheye-equisolid")]
    [InlineData("bokeh-sobol-burley")]
    [InlineData("adaptive-blue-noise")]
    [InlineData("equirectangular-range")]
    [InlineData("equiangular-cubemap-face")]
    [InlineData("mirrorball")]
    [InlineData("fisheye-equidistant")]
    [InlineData("fisheye-lens-polynomial")]
    [InlineData("central-cylindrical")]
    [InlineData("anamorphic-bokeh-scrambling")]
    [InlineData("clipped-lens-shift")]
    [InlineData("sample-subset")]
    [InlineData("pixel-jitter")]
    [InlineData("custom-pixel-jitter")]
    [InlineData("camera-border-inner")]
    [InlineData("camera-border-corner")]
    public async Task gpu_cameras_and_sampling_features_match_the_cpu_reference(string variant)
    {
        //Arrange
        RenderSettings settings = variant == "adaptive-blue-noise" ? WebGpuValidationScenes.Settings with { Samples = 32 } : WebGpuValidationScenes.Settings;
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        ConfigureCameraAndSampling(gpu, variant);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        WebGpuValidationScenes.Build(cpu, WebGpuValidationScenes.DiffuseSun);
        ConfigureCameraAndSampling(cpu, variant);
        FilmBuffer reference = cpu.Render(settings);
        using CyclesScene plain = TestScenes.CreateCpuScene();
        WebGpuValidationScenes.Build(plain, WebGpuValidationScenes.DiffuseSun);
        FilmBuffer unchanged = plain.Render(settings);

        //Act
        FilmBuffer film = await gpu.RenderAsync(settings, null, Token);

        //Assert
        gpu.IsFaulted.Should().BeFalse();
        if (gpu.Backend == Backend.Metal && variant == "adaptive-blue-noise")
        {
            // Cycles' Metal film of this case differs from the CPU film beyond the practical criterion, so on Metal it
            // is judged against its recorded Metal reference and the CPU comparison is reported (GpuReferences).
            Judge(gpu, "diffuse-sun-adaptive-blue-noise", film, reference).PracticalFailures.Should().Be(0);
        }
        else
        {
            (double maximum, int practical, int strict) = WebGpuValidationScenes.Compare(film.Span, reference.Span);
            output.WriteLine($"{variant}: max abs error {maximum:G6}, practical failures {practical}, strict differences {strict}");
            practical.Should().Be(0);
        }
        // The feature changes the image: the reference differs from the same scene without it.
        WebGpuValidationScenes.Compare(reference.Span, unchanged.Span).Practical.Should().BeGreaterThan(0);
    }

    // An emissive quad behind the boxes whose strength comes from a scene input the host provides: the Scene Time
    // node's frame (scene.SceneAttributes), the Geometry node's Pointiness (a standard attribute) or a colour linked
    // straight into the float Strength input (Cycles' implicit conversion). Without the feature the frame is 1, the
    // attribute is absent, or the strength is the constant 1.
    private static void AddSceneInputQuad(CyclesScene scene, string variant, bool feature)
    {
        ShaderNode shader = scene.CreateShader();
        EmissionNode emission = shader.Add<EmissionNode>();
        emission.Color = new System.Numerics.Vector3(.9f, .8f, .5f);
        emission.Strength = 1f;
        shader.Connect(emission.EmissionOutput, shader.Output.SurfaceInput);
        shader.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        MeshNode quad = scene.CreateMesh([-1.2f, 0, 1.4f, 1.2f, 0, 1.4f, 1.2f, 1.3f, 1.4f, -1.2f, 1.3f, 1.4f], [0, 1, 2, 0, 2, 3], shader);
        scene.CreateObject(quad);
        switch (variant)
        {
            case "scene-time":
                SceneTimeNode time = shader.Add<SceneTimeNode>();
                shader.Connect(time.FrameOutput, emission.StrengthInput);
                if (feature) scene.SceneAttributes.Frame = 2.5f;
                break;
            case "pointiness":
                GeometryNode geometry = shader.Add<GeometryNode>();
                shader.Connect(geometry.PointinessOutput, emission.StrengthInput);
                if (feature) quad.SetAttribute("pointiness", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.Standard, [.5f, 1f, 1.5f, 2f]);
                break;
            case "implicit-conversion":
                ColorNode color = shader.Add<ColorNode>();
                color.Value = new System.Numerics.Vector3(.4f, 2f, 3f);
                if (feature) shader.Connect(color.ColorOutput, emission.StrengthInput);
                break;
        }
    }

    [Theory]
    [InlineData("scene-time")]
    [InlineData("pointiness")]
    [InlineData("implicit-conversion")]
    public async Task gpu_scene_inputs_from_the_host_match_the_cpu_reference(string variant)
    {
        //Arrange
        RenderSettings settings = WebGpuValidationScenes.Settings;
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        AddSceneInputQuad(gpu, variant, true);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        WebGpuValidationScenes.Build(cpu, WebGpuValidationScenes.DiffuseSun);
        AddSceneInputQuad(cpu, variant, true);
        FilmBuffer reference = cpu.Render(settings);
        using CyclesScene plain = TestScenes.CreateCpuScene();
        WebGpuValidationScenes.Build(plain, WebGpuValidationScenes.DiffuseSun);
        AddSceneInputQuad(plain, variant, false);
        FilmBuffer unchanged = plain.Render(settings);

        //Act
        FilmBuffer film = await gpu.RenderAsync(settings, null, Token);

        //Assert
        gpu.IsFaulted.Should().BeFalse();
        (double maximum, int practical, int strict) = WebGpuValidationScenes.Compare(film.Span, reference.Span);
        output.WriteLine($"{variant}: max abs error {maximum:G6}, practical failures {practical}, strict differences {strict}");
        practical.Should().Be(0);
        WebGpuValidationScenes.Compare(reference.Span, unchanged.Span).Practical.Should().BeGreaterThan(0);
    }

    // A point light whose emission strength is the Light Falloff node's linear output, a Metallic
    // BSDF box with the physical conductor Fresnel and a thin film, or a multiple-scattering Sky
    // Texture world with the sun disc and sun guiding (a background light object); without the
    // feature the lamp's emission is constant, the conductor has no film and the world is constant.
    private static void AddLampOrMaterial(CyclesScene scene, string variant, bool feature)
    {
        ShaderNode shader = scene.CreateShader();
        switch (variant)
        {
            case "point-light-falloff":
                EmissionNode emission = shader.Add<EmissionNode>();
                emission.Color = System.Numerics.Vector3.One;
                emission.Strength = 1f;
                if (feature)
                {
                    LightFalloffNode falloff = shader.Add<LightFalloffNode>();
                    falloff.Strength = 1f;
                    falloff.Smooth = .5f;
                    shader.Connect(falloff.LinearOutput, emission.StrengthInput);
                }
                shader.Connect(emission.EmissionOutput, shader.Output.SurfaceInput);
                PointLightNode light = scene.CreateLight<PointLightNode>();
                light.Strength = new System.Numerics.Vector3(40f, 38f, 35f);
                light.Radius = .1f;
                light.UsedShaders = [shader];
                scene.CreateObject(light, Transform3x4.CreateTranslation(.3f, 2.2f, -1.2f));
                break;
            case "metallic-conductor-film":
                MetallicBsdfNode metallic = shader.Add<MetallicBsdfNode>();
                metallic.FresnelType = MetallicBsdfFresnelType.PhysicalConductor;
                metallic.Distribution = MetallicBsdfDistribution.Ggx;
                metallic.Roughness = .25f;
                metallic.Ior = new System.Numerics.Vector3(.18f, .42f, 1.37f);
                metallic.K = new System.Numerics.Vector3(3.42f, 2.35f, 1.77f);
                metallic.ThinFilmThickness = feature ? 320f : 0f;
                metallic.ThinFilmIor = 1.45f;
                shader.Connect(metallic.BsdfOutput, shader.Output.SurfaceInput);
                shader.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
                float[] p = [-.5f, 0, -.5f, .5f, 0, -.5f, .5f, 1, -.5f, -.5f, 1, -.5f, -.5f, 0, .5f, .5f, 0, .5f, .5f, 1, .5f, -.5f, 1, .5f];
                uint[] i = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5, 3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4];
                scene.CreateObject(scene.CreateMesh(p, i, shader), Transform3x4.CreateTranslation(.1f, 0, -1.3f));
                break;
            case "sky-sun-guiding":
                if (!feature)
                    break;
                SkyTextureNode sky = shader.Add<SkyTextureNode>();
                sky.SkyType = SkyTextureSkyType.MultipleScattering;
                sky.SunDisc = true;
                sky.SunSize = .2f;
                sky.SunIntensity = .2f;
                sky.SunElevation = .5f;
                sky.SunRotation = 1.1f;
                BackgroundShaderNode background = shader.Add<BackgroundShaderNode>();
                background.Strength = 1f;
                shader.Connect(sky.ColorOutput, background.ColorInput);
                shader.Connect(background.BackgroundOutput, shader.Output.SurfaceInput);
                shader.EmissionSamplingMethod = ShaderEmissionSamplingMethod.Auto;
                scene.Background.Shader = shader;
                BackgroundLightNode world = scene.CreateLight<BackgroundLightNode>();
                world.MapResolution = 256;
                world.UseMis = true;
                world.Strength = System.Numerics.Vector3.One;
                world.UsedShaders = [shader];
                scene.CreateObject(world);
                break;
        }
    }

    [Theory]
    [InlineData("point-light-falloff")]
    [InlineData("metallic-conductor-film")]
    [InlineData("sky-sun-guiding")]
    public async Task gpu_lamp_shaders_conductor_thin_films_and_sky_worlds_match_the_cpu_reference(string variant)
    {
        //Arrange
        RenderSettings settings = WebGpuValidationScenes.Settings;
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        AddLampOrMaterial(gpu, variant, true);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        WebGpuValidationScenes.Build(cpu, WebGpuValidationScenes.DiffuseSun);
        AddLampOrMaterial(cpu, variant, true);
        FilmBuffer reference = cpu.Render(settings);
        using CyclesScene plain = TestScenes.CreateCpuScene();
        WebGpuValidationScenes.Build(plain, WebGpuValidationScenes.DiffuseSun);
        AddLampOrMaterial(plain, variant, false);
        FilmBuffer unchanged = plain.Render(settings);

        //Act
        FilmBuffer film = await gpu.RenderAsync(settings, null, Token);

        //Assert
        gpu.IsFaulted.Should().BeFalse();
        if (gpu.Backend == Backend.Metal && variant == "sky-sun-guiding")
        {
            // Cycles' Metal film of this case differs from the CPU film beyond the practical criterion, so on Metal it
            // is judged against its recorded Metal reference and the CPU comparison is reported (GpuReferences).
            Judge(gpu, "diffuse-sun-sky-sun-guiding", film, reference).PracticalFailures.Should().Be(0);
        }
        else
        {
            (double maximum, int practical, int strict) = WebGpuValidationScenes.Compare(film.Span, reference.Span);
            output.WriteLine($"{variant}: max abs error {maximum:G6}, practical failures {practical}, strict differences {strict}");
            practical.Should().Be(0);
        }
        // The feature changes the image: the reference differs from the same scene without it.
        WebGpuValidationScenes.Compare(reference.Span, unchanged.Span).Practical.Should().BeGreaterThan(0);
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

    // The shared diffuse-sun scene plus two named panels: a diffuse panel with a value AOV and an
    // emissive panel, both in the light group "panels".
    private static void BuildPassScene(CyclesScene scene)
    {
        WebGpuValidationScenes.Build(scene, WebGpuValidationScenes.DiffuseSun);
        ShaderNode panel = scene.CreateShader();
        panel.Name = "panel-material";
        DiffuseBsdfNode diffuse = panel.Add<DiffuseBsdfNode>();
        diffuse.Color = new System.Numerics.Vector3(.4f, .7f, .3f);
        panel.Connect(diffuse.BsdfOutput, panel.Output.SurfaceInput);
        AovOutputNode aov = panel.Add<AovOutputNode>();
        aov.Name = "Mask";
        aov.Value = .25f;
        panel.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        ShaderNode light = scene.CreateShader();
        light.Name = "light-material";
        EmissionNode emission = light.Add<EmissionNode>();
        emission.Color = new System.Numerics.Vector3(1f, .8f, .5f);
        emission.Strength = 4f;
        light.Connect(emission.EmissionOutput, light.Output.SurfaceInput);
        light.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        ObjectNode a = scene.CreateObject(scene.CreateMesh([-1.6f, 0, 1.2f, -.4f, 0, 1.2f, -.4f, 1.1f, 1.2f, -1.6f, 1.1f, 1.2f], [0, 1, 2, 0, 2, 3], panel));
        ObjectNode b = scene.CreateObject(scene.CreateMesh([.4f, .2f, 1.2f, 1.4f, .2f, 1.2f, 1.4f, .9f, 1.2f, .4f, .9f, 1.2f], [0, 1, 2, 0, 2, 3], light));
        a.Name = "panel";
        b.Name = "light-panel";
        a.Lightgroup = "panels";
        b.Lightgroup = "panels";
    }

    [Fact]
    public async Task gpu_film_passes_match_the_cpu_reference()
    {
        //Arrange: light, data, denoising feature, AOV and light group passes on the GPU backend and
        //the CPU reference. WebGPU films are judged against the CPU film (practical criterion;
        //integer passes exactly); Metal's are reported only.
        using CyclesScene gpu = CreateGpuScene();
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        (PassType Type, string Name)[] passes =
        [
            (PassType.DiffuseDirect, "DiffDir"), (PassType.DiffuseIndirect, "DiffInd"), (PassType.DiffuseColor, "DiffCol"),
            (PassType.Emission, "Emit"), (PassType.Background, "Env"), (PassType.Depth, "Depth"), (PassType.Position, "Position"),
            (PassType.Normal, "Normal"), (PassType.ObjectId, "IndexOB"), (PassType.MaterialId, "IndexMA"), (PassType.Roughness, "Roughness"),
            (PassType.DenoisingAlbedo, "Denoising Albedo"), (PassType.DenoisingNormal, "Denoising Normal"), (PassType.AovValue, "Mask"),
        ];
        foreach (CyclesScene scene in new[] { gpu, cpu })
        {
            BuildPassScene(scene);
            foreach ((PassType type, string name) in passes) scene.AddPass(type, name);
            scene.AddPass(PassType.Combined, "Panels").Lightgroup = "panels";
        }

        //Act
        await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        cpu.Render(WebGpuValidationScenes.Settings);

        //Assert
        foreach (string name in passes.Select(p => p.Name).Append("Panels"))
        {
            FilmBuffer film = gpu.ReadPass(name), reference = cpu.ReadPass(name);
            film.Components.Should().Be(reference.Components);
            FilmComparison comparison = FilmComparison.Compare(film.Span, reference.Span);
            output.WriteLine($"{name}: {comparison}");
            film.Span.ToArray().All(float.IsFinite).Should().BeTrue();
            if (gpu.Backend == Backend.Metal) continue;
            if (name == "IndexOB" || name == "IndexMA") comparison.BitwiseEqual.Should().BeTrue();
            else comparison.PracticalFailures.Should().Be(0);
        }
        cpu.ReadPass("Mask").Span.ToArray().Should().Contain(.25f);
        cpu.ReadPass("Panels").Span.ToArray().Max().Should().BeGreaterThan(0f);
    }

    [Fact]
    public async Task gpu_cryptomatte_passes_match_the_cpu_reference()
    {
        //Arrange: object and material cryptomatte (depth 2) with named panels.
        using CyclesScene gpu = CreateGpuScene();
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        string[] names = ["CryptoObject00", "CryptoObject01", "CryptoMaterial00", "CryptoMaterial01"];
        foreach (CyclesScene scene in new[] { gpu, cpu })
        {
            BuildPassScene(scene);
            scene.Film.CryptomattePasses = FilmCryptomattePasses.Object | FilmCryptomattePasses.Material;
            scene.Film.CryptomatteDepth = 2;
            foreach (string name in names) scene.AddPass(PassType.Cryptomatte, name);
        }

        //Act
        await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        cpu.Render(WebGpuValidationScenes.Settings);

        //Assert: per pixel the (id, weight) slots, ids exactly and weights by the practical criterion
        //(the slot order of equal weights is the accumulation order's).
        foreach (string name in names)
        {
            float[] film = gpu.ReadPass(name).Span.ToArray(), reference = cpu.ReadPass(name).Span.ToArray();
            film.Length.Should().Be(reference.Length);
            int failures = 0;
            for (int pixel = 0; pixel < film.Length; pixel += 4)
            {
                Dictionary<int, float> expected = CryptomatteSlots(reference, pixel), actual = CryptomatteSlots(film, pixel);
                if (!expected.Keys.ToHashSet().SetEquals(actual.Keys)) { failures++; continue; }
                foreach ((int id, float weight) in expected)
                    if (Math.Abs(actual[id] - weight) > 5e-5 + 2e-4 * Math.Abs(weight)) failures++;
            }
            output.WriteLine($"{name}: {failures} differing pixels or weights");
            if (gpu.Backend != Backend.Metal) failures.Should().Be(0);
        }
        // The named panels give distinct ids beside the unnamed objects' common id.
        float[] objects = cpu.ReadPass(names[0]).Span.ToArray();
        Enumerable.Range(0, objects.Length / 2).Select(i => BitConverter.SingleToInt32Bits(objects[2 * i])).Where(id => id != 0).Distinct().Count()
            .Should().BeGreaterThan(2);
    }

    private static Dictionary<int, float> CryptomatteSlots(float[] values, int pixel)
    {
        Dictionary<int, float> slots = [];
        for (int k = 0; k < 4; k += 2)
            if (values[pixel + k] != 0 || values[pixel + k + 1] != 0) slots[BitConverter.SingleToInt32Bits(values[pixel + k])] = values[pixel + k + 1];
        return slots;
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
            report.Scope.RenderTimeRejections.Should().Equal(report.BackendScopes["metal"].RenderTimeRejections);
            report.Scope.RenderTimeRejections.Should().Contain(["a denoiser upscale factor other than 1", "curves and points with a Both displacement shader",
                "area lights with an axis of infinities (an axis with no zero component whose length underflows to zero)"]);
            report.Scope.PassesNote.Should().NotBeNullOrEmpty();
            report.Scope.MaxImageTileSize.Should().Be(128L);
        }
        else
        {
            report.BackendName.Should().Be("webgpu");
            report.PipelineTier.Should().BeOneOf("full", "conservative");
            report.Scope.WrittenPasses.Should().Contain("combined");
            report.Scope.MaxImageTileSize.Should().Be(1024L);
        }
    }

    // The host refuses both before the device sees the scene, on Metal as on every backend: Metal with GPU denoising
    // would return the reduced-resolution render packed into the film, and a Both displacement shader on curves or
    // points has no defined result there either.
    [Fact]
    public void gpu_denoiser_upscale_factor_and_both_displacement_on_curves_and_points_are_refused_by_name()
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        (ShaderNode material, _, _) = TestScenes.BuildTyped(gpu);
        RenderSettings settings = new(32, 24, 8);
        gpu.Integrator.UseDenoise = true;
        gpu.Integrator.DenoiseUseGpu = true;
        gpu.Integrator.DenoiserUpscaleFactor = 2f;
        ShaderNode displaced = TestScenes.BuildBothDisplacementShader(gpu);

        //Act
        Action upscaled = () => gpu.Render(settings);
        Action curves = () => gpu.Render(settings);
        Action points = () => gpu.Render(settings);

        //Assert
        CyclesException upscaleError = upscaled.Should().Throw<CyclesException>().Which;
        upscaleError.NativeStatus.Should().Be(NativeStatus.Unsupported);
        upscaleError.Message.Should().Contain("a denoiser upscale factor other than 1");
        gpu.Integrator.DenoiserUpscaleFactor = 1f;
        gpu.Integrator.UseDenoise = false;
        HairNode hair = gpu.CreateHair([-.5f, -.5f, -1f, 0f, 0f, -1f, .5f, .5f, -1f], [.1f, .1f, .1f], [0u], CurveShape.Thick, displaced);
        gpu.CreateObject(hair);
        CyclesException curvesError = curves.Should().Throw<CyclesException>().Which;
        curvesError.NativeStatus.Should().Be(NativeStatus.Unsupported);
        curvesError.Message.Should().Contain("curves and points with a Both displacement shader");
        hair.UsedShaders = [material];
        gpu.CreateObject(gpu.CreatePointCloud([-.5f, .5f, -1f, .5f, -.5f, -1f], [.2f, .2f], displaced));
        CyclesException pointsError = points.Should().Throw<CyclesException>().Which;
        pointsError.NativeStatus.Should().Be(NativeStatus.Unsupported);
        pointsError.Message.Should().Contain("curves and points with a Both displacement shader");
        gpu.IsFaulted.Should().BeFalse();
    }

    [Fact]
    public void the_tests_run_without_a_pipeline_cache_unless_one_asks()
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();

        //Act
        ICapabilityReport report = gpu.GetCapabilities();

        //Assert
        report.PipelineCacheActive.Should().BeFalse();
        report.PipelineCacheDirectory.Should().BeNull();
        report.PipelineCacheReason.Should().BeOneOf("off", "backend");
    }

    [Fact]
    public void a_configured_pipeline_cache_directory_is_reported_filled_and_reloaded()
    {
        //Arrange
        using (CreateGpuScene()) { }
        string directory = Path.Combine(Path.GetTempPath(), "codebrix-cycles-cache-test-" + Guid.NewGuid().ToString("N"));
        try
        {
            CyclesHost.ConfigurePipelineCache(PipelineCacheMode.Default, directory);

            //Act
            string first;
            using (CyclesScene gpu = new(CyclesBackend.Gpu)) first = gpu.Host.DeviceReportJson();
            string second;
            using (CyclesScene gpu = new(CyclesBackend.Gpu)) second = gpu.Host.DeviceReportJson();
            ICapabilityReport report = CapabilityReport.Parse(second);
            using JsonDocument cold = JsonDocument.Parse(first);
            using JsonDocument warm = JsonDocument.Parse(second);

            //Assert: only the runtime's Vulkan adapters have a cache; elsewhere the same API says so.
            if (!report.PipelineCacheActive)
            {
                report.PipelineCacheReason.Should().BeOneOf("not-offered", "backend");
                return;
            }
            report.PipelineCacheDirectory.Should().Be(directory);
            JsonElement filled = cold.RootElement.GetProperty("pipelineCache");
            JsonElement reloaded = warm.RootElement.GetProperty("pipelineCache");
            filled.GetProperty("misses").GetInt64().Should().BeGreaterThan(0);
            filled.GetProperty("saves").GetInt64().Should().BeGreaterThan(0);
            reloaded.GetProperty("hits").GetInt64().Should().BeGreaterThan(0);
            reloaded.GetProperty("misses").GetInt64().Should().Be(0);
            reloaded.GetProperty("loadedBytes").GetInt64().Should().BeGreaterThan(0);
            reloaded.GetProperty("saveFailures").GetInt64().Should().Be(0);
        }
        finally
        {
            CyclesHost.ConfigurePipelineCache(PipelineCacheMode.Off);
            if (Directory.Exists(directory)) Directory.Delete(directory, true);
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
        threshold.Should().Be(940000);
        largest.Should().BeGreaterThan(0);
        largest.Should().BeLessThan(threshold);
        guard.GetProperty("warnings").GetInt32().Should().Be(0);
        report.Warnings.Should().NotContain(w => w.Contains("size guard"));
    }

    [Fact]
    public async Task curves_added_after_a_gpu_render_match_a_scene_built_with_them()
    {
        //Arrange
        RenderSettings settings = new(48, 32, 8);
        float[] keys = [-.8f, -.6f, 0f, -.3f, -.2f, 0f, .2f, .2f, 0f, .8f, .6f, 0f];
        float[] radii = [.2f, .15f, .15f, .1f];
        using CyclesScene edited = CreateGpuScene();
        TestScenes.BuildTyped(edited);
        await edited.RenderAsync(settings, null, Token);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(cpu);
        cpu.CreateObject(cpu.CreateHair(keys, radii, [0u], CurveShape.Thick));

        //Act
        edited.CreateObject(edited.CreateHair(keys, radii, [0u], CurveShape.Thick));
        FilmBuffer film = await edited.RenderAsync(settings, null, Token);
        FilmBuffer reference = cpu.Render(settings);

        //Assert
        WebGpuValidationScenes.Compare(film.Span, reference.Span).Practical.Should().Be(0);
    }

    [Fact]
    public async Task curves_of_every_shape_render_on_the_gpu_within_the_practical_criterion()
    {
        //Arrange
        RenderSettings settings = new(48, 32, 8);
        static void Build(CyclesScene scene)
        {
            TestScenes.BuildTyped(scene);
            float[] keys = [-.8f, -.6f, 0f, -.3f, -.2f, 0f, .2f, .2f, 0f, .8f, .6f, 0f];
            float[] radii = [.2f, .15f, .15f, .1f];
            HairNode ribbons = scene.CreateHair(keys, radii, [0u], CurveShape.Ribbon);
            scene.CreateObject(ribbons, Transform3x4.CreateTranslation(-.6f, .4f, 0f));
            HairNode thick = scene.CreateHair(keys, radii, [0u], CurveShape.Thick);
            scene.CreateObject(thick, Transform3x4.CreateTranslation(0f, 0f, .1f));
            HairNode linear = scene.CreateHair(keys, radii, [0u], CurveShape.ThickLinear);
            scene.CreateObject(linear, Transform3x4.CreateTranslation(.6f, -.4f, .2f));
        }
        using CyclesScene gpu = CreateGpuScene();
        Build(gpu);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        Build(cpu);

        //Act
        FilmBuffer reference = cpu.Render(settings);
        FilmBuffer film = await gpu.RenderAsync(settings, null, Token);

        //Assert
        WebGpuValidationScenes.Compare(film.Span, reference.Span).Practical.Should().Be(0);
    }

    [Fact]
    public async Task curves_with_deformation_and_object_motion_render_on_the_gpu_within_the_practical_criterion()
    {
        //Arrange
        RenderSettings settings = new(48, 32, 8);
        static void Build(CyclesScene scene)
        {
            TestScenes.BuildTyped(scene);
            scene.Integrator.MotionBlur = true;
            scene.Camera.Shuttertime = .6f;
            float[] keys = [-.8f, -.6f, 0f, -.3f, -.2f, 0f, .2f, .2f, 0f, .8f, .6f, 0f];
            float[] radii = [.2f, .15f, .15f, .1f];
            float[] steps = [.. keys.Select(v => v - .1f), .. keys, .. keys.Select(v => v + .1f)];
            float[] stepRadii = [.. radii.Select(r => r * .5f), .. radii, .. radii.Select(r => r * 1.5f)];
            HairNode thick = scene.CreateHair(keys, radii, [0u], CurveShape.Thick);
            thick.SetMotionKeys(3, steps, stepRadii);
            scene.CreateObject(thick, Transform3x4.CreateTranslation(0f, 0f, .1f));
            HairNode ribbons = scene.CreateHair(keys, radii, [0u], CurveShape.Ribbon);
            ObjectNode moving = scene.CreateObject(ribbons, Transform3x4.CreateTranslation(-.6f, .4f, 0f));
            moving.Motion = [Transform3x4.CreateTranslation(-.8f, .4f, 0f), Transform3x4.CreateTranslation(-.6f, .4f, 0f), Transform3x4.CreateTranslation(-.4f, .5f, 0f)];
        }
        using CyclesScene gpu = CreateGpuScene();
        Build(gpu);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        Build(cpu);

        //Act
        FilmBuffer reference = cpu.Render(settings);
        FilmBuffer film = await gpu.RenderAsync(settings, null, Token);

        //Assert
        WebGpuValidationScenes.Compare(film.Span, reference.Span).Practical.Should().Be(0);
    }

    [Fact]
    public async Task curves_with_transparent_shadows_render_on_the_gpu_within_the_practical_criterion()
    {
        //Arrange
        RenderSettings settings = new(48, 32, 8);
        static void Build(CyclesScene scene)
        {
            TestScenes.BuildTyped(scene);
            ShaderNode shader = scene.CreateShader();
            TransparentBsdfNode transparent = shader.Add<TransparentBsdfNode>();
            transparent.Color = new System.Numerics.Vector3(.6f, .4f, .8f);
            shader.Connect(transparent.BsdfOutput, shader.Output.SurfaceInput);
            float[] keys = [-.8f, -.6f, 0f, -.3f, -.2f, 0f, .2f, .2f, 0f, .8f, .6f, 0f];
            float[] radii = [.2f, .15f, .15f, .1f];
            scene.CreateObject(scene.CreateHair(keys, radii, [0u], CurveShape.Thick, shader), Transform3x4.CreateTranslation(0f, 0f, .1f));
        }
        using CyclesScene gpu = CreateGpuScene();
        Build(gpu);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        Build(cpu);

        //Act
        FilmBuffer reference = cpu.Render(settings);
        FilmBuffer film = await gpu.RenderAsync(settings, null, Token);

        //Assert
        WebGpuValidationScenes.Compare(film.Span, reference.Span).Practical.Should().Be(0);
    }

    [Fact]
    public async Task points_added_after_a_gpu_render_match_a_scene_built_with_them()
    {
        //Arrange
        RenderSettings settings = new(48, 32, 8);
        float[] positions = [-.5f, 0f, 0f, 0f, .2f, .1f, .5f, -.2f, 0f];
        float[] radii = [.25f, .2f, .15f];
        using CyclesScene edited = CreateGpuScene();
        TestScenes.BuildTyped(edited);
        await edited.RenderAsync(settings, null, Token);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(cpu);
        cpu.CreateObject(cpu.CreatePointCloud(positions, radii));

        //Act
        edited.CreateObject(edited.CreatePointCloud(positions, radii));
        FilmBuffer film = await edited.RenderAsync(settings, null, Token);
        FilmBuffer reference = cpu.Render(settings);

        //Assert
        WebGpuValidationScenes.Compare(film.Span, reference.Span).Practical.Should().Be(0);
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

    // The lit triangle scene with a quad behind it and three points in front of it whose material reads a quaternion ("q",
    // per vertex on the quad, per point on the cloud) and spherical-harmonics coefficients ("sh", per face and per point)
    // through Attribute nodes: the quaternion's Color is the diffuse colour, its Vector the emission colour, and its Alpha
    // times the coefficients' Alpha (1 when they are found) the emission strength.
    private static (MeshNode Quad, PointCloudNode Points) BuildAttributeKinds(CyclesScene scene)
    {
        TestScenes.BuildTyped(scene);
        ShaderNode material = scene.CreateShader();
        AttributeNode q = material.Add<AttributeNode>();
        q.Attribute = "q";
        AttributeNode sh = material.Add<AttributeNode>();
        sh.Attribute = "sh";
        sh.Missing = new System.Numerics.Vector3(1f, 0f, 1f);
        sh.MissingAlpha = 0f;
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        EmissionNode emission = material.Add<EmissionNode>();
        MathNode strength = material.Add<MathNode>();
        strength.MathType = MathMathType.Multiply;
        AddClosureNode add = material.Add<AddClosureNode>();
        material.Connect(q.ColorOutput, diffuse.ColorInput);
        material.Connect(q.VectorOutput, emission.ColorInput);
        material.Connect(q.AlphaOutput, strength.Value1Input);
        material.Connect(sh.AlphaOutput, strength.Value2Input);
        material.Connect(strength.ValueOutput, emission.StrengthInput);
        material.Connect(diffuse.BsdfOutput, add.Closure1Input);
        material.Connect(emission.EmissionOutput, add.Closure2Input);
        material.Connect(add.ClosureOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        MeshNode quad = scene.CreateMesh([-2.5f, -1.8f, 1f, 2.5f, -1.8f, 1f, 2.5f, 1.8f, 1f, -2.5f, 1.8f, 1f], [0, 1, 2, 0, 2, 3], material);
        quad.SetAttribute("q", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.None,
            [.9f, .2f, .1f, .3f, .2f, .8f, .3f, .1f, .3f, .3f, .9f, .6f, .7f, .7f, .2f, 0f]);
        quad.SetAttribute("sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Face, AttributeFlags.None,
            Enumerable.Range(0, 2 * 45).Select(i => MathF.Sin(.37f * i)).ToArray());
        scene.CreateObject(quad);
        PointCloudNode points = scene.CreatePointCloud([-.9f, .4f, -.6f, 0f, -.3f, -.8f, .9f, .5f, -.5f], [.3f, .25f, .35f], material);
        points.SetAttribute("q", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.None,
            [.2f, .9f, .4f, .5f, .6f, .3f, .8f, .25f, .85f, .55f, .15f, .75f]);
        points.SetAttribute("sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Vertex, AttributeFlags.None,
            Enumerable.Range(0, 3 * 45).Select(i => MathF.Cos(.29f * i)).ToArray());
        scene.CreateObject(points);
        return (quad, points);
    }

    // Quaternion and spherical-harmonics attributes (provisional value kinds) render on Cycles' Metal device, judged against
    // its recorded reference; the WGSL device has no tables for them and refuses a scene that reads either, by name, until
    // neither is read (RENDER-TIME REJECTIONS).
    [Fact]
    public async Task quaternion_and_spherical_harmonics_attributes_render_on_metal_and_are_refused_by_name_on_webgpu()
    {
        //Arrange
        RenderSettings settings = new(48, 32, 8);
        using CyclesScene gpu = CreateGpuScene();
        (MeshNode quad, PointCloudNode points) = BuildAttributeKinds(gpu);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        BuildAttributeKinds(cpu);
        FilmBuffer reference = cpu.Render(settings);
        Func<Task<FilmBuffer>> render = () => gpu.RenderAsync(settings, null, Token);

        //Act / Assert
        reference.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(10);
        if (gpu.Backend == Backend.WebGpu)
        {
            (await render.Should().ThrowAsync<NativeFailure>()).Which.Message.Should().Contain("WebGPU quaternion attributes not implemented");
            quad.RemoveAttribute("q");
            points.RemoveAttribute("q");
            (await render.Should().ThrowAsync<NativeFailure>()).Which.Message.Should().Contain("WebGPU spherical harmonics attributes not implemented");
            quad.RemoveAttribute("sh");
            points.RemoveAttribute("sh");
            (await render()).Span.ToArray().All(float.IsFinite).Should().BeTrue();
            return;
        }
        FilmBuffer film = await render();
        Judge(gpu, "attribute-quaternion-spherical-harmonics", film, reference).PracticalFailures.Should().Be(0);
        gpu.IsFaulted.Should().BeFalse();
    }

    // Provisional (decision 23): a quaternion on the CurveKey, Curve and Mesh domains of a thick curve covering the view,
    // read by an Attribute node as an emission (GeometryAttributeTests' output scene, one sample): on Metal each output
    // pixel holds the quaternion's w, x, y (Color), their average (Fac) and z (Alpha) as on the CPU; on WebGPU the device
    // refuses the attribute table by name.
    [Theory]
    [InlineData("curves-key")]
    [InlineData("curves-curve")]
    [InlineData("curves-mesh")]
    public async Task quaternion_attributes_on_curves_render_on_metal_like_the_cpu_and_are_refused_by_name_on_webgpu(string domain)
    {
        //Arrange
        float[] q = [.625f, .25f, .375f, .8125f];
        RenderSettings settings = new(8, 8, 1);
        Action<CyclesScene, ShaderNode> placed = GeometryAttributeTests.Place(domain, "q", AttributeValue.Quaternion, q);

        //Act / Assert
        foreach (string socket in new[] { "Color", "Fac", "Alpha" })
        {
            using CyclesScene gpu = CreateGpuScene();
            GeometryAttributeTests.BuildOutputScene(gpu, placed, "q", socket);
            using CyclesScene cpu = TestScenes.CreateCpuScene();
            GeometryAttributeTests.BuildOutputScene(cpu, placed, "q", socket);
            FilmBuffer reference = cpu.Render(settings);
            Func<Task<FilmBuffer>> render = () => gpu.RenderAsync(settings, null, Token);
            if (gpu.Backend == Backend.WebGpu)
            {
                (await render.Should().ThrowAsync<NativeFailure>()).Which.Message.Should().Contain("WebGPU quaternion attributes not implemented");
                continue;
            }
            FilmBuffer film = await render();
            FilmComparison comparison = FilmComparison.Compare(film.Span, reference.Span);
            output.WriteLine($"{domain}, {socket}: Metal vs CPU {comparison}");
            float[] centre = GeometryAttributeTests.Centre(film);
            centre.Should().Equal(GeometryAttributeTests.Centre(reference));
            if (socket == "Color") centre.Should().Equal(q[0], q[1], q[2]);
            if (socket == "Alpha") centre.Should().Equal(q[3], q[3], q[3]);
            comparison.PracticalFailures.Should().Be(0);
            gpu.IsFaulted.Should().BeFalse();
        }
    }

    // The lit triangle scene with two Gaussian splat clouds in front of it (provisional standard attributes radiance:base,
    // scale and rotation): one with Cycles' default splat shader (an emission of the radiance of band 0), one with bands 1
    // to 3 (radiance:sh) and a caller shader that reads the radiance as a diffuse colour plus an emission, on a rotated and
    // uniformly scaled single-user object (never baked: splats stay in object space).
    private static PointCloudNode[] BuildGaussianSplats(CyclesScene scene)
    {
        TestScenes.BuildTyped(scene);
        ShaderNode material = scene.CreateShader();
        AttributeNode radiance = material.Add<AttributeNode>();
        radiance.Attribute = "radiance";
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        EmissionNode emission = material.Add<EmissionNode>();
        emission.Strength = .5f;
        AddClosureNode add = material.Add<AddClosureNode>();
        material.Connect(radiance.ColorOutput, diffuse.ColorInput);
        material.Connect(radiance.ColorOutput, emission.ColorInput);
        material.Connect(diffuse.BsdfOutput, add.Closure1Input);
        material.Connect(emission.EmissionOutput, add.Closure2Input);
        material.Connect(add.ClosureOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        PointCloudNode[] clouds = new PointCloudNode[2];
        for (int c = 0; c < 2; c++)
        {
            const int count = 20;
            float[] positions = Enumerable.Range(0, count).SelectMany(i => new[] { -.9f + .095f * i, .7f * MathF.Sin(1.7f * i + c), -1f + .3f * MathF.Cos(i + 2f * c) }).ToArray();
            PointCloudNode cloud = scene.CreatePointCloud(positions, Enumerable.Repeat(.1f, count).ToArray(), c == 0 ? null : material);
            cloud.RenderAs = PointCloudRenderAs.GaussianSplats;
            cloud.SetAttribute("radiance:base", AttributeValue.Float4, AttributeDomain.Vertex, AttributeFlags.Standard,
                Enumerable.Range(0, count).SelectMany(i => new[] { MathF.Sin(.9f * i + c), MathF.Cos(.7f * i), .8f * MathF.Sin(.4f * i - c), .3f + .033f * i }).ToArray());
            cloud.SetAttribute("scale", AttributeValue.Vector, AttributeDomain.Vertex, AttributeFlags.Standard,
                Enumerable.Range(0, count).SelectMany(i => new[] { .1f + .01f * i, .06f + .02f * (i % 4), .03f }).ToArray());
            cloud.SetAttribute("rotation", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.Standard,
                Enumerable.Range(0, count).SelectMany(i => new[] { MathF.Cos(.2f * i), .6f * MathF.Sin(.2f * i), .8f * MathF.Sin(.2f * i), .1f * c }).ToArray());
            if (c == 1)
                cloud.SetAttribute("radiance:sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Vertex, AttributeFlags.Standard,
                    Enumerable.Range(0, count * 45).Select(k => .3f * MathF.Sin(.53f * k)).ToArray());
            scene.CreateObject(cloud, c == 0 ? null : new Transform3x4(.88f, 0f, .66f, .1f, 0f, 1.1f, 0f, .1f, -.66f, 0f, .88f, .2f));
            clouds[c] = cloud;
        }
        return clouds;
    }

    // Gaussian splats (a point cloud rendered as splats, with the provisional standard attributes) render on Cycles' Metal
    // device, judged against its recorded reference; the WGSL device does not implement them and the host refuses them
    // there by the device's own words until the clouds render as points (RENDER-TIME REJECTIONS).
    [Fact]
    public async Task gaussian_splats_render_on_metal_and_are_refused_by_name_on_webgpu()
    {
        //Arrange
        RenderSettings settings = new(48, 32, 8);
        using CyclesScene gpu = CreateGpuScene();
        PointCloudNode[] clouds = BuildGaussianSplats(gpu);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        BuildGaussianSplats(cpu);
        FilmBuffer reference = cpu.Render(settings);
        Func<Task<FilmBuffer>> render = () => gpu.RenderAsync(settings, null, Token);

        //Act / Assert
        reference.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(10);
        if (gpu.Backend == Backend.WebGpu)
        {
            NativeFailure refused = (await render.Should().ThrowAsync<NativeFailure>()).Which;
            refused.NativeStatus.Should().Be(NativeStatus.Unsupported);
            refused.Message.Should().Contain("WebGPU Gaussian splats not implemented");
            foreach (PointCloudNode cloud in clouds) cloud.RenderAs = PointCloudRenderAs.Points;
            (await render()).Span.ToArray().All(float.IsFinite).Should().BeTrue();
            return;
        }
        FilmBuffer film = await render();
        Judge(gpu, "gaussian-splats", film, reference).PracticalFailures.Should().Be(0);
        gpu.IsFaulted.Should().BeFalse();
        clouds[1].GetAttribute("radius").Should().Equal(Enumerable.Repeat(.1f, 20).ToArray());
    }

    // Provisional (AttributeFlags.SplatBoundingRadius): the bounding radius of each splat, which Cycles' device update
    // computes on the host for every device (PointCloud::update_gsplat_radii), reads on Metal after a render bit for bit as
    // on the CPU and as GaussianSplatTests.BoundingRadii computes it, and is not ready before; the WebGPU backend, which
    // renders no splats, refuses the read by name.
    [Fact]
    public async Task the_splat_bounding_radius_reads_on_metal_as_on_the_cpu_and_is_refused_by_name_on_webgpu()
    {
        //Arrange
        RenderSettings settings = new(48, 32, 4);
        using CyclesScene gpu = CreateGpuScene();
        PointCloudNode[] clouds = BuildGaussianSplats(gpu);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        PointCloudNode[] cpuClouds = BuildGaussianSplats(cpu);
        cpu.Render(settings);
        Func<float[]> bounds = () => clouds[1].GetAttribute("radius", AttributeFlags.SplatBoundingRadius);

        //Act / Assert
        if (gpu.Backend == Backend.WebGpu)
        {
            NativeFailure refused = bounds.Should().Throw<NativeFailure>().Which;
            refused.NativeStatus.Should().Be(NativeStatus.Unsupported);
            refused.Message.Should().Contain("WebGPU Gaussian splats not implemented");
            return;
        }
        bounds.Should().Throw<NativeFailure>().Which.NativeStatus.Should().Be(NativeStatus.NotReady);
        (await gpu.RenderAsync(settings, null, Token)).Span.ToArray().All(float.IsFinite).Should().BeTrue();
        for (int c = 0; c < 2; c++)
        {
            float[] computed = clouds[c].GetAttribute("radius", AttributeFlags.SplatBoundingRadius);
            computed.Should().Equal(cpuClouds[c].GetAttribute("radius", AttributeFlags.SplatBoundingRadius));
            computed.Should().Equal(GaussianSplatTests.BoundingRadii(clouds[c].GetAttribute("radiance:base", AttributeFlags.Standard),
                clouds[c].GetAttribute("scale", AttributeFlags.Standard)));
            clouds[c].GetAttribute("radius").Should().Equal(Enumerable.Repeat(.1f, 20).ToArray());
        }
        gpu.IsFaulted.Should().BeFalse();
    }

    // The lit triangle scene with a quad behind it whose material reads an image texture bound as UDIM tiles (provisional):
    // UVs 0..3 by 0..2 over six tiles, five of them bound to byte4 checkers of different colours and 1013 unbound (the
    // texture's Missing colour), as a diffuse colour plus a faint emission.
    private static (ImageTextureNode Texture, CyclesImage Single) BuildUdimTiles(CyclesScene scene)
    {
        TestScenes.BuildTyped(scene);
        ShaderNode material = scene.CreateShader();
        UvmapNode uv = material.Add<UvmapNode>();
        ImageTextureNode texture = material.Add<ImageTextureNode>();
        texture.Interpolation = ImageTextureInterpolation.Closest;
        texture.Missing = new System.Numerics.Vector3(.9f, .8f, .1f);
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        EmissionNode emission = material.Add<EmissionNode>();
        emission.Strength = .4f;
        AddClosureNode add = material.Add<AddClosureNode>();
        material.Connect(uv.UvOutput, texture.VectorInput);
        material.Connect(texture.ColorOutput, diffuse.ColorInput);
        material.Connect(texture.ColorOutput, emission.ColorInput);
        material.Connect(diffuse.BsdfOutput, add.Closure1Input);
        material.Connect(emission.EmissionOutput, add.Closure2Input);
        material.Connect(add.ClosureOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        MeshNode quad = scene.CreateMesh([-2.5f, -1.8f, 1f, 2.5f, -1.8f, 1f, 2.5f, 1.8f, 1f, -2.5f, 1.8f, 1f], [0, 1, 2, 0, 2, 3], material);
        quad.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, [0f, 0f, 3f, 0f, 3f, 2f, 0f, 0f, 3f, 2f, 0f, 2f]);
        scene.CreateObject(quad);
        UdimTile[] tiles = [.. new[] { 1001, 1002, 1003, 1011, 1012 }.Select((number, k) => new UdimTile(number,
            scene.CreateImage<byte>(4, 4, ImageFormat.Byte4, Enumerable.Range(0, 16).SelectMany(i => ((i % 4) + (i / 4)) % 2 == 0
                ? new byte[] { (byte)(200 - 35 * k), (byte)(40 + 40 * k), 90, 255 }
                : new byte[] { 30, (byte)(180 - 30 * k), (byte)(60 + 45 * k), 255 }).ToArray())))];
        texture.BindTiles(tiles);
        foreach (UdimTile tile in tiles[1..]) tile.Image.Dispose();
        return (texture, tiles[0].Image);
    }

    // UDIM tiles (provisional cbc_host_image_bind_tiles) render on the GPU backend, judged against its authority (the
    // recorded reference on Cycles' Metal device, the CPU film on the WebGPU device); binding one image in their place
    // renders too.
    [Fact]
    public async Task udim_tiles_render_on_the_gpu_backend()
    {
        //Arrange
        RenderSettings settings = new(48, 32, 8);
        using CyclesScene gpu = CreateGpuScene();
        (ImageTextureNode texture, CyclesImage single) = BuildUdimTiles(gpu);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        BuildUdimTiles(cpu).Single.Dispose();
        FilmBuffer reference = cpu.Render(settings);
        Func<Task<FilmBuffer>> render = () => gpu.RenderAsync(settings, null, Token);

        //Act / Assert
        reference.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(10);
        FilmBuffer film = await render();
        Judge(gpu, "udim-tiles", film, reference).PracticalFailures.Should().Be(0);
        if (gpu.Backend == Backend.WebGpu)
        {
            texture.Bind(single);
            (await render()).Span.ToArray().All(float.IsFinite).Should().BeTrue();
        }
        gpu.IsFaulted.Should().BeFalse();
        single.Dispose();
    }

    // Progressive results (provisional HostFlags.ProgressiveResults) on the GPU backend: the validation scene's final film
    // is judged against its authority (the recorded Metal reference of that scene, the CPU film on WebGPU), and against a
    // plain GPU render of it in the same process (practical criterion: GPU accumulation is atomic); the intermediate films
    // arrive and the last one is the returned film.
    [Fact]
    public async Task progressive_results_render_on_the_gpu_backend()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        if (Unavailable.Value != null) Assert.Skip(Unavailable.Value);
        using CyclesScene gpu = new(CyclesBackend.Gpu, 1, HostFlags.ProgressiveResults);
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        ConcurrentQueue<IntermediateFilm> films = new();
        gpu.IntermediateFilmReceived += (_, film) => films.Enqueue(film);
        Func<Task<FilmBuffer>> render = () => gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);

        //Act / Assert
        FilmBuffer film = await render();
        using CyclesScene plain = CreateGpuScene();
        WebGpuValidationScenes.Build(plain, WebGpuValidationScenes.DiffuseSun);
        FilmBuffer plainFilm = await plain.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        FilmComparison versusPlain = FilmComparison.Compare(film.Span, plainFilm.Span);
        output.WriteLine($"progressive vs plain {gpu.Backend} render: {versusPlain}");
        Judge(gpu, WebGpuValidationScenes.DiffuseSun, film, RenderCpu(WebGpuValidationScenes.DiffuseSun)).PracticalFailures.Should().Be(0);
        versusPlain.PracticalFailures.Should().Be(0);
        IntermediateFilm[] received = [.. films];
        received.Length.Should().BeGreaterThanOrEqualTo(2);
        received[0].SamplesDone.Should().Be(1u);
        received.Select(f => f.SamplesDone).Should().BeInAscendingOrder();
        received[^1].SamplesDone.Should().Be(WebGpuValidationScenes.Settings.Samples);
        received[^1].Film.Span.SequenceEqual(film.Span).Should().BeTrue();
        gpu.IsFaulted.Should().BeFalse();
    }

    // The typed scene with a lit quad behind the triangle, textured with a smooth 16385 x 1 byte4 gradient: one pixel wider
    // than Cycles' Metal device accepts (MetalDevice::image_alloc, 16384 x 16384).
    private static void BuildOversizedGradient(CyclesScene scene)
    {
        TestScenes.BuildTyped(scene);
        ShaderNode material = scene.CreateShader();
        UvmapNode uv = material.Add<UvmapNode>();
        ImageTextureNode texture = material.Add<ImageTextureNode>();
        texture.Interpolation = ImageTextureInterpolation.Linear;
        texture.Extension = ImageTextureExtension.Clamp;
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        EmissionNode emission = material.Add<EmissionNode>();
        emission.Strength = .4f;
        AddClosureNode add = material.Add<AddClosureNode>();
        material.Connect(uv.UvOutput, texture.VectorInput);
        material.Connect(texture.ColorOutput, diffuse.ColorInput);
        material.Connect(texture.ColorOutput, emission.ColorInput);
        material.Connect(diffuse.BsdfOutput, add.Closure1Input);
        material.Connect(emission.EmissionOutput, add.Closure2Input);
        material.Connect(add.ClosureOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        MeshNode quad = scene.CreateMesh([-2.5f, -1.8f, 1f, 2.5f, -1.8f, 1f, 2.5f, 1.8f, 1f, -2.5f, 1.8f, 1f], [0, 1, 2, 0, 2, 3], material);
        quad.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, [0f, 0f, 1f, 0f, 1f, 1f, 0f, 0f, 1f, 1f, 0f, 1f]);
        scene.CreateObject(quad);
        const int width = 16385;
        byte[] pixels = [.. Enumerable.Range(0, width).SelectMany(x => new byte[]
        {
            (byte)(30 + 200 * x / (width - 1)), (byte)(220 - 180 * x / (width - 1)), (byte)(128 + 100 * Math.Sin(2 * Math.PI * x / (width - 1))), 255,
        })];
        using CyclesImage image = scene.CreateImage<byte>(width, 1, ImageFormat.Byte4, pixels);
        texture.Bind(image);
    }

    // The texture limit (provisional CyclesScene.TextureLimit) reduces images on the host before any device sees them: an
    // image Cycles' Metal device refuses renders once a limit of 16384 halves it (to 8192 x 1), judged against its recorded
    // reference; the WebGPU device, which takes the image whole too, renders the halved image, judged against the CPU film.
    [Fact]
    public async Task texture_limit_reduces_an_oversized_image_on_the_gpu_backend()
    {
        //Arrange
        RenderSettings settings = new(48, 32, 8);
        using CyclesScene gpu = CreateGpuScene();
        gpu.TextureLimit = 16384;
        BuildOversizedGradient(gpu);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        cpu.TextureLimit = 16384;
        BuildOversizedGradient(cpu);
        FilmBuffer reference = cpu.Render(settings);
        Func<Task<FilmBuffer>> render = () => gpu.RenderAsync(settings, null, Token);

        //Act / Assert
        reference.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(10);
        if (gpu.Backend == Backend.Metal)
        {
            using CyclesScene unlimited = CreateGpuScene();
            BuildOversizedGradient(unlimited);
            NativeFailure oversized = (await ((Func<Task<FilmBuffer>>)(() => unlimited.RenderAsync(settings, null, Token))).Should().ThrowAsync<NativeFailure>()).Which;
            oversized.Message.Should().Contain("16384");
        }
        FilmBuffer film = await render();
        Judge(gpu, "texture-limit", film, reference).PracticalFailures.Should().Be(0);
        gpu.IsFaulted.Should().BeFalse();
    }

    // BVH spatial splits (provisional CyclesScene.UseBvhSpatialSplit) on the GPU backend: Cycles' Metal device honours them
    // in its BVH2 (MetalRT ignores them, as in Cycles) and the WebGPU device traverses Cycles' BVH2. The instanced-slivers
    // scene of BvhSpatialSplitTests with splits (transparent shadows through primitives that several leaves reference) is
    // judged against its authority (the recorded reference on Metal, the CPU film on WebGPU) and against the same scene
    // without splits (practical criterion: GPU accumulation is atomic), and switching them on after a render matches a scene
    // built with them.
    [Fact]
    public async Task bvh_spatial_splits_render_on_the_gpu_backend()
    {
        //Arrange
        RenderSettings settings = new(64, 48, 8);
        using CyclesScene gpu = CreateGpuScene();
        gpu.UseBvhSpatialSplit = true;
        BvhSpatialSplitTests.Build(gpu);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        cpu.UseBvhSpatialSplit = true;
        BvhSpatialSplitTests.Build(cpu);
        FilmBuffer reference = cpu.Render(settings);
        Func<Task<FilmBuffer>> render = () => gpu.RenderAsync(settings, null, Token);

        //Act / Assert
        FilmBuffer film = await render();
        Judge(gpu, "bvh-spatial-split", film, reference).PracticalFailures.Should().Be(0);
        using CyclesScene plain = CreateGpuScene();
        BvhSpatialSplitTests.Build(plain);
        FilmBuffer plainFilm = await plain.RenderAsync(settings, null, Token);
        FilmComparison versusPlain = FilmComparison.Compare(film.Span, plainFilm.Span);
        output.WriteLine($"{gpu.Backend}, spatial splits vs none: {versusPlain}");
        versusPlain.PracticalFailures.Should().Be(0);
        plain.UseBvhSpatialSplit = true;
        FilmComparison toggled = FilmComparison.Compare((await plain.RenderAsync(settings, null, Token)).Span, film.Span);
        output.WriteLine($"{gpu.Backend}, splits switched on after a render vs a scene built with them: {toggled}");
        toggled.PracticalFailures.Should().Be(0);
        gpu.IsFaulted.Should().BeFalse();
        plain.IsFaulted.Should().BeFalse();
    }

    // Tiled rendering (provisional CyclesScene.TileSize) on the GPU backend: the multi-pass scene of TiledRenderingTests
    // (a lamp in a light group, a second, emissive triangle, named passes and cryptomatte) in 12 tiles of 16 pixels, the
    // finished tiles kept in memory, is judged against its authority (the recorded reference on Cycles' Metal device, the
    // CPU film on the WebGPU device), and against the untiled GPU film and passes (practical criterion: GPU accumulation is
    // atomic); with denoising too.
    [Fact]
    public async Task tiled_rendering_renders_on_the_gpu_backend()
    {
        //Arrange
        RenderSettings settings = new(64, 48, 16);
        using CyclesScene gpu = CreateGpuScene();
        TiledRenderingTests.Build(gpu, "passes");
        gpu.TileSize = 16;
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        TiledRenderingTests.Build(cpu, "passes");
        cpu.TileSize = 16;
        FilmBuffer reference = cpu.Render(settings);
        Func<Task<FilmBuffer>> render = () => gpu.RenderAsync(settings, null, Token);

        //Act / Assert
        FilmBuffer film = await render();
        Judge(gpu, "tiled-rendering", film, reference).PracticalFailures.Should().Be(0);
        TiledRenderingTests.Report(gpu).Should().Be((16u, 12L, 1L, 0L));
        string[] passes = ["Depth", "DiffuseColor", "Emission", "Group"];
        Dictionary<string, float[]> tiledPasses = passes.ToDictionary(name => name, name => gpu.ReadPass(name).Span.ToArray());
        using CyclesScene plain = CreateGpuScene();
        TiledRenderingTests.Build(plain, "passes");
        FilmBuffer plainFilm = await plain.RenderAsync(settings, null, Token);
        FilmComparison versusUntiled = FilmComparison.Compare(film.Span, plainFilm.Span);
        output.WriteLine($"{gpu.Backend}, 12 tiles vs untiled: {versusUntiled}");
        versusUntiled.PracticalFailures.Should().Be(0);
        foreach (string name in passes)
        {
            FilmComparison pass = FilmComparison.Compare(tiledPasses[name], plain.ReadPass(name).Span);
            output.WriteLine($"  pass {name}: {pass}");
            pass.PracticalFailures.Should().Be(0, name);
        }
        // Denoising: the full frame read back from memory is denoised once.
        gpu.Integrator.UseDenoise = true;
        plain.Integrator.UseDenoise = true;
        FilmBuffer tiledDenoised = await gpu.RenderAsync(settings, null, Token);
        FilmBuffer untiledDenoised = await plain.RenderAsync(settings, null, Token);
        FilmComparison denoised = FilmComparison.Compare(tiledDenoised.Span, untiledDenoised.Span);
        output.WriteLine($"{gpu.Backend} denoised, 12 tiles vs untiled: {denoised}");
        denoised.PracticalFailures.Should().Be(0);
        TiledRenderingTests.Report(gpu).Should().Be((16u, 24L, 2L, 0L));
        gpu.IsFaulted.Should().BeFalse();
        plain.IsFaulted.Should().BeFalse();
    }

    // The lit triangle scene with two quads behind it whose material reads a tiled image (provisional CreateTiledImage) of
    // a 256 x 256 byte4 pattern with box-filtered mip levels: the left quad minified (its UVs span the image twice, so
    // Cycles' texture cache selects the smallest level), the right one magnified (a few texels: level 0).
    private static (ImageTextureNode Texture, TiledImageTests.LevelReader Reader) BuildTiledImage(CyclesScene scene, bool plain = false)
    {
        TestScenes.BuildTyped(scene);
        ShaderNode material = scene.CreateShader();
        UvmapNode uv = material.Add<UvmapNode>();
        ImageTextureNode texture = material.Add<ImageTextureNode>();
        texture.Interpolation = ImageTextureInterpolation.Closest;
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        EmissionNode emission = material.Add<EmissionNode>();
        emission.Strength = .4f;
        AddClosureNode add = material.Add<AddClosureNode>();
        material.Connect(uv.UvOutput, texture.VectorInput);
        material.Connect(texture.ColorOutput, diffuse.ColorInput);
        material.Connect(texture.ColorOutput, emission.ColorInput);
        material.Connect(diffuse.BsdfOutput, add.Closure1Input);
        material.Connect(emission.EmissionOutput, add.Closure2Input);
        material.Connect(add.ClosureOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        MeshNode quads = scene.CreateMesh([-2.5f, -1.8f, 1f, 0f, -1.8f, 1f, 0f, 1.8f, 1f, -2.5f, 1.8f, 1f, 0f, -1.8f, 1f, 2.5f, -1.8f, 1f, 2.5f, 1.8f, 1f, 0f, 1.8f, 1f],
            [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7], material);
        quads.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard,
            [0f, 0f, 2f, 0f, 2f, 1.5f, 0f, 0f, 2f, 1.5f, 0f, 1.5f, .45f, .45f, .49f, .45f, .49f, .51f, .45f, .45f, .49f, .51f, .45f, .51f]);
        scene.CreateObject(quads);
        byte[] pixels = [.. Enumerable.Range(0, 256 * 256).SelectMany(i => ((i % 256 / 16) + (i / 256 / 16)) % 2 == 0
            ? new byte[] { (byte)(200 - i % 256 / 2), (byte)(40 + i / 256 / 2), 90, 255 }
            : new byte[] { 30, (byte)(180 - i / 256 / 3), (byte)(60 + i % 256 / 3), 255 })];
        TiledImageTests.LevelReader reader = plain ? null : new(256, 256, 4, TiledImageTests.BoxLevels(pixels, 256, 256, 4));
        using CyclesImage image = plain ? scene.CreateImage<byte>(256, 256, ImageFormat.Byte4, pixels) : scene.CreateTiledImage(256, 256, ImageFormat.Byte4, reader);
        texture.Bind(image);
        return (texture, reader);
    }

    // Decision 28(b): a tiled image inside a tiled render (tiles of 16), inside a camera-border render and inside both, on
    // the GPU backend and on the CPU reference, each against the same render with the same pixels bound as a plain image.
    // The scene reads level 0 only (TiledImageTests: an orthographic view of a 256 x 256 pattern magnified to half a texel
    // per pixel across four of its 64-pixel tiles, an emission, no bounce), so the two must agree: Closest renders the same
    // texels. Cycles restarts a path after a texture cache miss on a GPU by running the kernel that missed again with the
    // path's state, whose render_pixel_index addresses the tile's or border's buffer; only a miss of the camera ray itself
    // rebuilds the pixel from the camera width (integrator_init_from_camera with no tile, kernel/integrator/init_from_camera.h),
    // and only an OSL camera can miss there (camera_sample_custom), which this library does not include.
    [Theory]
    [InlineData("tiles")]
    [InlineData("border")]
    [InlineData("border-tiles")]
    public async Task a_tiled_image_renders_inside_a_tiled_render_and_a_camera_border_like_the_image_bound_plainly(string variant)
    {
        //Arrange
        RenderSettings settings = new(128, 128, 1);
        byte[] pixels = TiledImageTests.Pattern(256, 256);
        TiledImageTests.LevelReader Build(CyclesScene scene, bool plain)
        {
            TiledImageTests.Configure(scene);
            ImageTextureNode texture = TiledImageTests.Quad(scene, .125f, .125f, .375f, .375f);
            TiledImageTests.LevelReader reader = plain ? null : new(256, 256, 4, TiledImageTests.BoxLevels(pixels, 256, 256, 4));
            using (CyclesImage image = plain ? scene.CreateImage<byte>(256, 256, ImageFormat.Byte4, pixels) : scene.CreateTiledImage(256, 256, ImageFormat.Byte4, reader))
                texture.Bind(image);
            if (variant.Contains("tiles")) scene.TileSize = 16;
            if (variant.Contains("border"))
            {
                scene.Camera.BorderLeft = .25f;
                scene.Camera.BorderRight = .8125f;
                scene.Camera.BorderBottom = .1875f;
                scene.Camera.BorderTop = .625f;
            }
            return reader;
        }
        using CyclesScene gpu = CreateGpuScene();
        TiledImageTests.LevelReader reader = Build(gpu, false);
        using CyclesScene gpuPlain = CreateGpuScene();
        Build(gpuPlain, true);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        TiledImageTests.LevelReader cpuReader = Build(cpu, false);
        using CyclesScene cpuPlain = TestScenes.CreateCpuScene();
        Build(cpuPlain, true);

        //Act
        FilmBuffer cpuFilm = cpu.Render(settings), cpuPlainFilm = cpuPlain.Render(settings);
        output.WriteLine($"{variant}, CPU: tiled image vs plain image {FilmComparison.Compare(cpuFilm.Span, cpuPlainFilm.Span)}; " +
            $"{cpuReader.Reads.Count} reads, levels {string.Join(",", cpuReader.Reads.Select(r => r.Level).Distinct().Order())}");

        //Assert
        cpuPlainFilm.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(100);
        cpuReader.Reads.Select(r => r.Level).Distinct().Should().Equal(0);
        cpuReader.Reads.Count.Should().BeGreaterThan(1);
        cpuFilm.Span.SequenceEqual(cpuPlainFilm.Span).Should().BeTrue();
        if (gpu.Backend == Backend.WebGpu)
        {
            NativeFailure refused = (await ((Func<Task<FilmBuffer>>)(() => gpu.RenderAsync(settings, null, Token))).Should().ThrowAsync<NativeFailure>()).Which;
            refused.NativeStatus.Should().Be(NativeStatus.Unsupported);
            return;
        }
        FilmBuffer film = await gpu.RenderAsync(settings, null, Token), plainFilm = await gpuPlain.RenderAsync(settings, null, Token);
        FilmComparison comparison = FilmComparison.Compare(film.Span, plainFilm.Span);
        output.WriteLine($"{variant}, Metal: tiled image vs plain image {comparison}; vs the CPU's tiled image {FilmComparison.Compare(film.Span, cpuFilm.Span)}; " +
            $"{reader.Reads.Count} reads, levels {string.Join(",", reader.Reads.Select(r => r.Level).Distinct().Order())}");
        reader.Reads.Select(r => r.Level).Distinct().Should().Equal(0);
        reader.Reads.Count.Should().BeGreaterThan(1);
        comparison.BitwiseEqual.Should().BeTrue();
        gpu.IsFaulted.Should().BeFalse();
    }

    // Tiled images (provisional CyclesScene.CreateTiledImage, Cycles' texture cache) on Cycles' Metal device, whose cache
    // misses restart the path and load the requested tiles between kernel steps: judged against the recorded Metal
    // reference, CPU comparison reported; a reader that throws fails the Metal render by name and the next render reads
    // the tile again. The host refuses a tiled image on the WebGPU backend by name (its capability entry "sparse or tiled
    // images", RENDER-TIME REJECTIONS).
    [Fact]
    public async Task tiled_images_render_on_metal_and_are_refused_by_name_on_webgpu()
    {
        //Arrange
        RenderSettings settings = new(48, 32, 8);
        using CyclesScene gpu = CreateGpuScene();
        (_, TiledImageTests.LevelReader reader) = BuildTiledImage(gpu);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        (_, TiledImageTests.LevelReader cpuReader) = BuildTiledImage(cpu);
        FilmBuffer reference = cpu.Render(settings);
        Func<Task<FilmBuffer>> render = () => gpu.RenderAsync(settings, null, Token);

        //Act / Assert
        reference.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(10);
        cpuReader.Reads.Select(r => r.Level).Distinct().Count().Should().BeGreaterThan(1);
        if (gpu.Backend == Backend.WebGpu)
        {
            NativeFailure refused = (await render.Should().ThrowAsync<NativeFailure>()).Which;
            refused.NativeStatus.Should().Be(NativeStatus.Unsupported);
            refused.Message.Should().Contain("WebGPU tiled images not implemented");
            reader.Reads.Should().BeEmpty();
            return;
        }
        FilmBuffer film = await render();
        output.WriteLine($"Metal tiled image: {reader.Reads.Count} reads, levels {string.Join(",", reader.Reads.Select(r => r.Level).Distinct().Order())}; CPU {cpuReader.Reads.Count} reads");
        Judge(gpu, "tiled-image", film, reference).PracticalFailures.Should().Be(0);
        reader.Overlapped.Should().BeFalse();
        reader.Reads.Select(r => r.Level).Distinct().Count().Should().BeGreaterThan(1);
        gpu.IsFaulted.Should().BeFalse();
        using CyclesScene failing = CreateGpuScene();
        (_, TiledImageTests.LevelReader failingReader) = BuildTiledImage(failing);
        InvalidOperationException thrown = new("unreadable tile");
        failingReader.Fail = (_, _, _, _, _) => thrown;
        CyclesException failure = (await ((Func<Task<FilmBuffer>>)(() => failing.RenderAsync(settings, null, Token))).Should().ThrowAsync<CyclesException>()).Which;
        failure.NativeStatus.Should().Be(NativeStatus.Failed);
        failure.InnerException.Should().BeSameAs(thrown);
        failure.Message.Should().Contain("the tile reader failed");
        failingReader.Fail = null;
        FilmComparison retried = FilmComparison.Compare((await failing.RenderAsync(settings, null, Token)).Span, film.Span);
        output.WriteLine($"Metal, the render after a failed read vs the first Metal film: {retried}");
        retried.PracticalFailures.Should().Be(0);
    }

    // Provisional tile sizes on Cycles' Metal device, at each size it takes (64 and 128: its tile pool of 64 tiles of
    // size + 4 pixels is at most 16384 wide): the mip levels selected by the footprint equal plain images of the level
    // colours, and a magnified pattern across a tile edge equals the same pixels bound plainly, each bit for bit on Metal
    // (the CPU comparison reported); 256 is refused by name. On WebGPU the image is created and its render refused.
    [Theory]
    [InlineData(64)]
    [InlineData(128)]
    public async Task tiled_images_at_every_metal_tile_size_select_mip_levels_and_render_like_plain_images(int tileSize)
    {
        //Arrange
        using CyclesScene strip = CreateGpuScene();
        TiledImageTests.LevelReader stripReader = TiledImageTests.TileSizeStrip(strip, tileSize);
        Func<Task<FilmBuffer>> renderStrip = () => strip.RenderAsync(TiledImageTests.StripSettings, null, Token);

        //Act / Assert
        if (strip.Backend == Backend.WebGpu)
        {
            NativeFailure refused = (await renderStrip.Should().ThrowAsync<NativeFailure>()).Which;
            refused.NativeStatus.Should().Be(NativeStatus.Unsupported);
            refused.Message.Should().Contain("WebGPU tiled images not implemented");
            stripReader.Reads.Should().BeEmpty();
            return;
        }
        FilmBuffer film = await renderStrip();
        using CyclesScene colors = CreateGpuScene();
        FilmBuffer plainColors = TiledImageTests.StripOfColors(colors, 0, 1, 2, 3);
        output.WriteLine($"Metal tile size {tileSize}: strip vs plain level colours {FilmComparison.Compare(film.Span, plainColors.Span)}; " +
            $"{stripReader.Reads.Count} reads, levels {string.Join(",", stripReader.Reads.Select(r => r.Level).Distinct().Order())}, widest {stripReader.Reads.Max(r => r.Width)}");
        film.Span.SequenceEqual(plainColors.Span).Should().BeTrue();
        stripReader.Reads.Select(r => r.Level).Distinct().Order().Should().Equal(0, 1, 2, 3);
        stripReader.Reads.Max(r => r.Width).Should().BeInRange(tileSize + 1, tileSize + 4);

        using CyclesScene plainScene = CreateGpuScene();
        using CyclesScene tiledScene = CreateGpuScene();
        (FilmBuffer plain, _) = TiledImageTests.TileSizePattern(plainScene, tileSize, false);
        (FilmBuffer tiled, TiledImageTests.LevelReader reader) = TiledImageTests.TileSizePattern(tiledScene, tileSize, true);
        using CyclesScene cpuScene = TestScenes.CreateCpuScene();
        (FilmBuffer cpu, _) = TiledImageTests.TileSizePattern(cpuScene, tileSize, true);
        output.WriteLine($"Metal tile size {tileSize}: tiled vs plain {FilmComparison.Compare(tiled.Span, plain.Span)}; tiled vs CPU tiled {FilmComparison.Compare(tiled.Span, cpu.Span)}");
        tiled.Span.SequenceEqual(plain.Span).Should().BeTrue();
        reader.Reads.All(r => r.Level == 0).Should().BeTrue();
        // Tiles 0 and 1 of level 0, each read from 2 pixels before it (its border).
        reader.Reads.Select(r => (r.X + 2) / tileSize).Distinct().Count().Should().BeGreaterThanOrEqualTo(2);

        using CyclesScene larger = CreateGpuScene();
        Action tooLarge = () => larger.CreateTiledImage(512, 512, ImageFormat.Byte4, new TiledImageTests.LevelReader(512, 512, 4, TiledImageTests.SolidLevels(512, 512)), 256);
        NativeFailure error = tooLarge.Should().Throw<NativeFailure>().Which;
        error.NativeStatus.Should().Be(NativeStatus.Invalid);
        error.Message.Should().Contain("Invalid tiled image tile size 256: 0 (the default, 64) or a power of two from 64 to 128 on the Metal backend");
        strip.IsFaulted.Should().BeFalse();
        tiledScene.IsFaulted.Should().BeFalse();
    }

    // Subdivision surfaces bake (provisional): the host maps the texels through Cycles' tessellation of the cage, which every
    // device receives as triangles, and the device bakes them as any mesh kept in object space. Each bake type of
    // SubdivisionBakingTests, with and without true displacement, is judged by the reference policy (on Metal against its
    // recorded reference, the CPU comparison reported; on WebGPU against the CPU bake) and against the same device's bake of the
    // ordinary mesh built from the same tessellation (practical criterion: GPU accumulation is atomic). Pixel-space dicing is
    // refused by name on the GPU backend as on every backend.
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task subdivision_surfaces_bake_on_the_gpu_like_the_mesh_of_their_tessellation(bool displaced)
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        (ObjectNode target, MeshNode mesh) = SubdivisionBakingTests.Build(gpu, displaced);
        using CyclesScene plain = CreateGpuScene();
        (ObjectNode plainTarget, _) = SubdivisionBakingTests.BuildPlain(plain, SubdivisionBakingTests.Diced(), displaced);
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        (ObjectNode cpuTarget, _) = SubdivisionBakingTests.Build(cpu, displaced);
        Func<Task<FilmBuffer>> pixelSpace = () => gpu.BakeAsync(target, SubdivisionBakingTests.Settings(BakeType.Combined), null, Token);

        //Act / Assert
        foreach (BakeType type in SubdivisionBakingTests.Types)
        {
            BakeSettings settings = SubdivisionBakingTests.Settings(type);
            FilmBuffer film = await gpu.BakeAsync(target, settings, null, Token);
            FilmBuffer plainFilm = await plain.BakeAsync(plainTarget, settings, null, Token);
            FilmBuffer reference = cpu.Bake(cpuTarget, settings);
            string kind = type switch { BakeType.DiffuseColor => "diffuse-color", _ => type.ToString().ToLowerInvariant() };
            string name = "subdivision-bake-" + kind + (displaced ? "-displaced" : "");
            FilmComparison judged = Judge(gpu, name, film, reference, $"float32 little-endian, {settings.Components} components per texel, row 0 at v = 0 (a {kind} bake)");
            FilmComparison versusPlain = FilmComparison.Compare(film.Span, plainFilm.Span);
            output.WriteLine($"{name}: vs the same device's bake of the ordinary mesh of the tessellation: {versusPlain}");
            judged.PracticalFailures.Should().Be(0, name);
            versusPlain.PracticalFailures.Should().Be(0, name);
        }
        mesh.SubdAdaptiveSpace = MeshSubdAdaptiveSpace.Pixel;
        NativeFailure refused = (await pixelSpace.Should().ThrowAsync<NativeFailure>()).Which;
        refused.NativeStatus.Should().Be(NativeStatus.Unsupported);
        refused.Message.Should().Contain("bakes of subdivision surfaces diced in pixel space");
        gpu.IsFaulted.Should().BeFalse();
        plain.IsFaulted.Should().BeFalse();
    }

    // Records reports on the reporting thread, so the last report is known when the task completes.
    private sealed class SynchronousProgress(ConcurrentQueue<RenderProgress> reports) : IProgress<RenderProgress>
    {
        public void Report(RenderProgress value) => reports.Enqueue(value);
    }
}
