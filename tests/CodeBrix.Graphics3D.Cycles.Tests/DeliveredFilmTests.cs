// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Numerics;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// The delivered film (provisional; native/host/ABI.md "Conventions"): the host verifies that the film Cycles' output driver
// delivers is the one requested (its size, its render buffers, its passes, every value written) and otherwise fails the
// render or bake by name. No accepted scene triggers it: these cases are the ones whose delivery differs from a plain
// render (named passes of every component count, a camera border, tiles, bakes). A film rendered at another resolution
// would be refused; the one known cause, a denoiser upscale factor, is refused by name before a render or bake starts
// (provisional for bakes), and the check stays behind that refusal.
public class DeliveredFilmTests
{
    private static readonly RenderSettings Settings = new(40, 30, 4);

    internal static (CyclesScene Scene, Generated.Scene.ObjectNode Target) BuildBake(CyclesScene scene)
    {
        IntegratorNode integrator = scene.Integrator;
        integrator.UseGuiding = false;
        integrator.UseLightTree = false;
        integrator.UseAdaptiveSampling = false;
        integrator.SamplingPattern = IntegratorSamplingPattern.TabulatedSobol;
        ShaderNode material = scene.CreateShader();
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        diffuse.Color = new Vector3(.6f, .5f, .4f);
        material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        Generated.Geometry.MeshNode mesh = scene.CreateMesh([-1f, -1f, 0f, 1f, -1f, 0f, 1f, 1f, 0f, -1f, 1f, 0f], [0, 1, 2, 0, 2, 3], material);
        mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, [0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1]);
        Generated.Scene.ObjectNode target = scene.CreateObject(mesh);
        ShaderNode world = scene.CreateShader();
        BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
        background.Color = new Vector3(.8f, .7f, .6f);
        world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
        world.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        scene.Background.Shader = world;
        return (scene, target);
    }

    [Theory]
    [InlineData(0u, false)]
    [InlineData(0u, true)]
    [InlineData(8u, false)]
    [InlineData(8u, true)]
    public void renders_with_passes_of_every_component_count_deliver_the_requested_film(uint tileSize, bool border)
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        scene.TileSize = tileSize;
        if (border)
        {
            scene.Camera.BorderLeft = .2f;
            scene.Camera.BorderRight = .85f;
            scene.Camera.BorderBottom = .1f;
            scene.Camera.BorderTop = .7f;
        }
        (PassType Type, string Name, int Components)[] passes =
        [
            (PassType.Depth, "Depth", 1), (PassType.Normal, "Normal", 3), (PassType.DiffuseColor, "DiffuseColor", 3),
            (PassType.DiffuseDirect, "DiffuseDirect", 3), (PassType.Uv, "UV", 3), (PassType.SampleCount, "Samples", 1),
            (PassType.Combined, "Noisy", 4),
        ];
        foreach ((PassType type, string name, _) in passes)
            if (type == PassType.Combined) scene.AddPass(type, name, PassMode.Noisy); else scene.AddPass(type, name);

        //Act
        FilmBuffer film = scene.Render(Settings);

        //Assert
        film.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        foreach ((_, string name, int components) in passes)
        {
            FilmBuffer pass = scene.ReadPass(name);
            pass.Components.Should().Be(components, name);
            pass.Span.ToArray().All(float.IsFinite).Should().BeTrue(name);
        }
        scene.ReadPass("Noisy").Span.SequenceEqual(film.Span).Should().BeTrue();
    }

    // A denoiser upscale factor other than 1 (provisional for bakes): refused by name at bake as at render, before anything
    // bakes, untiled and in tiles; the bake map, which renders nothing, and a bake without denoising are not affected, and
    // after the refusal the scene bakes at a factor of 1 as a new scene does.
    [Theory]
    [InlineData(0u)]
    [InlineData(16u)]
    public void a_bake_with_a_denoiser_upscale_factor_is_refused_by_name_before_it_bakes_as_a_render_is(uint tileSize)
    {
        //Arrange
        BakeSettings settings = new(32, 32, 8, BakeType.Combined);
        (CyclesScene Scene, Generated.Scene.ObjectNode Target) Build(float factor, bool denoise)
        {
            CyclesScene scene = TestScenes.CreateCpuScene();
            (_, Generated.Scene.ObjectNode target) = BuildBake(scene);
            scene.TileSize = tileSize;
            scene.Integrator.UseDenoise = denoise;
            scene.Integrator.DenoiserUpscaleFactor = factor;
            return (scene, target);
        }
        (CyclesScene plainScene, Generated.Scene.ObjectNode plainTarget) = Build(1, true);
        (CyclesScene noisyScene, Generated.Scene.ObjectNode noisyTarget) = Build(1, false);
        (CyclesScene noisyUpscaledScene, Generated.Scene.ObjectNode noisyUpscaledTarget) = Build(2, false);
        (CyclesScene upscaledScene, Generated.Scene.ObjectNode upscaledTarget) = Build(2, true);
        using CyclesScene plainOwner = plainScene, noisyOwner = noisyScene, noisyUpscaledOwner = noisyUpscaledScene, upscaledOwner = upscaledScene;
        int events = 0;
        upscaledScene.RenderEventReceived += (_, _) => events++;

        //Act
        FilmBuffer plain = plainScene.Bake(plainTarget, settings);
        FilmBuffer noisy = noisyScene.Bake(noisyTarget, settings);
        FilmBuffer noisyUpscaled = noisyUpscaledScene.Bake(noisyUpscaledTarget, settings);
        Action bake = () => upscaledScene.Bake(upscaledTarget, settings);
        Action render = () => upscaledScene.Render(Settings);
        BakeMap map = upscaledScene.ReadBakeMap(upscaledTarget, settings);

        //Assert
        foreach (Action refusedCall in new[] { bake, render })
        {
            NativeFailure refused = refusedCall.Should().Throw<NativeFailure>().Which;
            refused.NativeStatus.Should().Be(NativeStatus.Unsupported);
            refused.Message.Should().Contain("Unsupported feature: a denoiser upscale factor other than 1");
        }
        events.Should().Be(0);
        map.Triangles.SequenceEqual(plainScene.ReadBakeMap(plainTarget, settings).Triangles).Should().BeTrue();
        noisyUpscaled.Span.SequenceEqual(noisy.Span).Should().BeTrue();
        upscaledScene.Integrator.DenoiserUpscaleFactor = 1;
        upscaledScene.Bake(upscaledTarget, settings).Span.SequenceEqual(plain.Span).Should().BeTrue();
    }
}
