// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Rendering;
using CodeBrix.Graphics3D.Cycles.Validation;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Cycles' Open Image Denoise denoiser through the managed API: off by default, on when the
// integrator asks for it (the CPU reference backend denoises on OIDN's CPU device).
public class DenoisingTests
{
    private static readonly RenderSettings Small = new(32, 24, 8);

    [Fact]
    public void binary_report_states_open_image_denoise_and_its_device_per_backend()
    {
        //Arrange
        TestScenes.RequireNativeHost();

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();

        //Assert
        report.CanDenoise.Should().BeTrue();
        report.OptionalLibraries["openImageDenoise"].Should().BeTrue();
        report.DenoiseDevices["cpu"].Should().Be("cpu");
        if (report.CompiledBackends.Contains("webgpu")) report.DenoiseDevices["webgpu"].Should().Be("cpu");
        if (report.CompiledBackends.Contains("metal")) report.DenoiseDevices["metal"].Should().Be("metal");
    }

    [Fact]
    public void integrator_denoising_is_off_by_default()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        bool useDenoise = scene.Integrator.UseDenoise;

        //Assert
        useDenoise.Should().BeFalse();
    }

    [Fact]
    public void denoised_cpu_render_is_finite_deterministic_and_differs_from_the_noisy_render()
    {
        //Arrange
        using CyclesScene noisyScene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(noisyScene);
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        RequestDenoising(scene.Integrator);
        float[] noisy = noisyScene.Render(Small).ToArray();

        //Act
        float[] first = scene.Render(Small).ToArray();
        float[] second = scene.Render(Small).ToArray();

        //Assert
        first.All(float.IsFinite).Should().BeTrue();
        first.SequenceEqual(second).Should().BeTrue();
        first.SequenceEqual(noisy).Should().BeFalse();
    }

    [Fact]
    public void denoising_switched_on_between_renders_of_one_scene_takes_effect()
    {
        //Arrange
        using CyclesScene reference = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(reference);
        RequestDenoising(reference.Integrator);
        float[] denoised = reference.Render(Small).ToArray();
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        float[] noisy = scene.Render(Small).ToArray();
        RequestDenoising(scene.Integrator);

        //Act
        float[] switched = scene.Render(Small).ToArray();

        //Assert
        switched.SequenceEqual(noisy).Should().BeFalse();
        switched.SequenceEqual(denoised).Should().BeTrue();
    }

    [Fact]
    public void noisy_combined_pass_is_the_undenoised_image_beside_the_denoised_combined()
    {
        //Arrange
        using CyclesScene reference = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(reference);
        float[] undenoised = reference.Render(Small).ToArray();
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        scene.AddPass(PassType.Combined, "Noisy Image", PassMode.Noisy);
        RequestDenoising(scene.Integrator);

        //Act
        float[] denoised = scene.Render(Small).ToArray();
        FilmBuffer noisy = scene.ReadPass("Noisy Image");

        //Assert
        scene.GetRenderedPasses().Select(pass => pass.Name).Should().Contain("Noisy Image");
        noisy.Components.Should().Be(4);
        noisy.Span.ToArray().SequenceEqual(undenoised).Should().BeTrue();
        denoised.SequenceEqual(undenoised).Should().BeFalse();
    }

    [Fact]
    public void noisy_pass_requests_leave_an_undenoised_render_unchanged()
    {
        //Arrange
        using CyclesScene reference = TestScenes.CreateCpuScene();
        BuildShadowCatcher(reference, noisy: false);
        float[] combined = reference.Render(WebGpuValidationScenes.Settings).ToArray();
        float[] catcher = reference.ReadPass("Shadow Catcher").ToArray(), matte = reference.ReadPass("Shadow Catcher Matte").ToArray();
        using CyclesScene scene = TestScenes.CreateCpuScene();
        BuildShadowCatcher(scene);
        scene.AddPass(PassType.Combined, "Noisy Image", PassMode.Noisy);

        //Act
        float[] film = scene.Render(WebGpuValidationScenes.Settings).ToArray();

        //Assert
        film.SequenceEqual(combined).Should().BeTrue();
        scene.ReadPass("Noisy Image").ToArray().SequenceEqual(combined).Should().BeTrue();
        scene.ReadPass("Shadow Catcher").ToArray().SequenceEqual(catcher).Should().BeTrue();
        scene.ReadPass("Shadow Catcher Noisy").ToArray().SequenceEqual(catcher).Should().BeTrue();
        scene.ReadPass("Shadow Catcher Matte").ToArray().SequenceEqual(matte).Should().BeTrue();
        scene.ReadPass("Shadow Catcher Matte Noisy").ToArray().SequenceEqual(matte).Should().BeTrue();
    }

    [Fact]
    public void shadow_catcher_passes_are_denoised_by_default_and_noisy_on_request_also_after_an_undenoised_render()
    {
        //Arrange
        using CyclesScene reference = TestScenes.CreateCpuScene();
        BuildShadowCatcher(reference, noisy: false);
        reference.Render(WebGpuValidationScenes.Settings);
        float[] undenoisedCatcher = reference.ReadPass("Shadow Catcher").ToArray();
        float[] undenoisedMatte = reference.ReadPass("Shadow Catcher Matte").ToArray();
        using CyclesScene fresh = TestScenes.CreateCpuScene();
        BuildShadowCatcher(fresh);
        RequestDenoising(fresh.Integrator);
        fresh.Render(WebGpuValidationScenes.Settings);
        using CyclesScene scene = TestScenes.CreateCpuScene();
        BuildShadowCatcher(scene);
        scene.Render(WebGpuValidationScenes.Settings);
        RequestDenoising(scene.Integrator);

        //Act
        scene.Render(WebGpuValidationScenes.Settings);

        //Assert
        float[] catcher = scene.ReadPass("Shadow Catcher").ToArray(), matte = scene.ReadPass("Shadow Catcher Matte").ToArray();
        catcher.All(float.IsFinite).Should().BeTrue();
        catcher.SequenceEqual(undenoisedCatcher).Should().BeFalse();
        matte.SequenceEqual(undenoisedMatte).Should().BeFalse();
        catcher.SequenceEqual(fresh.ReadPass("Shadow Catcher").ToArray()).Should().BeTrue();
        matte.SequenceEqual(fresh.ReadPass("Shadow Catcher Matte").ToArray()).Should().BeTrue();
        scene.ReadPass("Shadow Catcher Noisy").ToArray().SequenceEqual(undenoisedCatcher).Should().BeTrue();
        scene.ReadPass("Shadow Catcher Matte Noisy").ToArray().SequenceEqual(undenoisedMatte).Should().BeTrue();
    }

    [Fact]
    public void light_group_passes_with_denoising_are_refused()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        BuildLightGroup(scene);
        RequestDenoising(scene.Integrator);

        //Act
        Action render = () => scene.Render(Small);

        //Assert
        render.Should().Throw<CyclesException>().Which.Message.Should().Contain("Light group passes are not supported together with denoising");
    }

    private static void BuildShadowCatcher(CyclesScene scene, bool noisy = true)
    {
        WebGpuValidationScenes.Build(scene, WebGpuValidationScenes.DiffuseSun);
        scene.Background.Transparent = true;
        ShaderNode material = scene.CreateShader();
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        diffuse.Color = new System.Numerics.Vector3(.3f, .6f, .4f);
        material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        ObjectNode catcher = scene.CreateObject(scene.CreateMesh([-2.5f, .01f, -2.5f, 2.5f, .01f, -2.5f, 2.5f, .01f, 2.5f, -2.5f, .01f, 2.5f],
            [0, 2, 1, 0, 3, 2], material));
        catcher.IsShadowCatcher = true;
        scene.AddPass(PassType.ShadowCatcher, "Shadow Catcher");
        scene.AddPass(PassType.ShadowCatcherMatte, "Shadow Catcher Matte");
        if (!noisy) return;
        scene.AddPass(PassType.ShadowCatcher, "Shadow Catcher Noisy", PassMode.Noisy);
        scene.AddPass(PassType.ShadowCatcherMatte, "Shadow Catcher Matte Noisy", PassMode.Noisy);
    }

    private static void BuildLightGroup(CyclesScene scene)
    {
        TestScenes.BuildTyped(scene);
        scene.Background.Lightgroup = "group";
        scene.AddPass(PassType.Combined, "Group").Lightgroup = "group";
    }

    private static void RequestDenoising(IntegratorNode integrator)
    {
        integrator.UseDenoise = true;
        integrator.DenoiserType = IntegratorDenoiserType.Openimagedenoise;
        integrator.DenoiseUseGpu = false;
        integrator.DenoiserPrefilter = IntegratorDenoiserPrefilter.Accurate;
        integrator.DenoiserQuality = IntegratorDenoiserQuality.High;
        integrator.DenoiseStartSample = 0;
    }
}
