// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Lights;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.GltfRender;
using CodeBrix.Graphics3D.Cycles.Interop;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Cycles' BVH spatial splits (provisional CyclesScene.UseBvhSpatialSplit, cbc_host_bvh_spatial_split) on the CPU backends.
// A speed setting: the BVH2 builder splits primitives between nodes in the BVH of instanced geometry, Embree builds at its
// high quality, and rays find the same closest hits, so films do not change by design. The scene gives the splits
// something to do: a fan of long thin triangles, half of them transparent, used by two objects (instanced, so it has its
// own BVH) in front of a floor (single user, baked into the scene BVH), a mesh with true displacement (which a rebuild
// must not displace again), curves and points, lit by a point lamp through the transparent slivers (transparent shadows).
public class BvhSpatialSplitTests
{
    private readonly ITestOutputHelper output;

    public BvhSpatialSplitTests(ITestOutputHelper output) => this.output = output;

    private static readonly RenderSettings Settings = new(64, 48, 8);

    internal static void Build(CyclesScene scene)
    {
        IntegratorNode integrator = scene.Integrator;
        integrator.UseGuiding = false;
        integrator.UseLightTree = false;
        integrator.UseAdaptiveSampling = false;
        integrator.UseDenoise = false;
        integrator.CausticsReflective = false;
        integrator.CausticsRefractive = false;
        integrator.MaxBounce = 4;
        integrator.SamplingPattern = IntegratorSamplingPattern.TabulatedSobol;
        scene.Camera.Fov = .9f;
        scene.Camera.Matrix = new Transform3x4(1, 0, 0, 0, 0, .96f, -.28f, 1.1f, 0, .28f, .96f, -5f);

        ShaderNode diffuse = scene.CreateShader();
        DiffuseBsdfNode floorBsdf = diffuse.Add<DiffuseBsdfNode>();
        floorBsdf.Color = new Vector3(.7f, .65f, .6f);
        diffuse.Connect(floorBsdf.BsdfOutput, diffuse.Output.SurfaceInput);
        diffuse.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;

        ShaderNode sliver = scene.CreateShader();
        DiffuseBsdfNode sliverBsdf = sliver.Add<DiffuseBsdfNode>();
        sliverBsdf.Color = new Vector3(.2f, .5f, .8f);
        TransparentBsdfNode transparent = sliver.Add<TransparentBsdfNode>();
        transparent.Color = new Vector3(.9f, .6f, .3f);
        MixClosureNode mix = sliver.Add<MixClosureNode>();
        mix.Fac = .5f;
        sliver.Connect(sliverBsdf.BsdfOutput, mix.Closure1Input);
        sliver.Connect(transparent.BsdfOutput, mix.Closure2Input);
        sliver.Connect(mix.ClosureOutput, sliver.Output.SurfaceInput);
        sliver.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;

        // The fan: 48 long thin triangles from a common hub, crossing each other's bounding boxes.
        List<float> positions = [0f, 0f, 0f];
        List<uint> indices = [];
        for (int k = 0; k < 48; k++)
        {
            double angle = 2 * Math.PI * k / 48;
            float length = 1.2f + .3f * (k % 5);
            positions.AddRange([(float)(length * Math.Cos(angle)), (float)(length * Math.Sin(angle)), .05f * (k % 3),
                (float)(length * Math.Cos(angle + .02)), (float)(length * Math.Sin(angle + .02)), -.05f * (k % 4)]);
            indices.AddRange([0u, (uint)(1 + 2 * k), (uint)(2 + 2 * k)]);
        }
        MeshNode fan = scene.CreateMesh([.. positions], [.. indices], sliver);
        scene.CreateObject(fan, new Transform3x4(.9f, -.2f, 0, -.8f, .2f, .9f, 0, .2f, 0, 0, .9f, 0));
        scene.CreateObject(fan, new Transform3x4(.7f, .3f, 0, .9f, -.3f, .7f, 0, -.1f, 0, 0, .7f, .3f));

        MeshNode floor = scene.CreateMesh([-4f, -1.5f, -2f, 4f, -1.5f, -2f, 4f, -1.5f, 3f, -4f, -1.5f, 3f], [0, 1, 2, 0, 2, 3], diffuse);
        scene.CreateObject(floor, Transform3x4.CreateTranslation(0, 0, .5f));

        ShaderNode displaced = scene.CreateShader();
        DiffuseBsdfNode displacedBsdf = displaced.Add<DiffuseBsdfNode>();
        displacedBsdf.Color = new Vector3(.8f, .3f, .3f);
        NoiseTextureNode noise = displaced.Add<NoiseTextureNode>();
        noise.Scale = 3f;
        DisplacementNode displacement = displaced.Add<DisplacementNode>();
        displacement.Scale = .3f;
        displaced.Connect(displacedBsdf.BsdfOutput, displaced.Output.SurfaceInput);
        displaced.Connect(noise.FacOutput, displacement.HeightInput);
        displaced.Connect(displacement.DisplacementOutput, displaced.Output.DisplacementInput);
        displaced.DisplacementMethod = ShaderDisplacementMethod.True;
        displaced.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        const int cells = 12;
        List<float> grid = [];
        List<uint> gridIndices = [];
        for (int y = 0; y <= cells; y++)
            for (int x = 0; x <= cells; x++)
                grid.AddRange([-1f + 2f * x / cells, 0f, -1f + 2f * y / cells]);
        for (int y = 0; y < cells; y++)
            for (int x = 0; x < cells; x++)
            {
                uint a = (uint)(y * (cells + 1) + x), b = a + 1, c = a + cells + 1, d = c + 1;
                gridIndices.AddRange([a, c, b, b, c, d]);
            }
        scene.CreateObject(scene.CreateMesh([.. grid], [.. gridIndices], displaced), Transform3x4.CreateTranslation(1.6f, -1.2f, 1.2f));

        scene.CreateObject(scene.CreateHair([-2.2f, -1.4f, 0f, -2f, -.6f, .1f, -2.3f, .3f, 0f], [.05f, .04f, .02f], [0u], CurveShape.Thick, diffuse));
        scene.CreateObject(scene.CreatePointCloud([-1.4f, -1.1f, .6f, -1.1f, -1.2f, .8f, -1.7f, -1.25f, .9f], [.15f, .12f, .1f], diffuse));

        ShaderNode lamp = scene.CreateShader();
        EmissionNode emission = lamp.Add<EmissionNode>();
        emission.Color = Vector3.One;
        emission.Strength = 1f;
        lamp.Connect(emission.EmissionOutput, lamp.Output.SurfaceInput);
        PointLightNode light = scene.CreateLight<PointLightNode>();
        light.Strength = new Vector3(60f, 58f, 55f);
        light.Radius = .1f;
        light.UsedShaders = [lamp];
        scene.CreateObject(light, Transform3x4.CreateTranslation(.4f, 2.4f, -1.5f));

        ShaderNode world = scene.CreateShader();
        BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
        background.Color = new Vector3(.1f, .12f, .15f);
        background.Strength = 1f;
        world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
        world.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        scene.Background.Shader = world;
    }

