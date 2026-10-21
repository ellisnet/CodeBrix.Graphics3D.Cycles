// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Concurrent;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using CodeBrix.Graphics3D.Cycles.GltfRender;
using CodeBrix.Graphics3D.Cycles.Rendering;
using CodeBrix.Graphics3D.Cycles.Validation;
using CodeBrix.Graphics3D.Gltf2.Schema2;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Renders the shared validation scenes through the typed API on CyclesBackend.Gpu (WebGPU) and on
// CyclesBackend.CpuReference and compares the films: the practical criterion must hold; strict
// differences are reported (GPU film accumulation uses atomic float addition, so they vary from run
// to run). Skips with a reason when the native host or a WebGPU adapter is absent. All tests are in
// one class so GPU renders run one at a time.
public class WebGpuRenderTests(ITestOutputHelper output)
{
    private static CancellationToken Token => TestContext.Current.CancellationToken;

    private static readonly Lazy<string> Unavailable = new(ProbeGpu);

    // Creates a GPU scene, or skips when this machine has no usable WebGPU adapter.
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
            return scene.Backend == Backend.WebGpu ? null : "CyclesBackend.Gpu resolved to " + scene.Backend + ", not WebGPU.";
        }
        catch (CyclesException error)
        {
            return "No WebGPU adapter is available to the native host: " + error.Message;
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

    [Theory]
    [InlineData(WebGpuValidationScenes.Glass)]
    [InlineData(WebGpuValidationScenes.DiffuseSun)]
    [InlineData(WebGpuValidationScenes.TexturedPlane)]
    public async Task gpu_film_matches_the_cpu_reference_with_the_practical_criterion(string name)
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, name);
        FilmBuffer reference = RenderCpu(name);
        ExportReference(name, reference);

        //Act
        FilmBuffer film = await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        (double maximum, int practical, int strict) = WebGpuValidationScenes.Compare(film.Span, reference.Span);
        output.WriteLine($"{name}: max abs error {maximum:G6}, practical failures {practical}, strict differences {strict} of {film.Span.Length}");

        //Assert
        gpu.Backend.Should().Be(Backend.WebGpu);
        gpu.IsFaulted.Should().BeFalse();
        practical.Should().Be(0);
        // The scene is not trivially empty: the films hold more than the background colour.
        reference.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(10);
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
        WebGpuValidationScenes.Compare(again.Span, RenderCpu(WebGpuValidationScenes.DiffuseSun).Span).Practical.Should().Be(0);
    }

    [Fact]
    public void gpu_capability_report_states_the_webgpu_tier_decision()
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
        gpu.Render(WebGpuValidationScenes.Settings);

        //Act
        ICapabilityReport report = gpu.GetCapabilities();
        output.WriteLine($"device {report.DeviceDescription}; tier {report.PipelineTier}; warnings [{string.Join("; ", report.Warnings)}]");

        //Assert
        report.HasDevice.Should().BeTrue();
        report.IsGpu.Should().BeTrue();
        report.BackendName.Should().Be("webgpu");
        report.Backend.Should().Be(Backend.WebGpu);
        report.PipelineTier.Should().BeOneOf("full", "conservative");
        report.DeviceDescription.Should().NotBeNullOrEmpty();
        report.GpuFallsBackToCpu.Should().BeFalse();
        report.Scope.WrittenPasses.Should().Contain("combined");
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
        FilmComparison comparison = FilmComparison.Compare(film.Span, reference.Span);
        output.WriteLine("gltf sample: " + comparison);

        //Assert
        comparison.PracticalFailures.Should().Be(0);
    }

    // Records reports on the reporting thread, so the last report is known when the task completes.
    private sealed class SynchronousProgress(ConcurrentQueue<RenderProgress> reports) : IProgress<RenderProgress>
    {
        public void Report(RenderProgress value) => reports.Enqueue(value);
    }
}
