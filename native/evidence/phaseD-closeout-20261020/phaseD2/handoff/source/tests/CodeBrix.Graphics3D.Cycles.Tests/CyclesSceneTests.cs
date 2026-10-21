// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Linq;
using System.Numerics;
using System.Threading;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Lights;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Nodes;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Exercises the typed facade against the native host built in this working tree on the CPU
// reference backend. Skips (never fails) when that build is absent.
public class CyclesSceneTests
{
    private static readonly RenderSettings Small = new(32, 24, 8);

    private static CancellationToken Token => TestContext.Current.CancellationToken;

    [Fact]
    public void ResolveBackend_maps_cpu_reference_explicitly()
        => CyclesScene.ResolveBackend(CyclesBackend.CpuReference).Should().Be(Backend.CpuReference);

    [Fact]
    public void ResolveBackend_maps_gpu_to_webgpu_or_metal_by_platform()
    {
        //Arrange
        bool appleSilicon = OperatingSystem.IsMacOS() && System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture == System.Runtime.InteropServices.Architecture.Arm64;

        //Act
        Backend backend = CyclesScene.ResolveBackend(CyclesBackend.Gpu);

        //Assert
        backend.Should().Be(appleSilicon ? Backend.Metal : Backend.WebGpu);
    }

    [Fact]
    public void Render_of_a_typed_scene_is_finite_and_bit_identical_across_runs()
    {
        //Arrange
        using CyclesScene first = TestScenes.CreateCpuScene();
        using CyclesScene second = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(first);
        TestScenes.BuildTyped(second);

        //Act
        FilmBuffer a = first.Render(Small);
        FilmBuffer b = first.Render(Small);
        FilmBuffer c = second.Render(Small);

        //Assert
        a.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        a.Span.ToArray().Any(value => value > 0).Should().BeTrue();
        a.Span.SequenceEqual(b.Span).Should().BeTrue();
        a.Span.SequenceEqual(c.Span).Should().BeTrue();
        a.Width.Should().Be(32);
        a.Height.Should().Be(24);
        a.Stride.Should().Be(32 * 4);
        a.Pass.Should().Be(RenderPass.Combined);
    }

    [Fact]
    public void Render_of_a_typed_scene_matches_the_by_name_host_bitwise()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        using CyclesHost host = new(Backend.CpuReference);
        TestScenes.BuildByName(host);
        float[] byName = new float[Small.FloatCount];

        //Act
        FilmBuffer typed = scene.Render(Small);
        host.Render(Small.Width, Small.Height, Small.Samples, byName);