    private static FilmBuffer Render(Backend backend, bool split)
    {
        TestScenes.RequireNativeHost();
        using CyclesScene scene = new(backend);
        scene.UseBvhSpatialSplit = split;
        Build(scene);
        return scene.Render(Settings);
    }

    // The status messages of one render: Cycles names each BVH it builds ("Updating Geometry BVH ...", "Updating Scene BVH").
    private static (FilmBuffer Film, string[] Statuses) RenderWithStatuses(CyclesScene scene)
    {
        ConcurrentQueue<string> statuses = new();
        void Listen(object sender, RenderEvent e)
        {
            if (e.Kind == RenderEventKind.Status) statuses.Enqueue(e.Message);
        }
        scene.RenderEventReceived += Listen;
        try
        {
            FilmBuffer film = scene.Render(Settings);
            return (film, [.. statuses]);
        }
        finally
        {
            scene.RenderEventReceived -= Listen;
        }
    }

    [Fact]
    public void the_setting_reads_back_and_the_status_tier_refuses_values_other_than_0_and_1()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        using CyclesScene scene = TestScenes.CreateCpuScene();
        CyclesAbi.Create(Backend.CpuReference, 1, out ulong host).Should().Be(NativeStatus.Ok);

        //Act
        bool initial = scene.UseBvhSpatialSplit;
        scene.UseBvhSpatialSplit = true;
        scene.Host.SetBvhSpatialSplit(true);
        NativeStatus two = (NativeStatus)Native.BvhSpatialSplit(host, 2);
        string message = CyclesAbi.LastError();
        NativeStatus one = (NativeStatus)Native.BvhSpatialSplit(host, 1);
        NativeStatus zero = (NativeStatus)Native.BvhSpatialSplit(host, 0);
        CyclesAbi.Destroy(host).Should().Be(NativeStatus.Ok);

