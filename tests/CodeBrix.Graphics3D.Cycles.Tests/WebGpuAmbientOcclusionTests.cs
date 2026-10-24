// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Rendering;
using CodeBrix.Graphics3D.Cycles.Validation;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// The AO pass on CyclesBackend.Gpu (WebGPU) read back through ReadPass and compared with the
// CPU reference backend's AO pass of the same scene: the practical criterion must hold. Skips with
// a reason when the native host or a WebGPU adapter is absent.
public class WebGpuAmbientOcclusionTests(ITestOutputHelper output)
{
    private static CancellationToken Token => TestContext.Current.CancellationToken;

    private static readonly Lazy<string> Unavailable = new(ProbeGpu);

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

    [Fact]
    public async Task gpu_ao_pass_matches_the_cpu_reference_with_the_practical_criterion()
    {
        //Arrange
        using CyclesScene gpu = CreateGpuScene();
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        foreach (CyclesScene scene in new[] { gpu, cpu })
        {
            WebGpuValidationScenes.Build(scene, WebGpuValidationScenes.DiffuseSun);
            scene.AddPass(PassType.Ao, "AO");
            scene.Integrator.AoDistance = 1.5f;
        }
        cpu.Render(WebGpuValidationScenes.Settings);
        FilmBuffer cpuAo = cpu.ReadPass("AO");

        //Act
        await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, Token);
        FilmBuffer gpuAo = gpu.ReadPass("AO");
        (double maximum, int practical, int strict) = WebGpuValidationScenes.Compare(gpuAo.Span, cpuAo.Span);
        output.WriteLine($"AO pass: max abs error {maximum:G6}, practical failures {practical}, strict differences {strict} of {gpuAo.Span.Length}");

        //Assert
        gpu.GetRenderedPasses().Should().Contain(pass => pass.Name == "AO" && pass.Written);
        gpuAo.Span.Length.Should().Be(cpuAo.Span.Length);
        practical.Should().Be(0);
        // Occlusion varies over the image: the pass is not a constant.
        cpuAo.Span.ToArray().Distinct().Count().Should().BeGreaterThan(10);
    }
}
