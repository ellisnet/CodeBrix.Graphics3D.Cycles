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

    // The integer and component nodes are not part of the WGSL device: a graph that reaches one with a linked input is
    // refused with the node's name (a constant one is folded away before the device sees it), and the CPU renders it.
    [Theory]
    [InlineData("Boolean Math")]
    [InlineData("Integer Math")]
    [InlineData("Get Vector Component")]
    public async Task webgpu_refuses_the_integer_and_component_nodes_by_name_and_the_cpu_renders_them(string node)
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        if (gpu.Backend == Backend.Metal) Assert.Skip("Cycles' Metal device renders these nodes; the refusal is the WGSL device's.");
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
                    component.Index = 1;
                    material.Connect(geometry.NormalOutput, component.VectorInput);
                    material.Connect(component.ValueOutput, emission.StrengthInput);
                    break;
            }
            material.Connect(emission.EmissionOutput, material.Output.SurfaceInput);
            material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
            scene.CreateObject(scene.CreateMesh([-.4f, 1.2f, 0, .4f, 1.2f, 0, 0, 1.9f, 0], [0, 1, 2], material));
        }

        //Act
        Func<Task> render = () => gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        FilmBuffer onCpu = await cpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);

        //Assert
        (await render.Should().ThrowAsync<NativeFailure>()).Which.Message.Should().Contain("WebGPU " + node + " node not implemented");
        onCpu.Span.ToArray().All(float.IsFinite).Should().BeTrue();
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
            report.Scope.RenderTimeRejections.Should().NotContain(rejection => rejection.Contains("upscale"));
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

    // Records reports on the reporting thread, so the last report is known when the task completes.
    private sealed class SynchronousProgress(ConcurrentQueue<RenderProgress> reports) : IProgress<RenderProgress>
    {
        public void Report(RenderProgress value) => reports.Enqueue(value);
    }
}