        //Assert
        typed.Span.SequenceEqual(byName).Should().BeTrue();
    }

    [Fact]
    public async Task RenderAsync_matches_the_synchronous_render_and_reports_progress()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        FilmBuffer expected = scene.Render(Small);
        ConcurrentQueue<RenderProgress> reports = new();

        //Act
        FilmBuffer film = await scene.RenderAsync(Small, new SynchronousProgress(reports), Token);

        //Assert
        film.Span.SequenceEqual(expected.Span).Should().BeTrue();
        reports.First().Stage.Should().Be(RenderStage.Starting);
        reports.Last().Stage.Should().Be(RenderStage.Completed);
        reports.Last().Fraction.Should().Be(1);
    }

    [Fact]
    public async Task RenderAsync_cancels_promptly_and_leaves_the_destination_untouched()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        float[] pixels = Enumerable.Repeat(-99f, (int)Small.FloatCount).ToArray();
        using CancellationTokenSource cancellation = CancellationTokenSource.CreateLinkedTokenSource(Token);
        ConcurrentQueue<RenderProgress> reports = new();

        //Act
        Task render = scene.RenderAsync(Small with { Samples = 1_000_000 }, pixels, new SynchronousProgress(reports), cancellation.Token);
        Stopwatch wait = Stopwatch.StartNew();
        while (!scene.IsRendering && !render.IsCompleted && wait.Elapsed < TimeSpan.FromSeconds(30)) await Task.Delay(1, Token);
        Stopwatch cancelled = Stopwatch.StartNew();
        cancellation.Cancel();
        Func<Task> awaiting = () => render;

        //Assert
        await awaiting.Should().ThrowAsync<OperationCanceledException>();
        cancelled.Elapsed.Should().BeLessThan(TimeSpan.FromSeconds(10));
        pixels.All(value => value == -99f).Should().BeTrue();
        reports.Last().Stage.Should().Be(RenderStage.Cancelled);
        scene.Render(Small).Span.ToArray().All(float.IsFinite).Should().BeTrue();
    }

    [Fact]
    public async Task RenderAsync_with_a_cancelled_token_does_not_start()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        using CancellationTokenSource cancellation = new();
        cancellation.Cancel();

        //Act
        Func<Task> render = () => scene.RenderAsync(Small, null, cancellation.Token);

        //Assert
        await render.Should().ThrowAsync<OperationCanceledException>();
        scene.IsRendering.Should().BeFalse();
    }

    public static TheoryData<ImageFormat> AllImageFormats => new(Enum.GetValues<ImageFormat>());

    [Theory]
    [MemberData(nameof(AllImageFormats))]
    public void CreateImage_uploads_and_renders_every_image_format(ImageFormat format)
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (ShaderNode material, DiffuseBsdfNode diffuse, MeshNode mesh) = TestScenes.BuildTyped(scene);
        mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, [-.3f, -.2f, 1.7f, .1f, .1f, 1.4f]);
        ImageTextureNode texture = material.Add<ImageTextureNode>();
        UvmapNode uv = material.Add<UvmapNode>();
        material.Connect(uv.UvOutput, texture.VectorInput);
        material.Connect(texture.ColorOutput, diffuse.ColorInput);
        texture.Interpolation = ImageTextureInterpolation.Closest;
        int components = 7 * 5 * CyclesScene.Channels(format);

        //Act
        using CyclesImage image = format switch
        {
            ImageFormat.Float or ImageFormat.Float4 => scene.CreateImage(7, 5, format, Ramp(components, i => i / (float)components)),
            ImageFormat.Half or ImageFormat.Half4 => scene.CreateImage(7, 5, format, Ramp(components, i => (Half)(i / (float)components))),
            ImageFormat.Byte or ImageFormat.Byte4 => scene.CreateImage(7, 5, format, Ramp(components, i => (byte)(i * 255 / components))),
            _ => scene.CreateImage(7, 5, format, Ramp(components, i => (ushort)(i * 65535 / components)))
        };
        texture.Bind(image);
        FilmBuffer textured = scene.Render(Small);
        texture.Unbind();
        FilmBuffer unbound = scene.Render(Small);

        //Assert
        image.Format.Should().Be(format);
        textured.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        textured.Span.SequenceEqual(unbound.Span).Should().BeFalse();
    }

    [Fact]
    public void CreateImage_rejects_a_component_type_that_does_not_match_the_format()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        Action upload = () => scene.CreateImage<float>(2, 2, ImageFormat.Half4, new float[8]);

        //Assert
        upload.Should().Throw<ArgumentException>();
    }

    [Fact]
    public void CreateImage_reports_a_wrong_byte_count_as_a_native_failure()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        Action upload = () => scene.CreateImage<byte>(2, 2, ImageFormat.Byte4, new byte[15]);

        //Assert
        upload.Should().Throw<CyclesException>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
    }

    [Fact]
    public void Wrap_returns_the_same_typed_node_for_a_handle()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Node meshHandle = scene.Host.CreateNode("mesh");

        //Act
        CyclesNode wrapped = scene.Wrap(meshHandle);
        CyclesNode again = scene.Wrap(meshHandle);

        //Assert
        wrapped.Should().BeOfType<MeshNode>();
        again.Should().BeSameAs(wrapped);
        scene.Camera.Should().BeSameAs(scene.Wrap<CameraNode>(scene.Host.SceneNode("camera")));
        wrapped.TypeInfo.Name.Should().Be("mesh");
    }

    [Fact]
    public void node_reference_properties_return_the_typed_nodes_that_were_assigned()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        PointLightNode light = scene.CreateLight<PointLightNode>();
        ShaderNode emission = scene.CreateShader();

        //Act
        ObjectNode instance = scene.CreateObject(light, Transform3x4.CreateTranslation(1, 2, 3));
        light.UsedShaders = [emission];

        //Assert
        instance.Geometry.Should().BeSameAs(light);
        instance.Tfm.Should().Be(Transform3x4.CreateTranslation(1, 2, 3));
        light.UsedShaders.Should().Equal(emission);
    }

    [Fact]
    public void Create_rejects_types_the_host_does_not_create_in_the_scene()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        Action singleton = () => scene.Create<CameraNode>();
        Action shaderNode = () => scene.Create<DiffuseBsdfNode>();

        //Assert
        singleton.Should().Throw<ArgumentException>();
        shaderNode.Should().Throw<ArgumentException>();
    }

    [Fact]
    public void Connect_rejects_sockets_from_another_graph()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        ShaderNode first = scene.CreateShader(), second = scene.CreateShader();
        DiffuseBsdfNode diffuse = second.Add<DiffuseBsdfNode>();

        //Act
        Action connect = () => first.Connect(diffuse.BsdfOutput, first.Output.SurfaceInput);

        //Assert
        connect.Should().Throw<ArgumentException>();
    }

    [Fact]
    public void GetCapabilities_reports_the_cpu_reference_backend()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        ICapabilityReport report = scene.GetCapabilities();

        //Assert
        report.Backend.Should().Be(Backend.CpuReference);
        report.BackendName.Should().Be("cpu");
        report.IsGpu.Should().BeFalse();
        report.PipelineTier.Should().BeNull();
        report.AbiVersion.Should().Be(CyclesVersion.ExpectedAbi);
    }

    [Fact]
    public void CyclesVersion_reports_the_loaded_abi()
    {
        //Arrange
        TestScenes.RequireNativeHost();

        //Act
        uint abi = CyclesVersion.Abi;

        //Assert
        abi.Should().Be(CyclesVersion.ExpectedAbi);
        CyclesVersion.IsAbiCompatible.Should().BeTrue();
        CyclesVersion.ManagedVersion.Should().NotBeNull();
    }

    [Fact]
    public void instance_disposed_correctly()
    {
        //Arrange
        CyclesScene scene = TestScenes.CreateCpuScene();
        CameraNode camera = scene.Camera;
        CyclesImage image = scene.CreateImage<byte>(1, 1, ImageFormat.Byte4, [1, 2, 3, 4]);

        //Act
        scene.Dispose();
        Action secondDispose = scene.Dispose;
        Action useScene = () => _ = scene.Integrator;
        Action useNode = () => camera.Fov = 1;
        Action releaseImage = image.Dispose;

        //Assert
        secondDispose.Should().NotThrow();
        useScene.Should().Throw<ObjectDisposedException>();
        useNode.Should().Throw<ObjectDisposedException>();
        releaseImage.Should().NotThrow();
    }

    [Fact]
    public async Task DisposeAsync_cancels_an_active_render()
    {
        //Arrange
        CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        float[] pixels = Enumerable.Repeat(-99f, (int)Small.FloatCount).ToArray();
        Task render = scene.RenderAsync(Small with { Samples = 1_000_000 }, pixels, null, Token);
        Stopwatch wait = Stopwatch.StartNew();
        while (!scene.IsRendering && !render.IsCompleted && wait.Elapsed < TimeSpan.FromSeconds(30)) await Task.Delay(1, Token);

        //Act
        await scene.DisposeAsync();
        Func<Task> awaiting = () => render;

        //Assert
        (await awaiting.Should().ThrowAsync<CyclesException>()).Which.NativeStatus.Should().Be(NativeStatus.Cancelled);
        pixels.All(value => value == -99f).Should().BeTrue();
        Action use = () => _ = scene.Camera;
        use.Should().Throw<ObjectDisposedException>();
    }

    [Fact]
    public void image_dispose_keeps_the_bound_pixels()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (ShaderNode material, DiffuseBsdfNode diffuse, MeshNode _) = TestScenes.BuildTyped(scene);
        ImageTextureNode texture = material.Add<ImageTextureNode>();
        material.Connect(texture.ColorOutput, diffuse.ColorInput);
        CyclesImage image = scene.CreateImage<float>(1, 1, ImageFormat.Float4, [.9f, .1f, .1f, 1]);
        texture.Bind(image);
        FilmBuffer before = scene.Render(Small);

        //Act
        image.Dispose();
        FilmBuffer after = scene.Render(Small);
        Action rebind = () => texture.Bind(image);

        //Assert
        image.IsReleased.Should().BeTrue();
        after.Span.SequenceEqual(before.Span).Should().BeTrue();
        rebind.Should().Throw<ObjectDisposedException>();
    }

    private static T[] Ramp<T>(int count, Func<int, T> value) => Enumerable.Range(0, count).Select(value).ToArray();

    // Records reports on the reporting thread, so the last report is known when the task completes.
    private sealed class SynchronousProgress(ConcurrentQueue<RenderProgress> reports) : IProgress<RenderProgress>
    {
        public void Report(RenderProgress value) => reports.Enqueue(value);
    }
}