        //Assert
        initial.Should().BeFalse();
        scene.UseBvhSpatialSplit.Should().BeTrue();
        two.Should().Be(NativeStatus.Invalid);
        message.Should().Contain("BVH spatial split must be 0 (off) or 1 (on)");
        one.Should().Be(NativeStatus.Ok);
        zero.Should().Be(NativeStatus.Ok);
    }

    [Fact]
    public void the_cpu_reference_renders_bit_for_bit_the_same_film_with_spatial_splits()
    {
        //Arrange / Act
        FilmBuffer plain = Render(Backend.CpuReference, false);
        FilmBuffer split = Render(Backend.CpuReference, true);
        FilmBuffer again = Render(Backend.CpuReference, true);
        FilmComparison comparison = FilmComparison.Compare(split.Span, plain.Span);
        output.WriteLine($"cpu reference, spatial splits vs none: {comparison}");

        //Assert
        split.Span.SequenceEqual(again.Span).Should().BeTrue();
        split.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        split.Span.SequenceEqual(plain.Span).Should().BeTrue();
        plain.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(100);
    }

    [Fact]
    public void the_embree_cpu_backend_renders_with_spatial_splits_within_the_practical_criterion()
    {
        //Arrange / Act
        FilmBuffer plain = Render(Backend.Cpu, false);
        FilmBuffer split = Render(Backend.Cpu, true);
        FilmBuffer again = Render(Backend.Cpu, true);
        FilmComparison comparison = FilmComparison.Compare(split.Span, plain.Span);
        output.WriteLine($"cpu-embree, spatial splits (high build quality) vs none: {comparison}, bitwise {split.Span.SequenceEqual(plain.Span)}");

        //Assert
        split.Span.SequenceEqual(again.Span).Should().BeTrue();
        split.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        comparison.PracticalFailures.Should().Be(0);
    }

    [Fact]
    public void a_change_after_a_render_rebuilds_every_bvh_and_renders_like_a_new_scene()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Build(scene);
        (FilmBuffer first, string[] firstStatuses) = RenderWithStatuses(scene);
        (FilmBuffer unchanged, string[] unchangedStatuses) = RenderWithStatuses(scene);
        scene.UseBvhSpatialSplit = false;
        (_, string[] sameValueStatuses) = RenderWithStatuses(scene);

        //Act
        scene.UseBvhSpatialSplit = true;
        (FilmBuffer toggled, string[] toggledStatuses) = RenderWithStatuses(scene);
        scene.UseBvhSpatialSplit = false;
        (FilmBuffer back, string[] backStatuses) = RenderWithStatuses(scene);
        output.WriteLine("after the change: " + string.Join(" | ", toggledStatuses.Where(s => s.Contains("BVH", StringComparison.Ordinal)).Distinct()));

        //Assert
        static bool Builds(string[] statuses, string what) => statuses.Any(s => s.StartsWith(what, StringComparison.Ordinal));
        Builds(firstStatuses, "Updating Scene BVH").Should().BeTrue();
        Builds(firstStatuses, "Updating Geometry BVH").Should().BeTrue();
        Builds(unchangedStatuses, "Updating Scene BVH").Should().BeFalse();
        Builds(sameValueStatuses, "Updating Scene BVH").Should().BeFalse();
        Builds(toggledStatuses, "Updating Scene BVH").Should().BeTrue();
        Builds(toggledStatuses, "Updating Geometry BVH").Should().BeTrue();
        Builds(backStatuses, "Updating Scene BVH").Should().BeTrue();
        unchanged.Span.SequenceEqual(first.Span).Should().BeTrue();
        toggled.Span.SequenceEqual(Render(Backend.CpuReference, true).Span).Should().BeTrue();
        back.Span.SequenceEqual(first.Span).Should().BeTrue();
    }

    [Fact]
    public void a_change_after_a_render_on_the_embree_backend_renders_like_a_new_scene()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        using CyclesScene scene = new(Backend.Cpu);
        Build(scene);
        FilmBuffer first = scene.Render(Settings);

        //Act
        scene.UseBvhSpatialSplit = true;
        FilmBuffer toggled = scene.Render(Settings);
        scene.UseBvhSpatialSplit = false;
        FilmBuffer back = scene.Render(Settings);

        //Assert
        toggled.Span.SequenceEqual(Render(Backend.Cpu, true).Span).Should().BeTrue();
        back.Span.SequenceEqual(first.Span).Should().BeTrue();
    }
}
