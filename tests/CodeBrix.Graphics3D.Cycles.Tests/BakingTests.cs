// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Threading;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Interop;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Texture baking through the managed API on the CPU reference backend, against known results:
// a position from the triangle's corners, a colour from the shader's sockets, an image baked back
// to itself. native/tools/bake_gate.py holds the wider native set; a result compared only against
// itself proves nothing, so the only bit-identical checks here are after edits, against a new host.
public class BakingTests
{
    private const int Size = 8;
    private static readonly float[] Corners = [-2, -1, .5f, 2, -1, .5f, 2, 1, .5f, -2, 1, .5f];
    private static readonly uint[] Quad = [0, 1, 2, 0, 2, 3];
    // Two triangles, the second not filling the rest of the image, no pixel centre at 8 x 8 on an edge.
    private static readonly float[] LayoutUv = [.0613f, .0471f, .9123f, .0471f, .9123f, .6371f, .0313f, .2531f, .5317f, .7537f, .0313f, .9411f];
    private static readonly float[] FullUv = [0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1];

    private static (CyclesScene Scene, ObjectNode Target, MeshNode Mesh) Build(Action<CyclesScene, ShaderNode> shading, float[] uv = null, Transform3x4? transform = null)
    {
        CyclesScene scene = TestScenes.CreateCpuScene();
        IntegratorNode integrator = scene.Integrator;
        integrator.UseGuiding = false;
        integrator.UseLightTree = false;
        integrator.UseAdaptiveSampling = false;
        integrator.UseDenoise = false;
        integrator.SamplingPattern = IntegratorSamplingPattern.TabulatedSobol;
        ShaderNode material = scene.CreateShader();
        shading(scene, material);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        MeshNode mesh = scene.CreateMesh(Corners, Quad, material);
        mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, uv ?? LayoutUv);
        ObjectNode target = scene.CreateObject(mesh, transform);
        return (scene, target, mesh);
    }

    private static void Emission(CyclesScene scene, ShaderNode material, Vector3 color)
    {
        EmissionNode emission = material.Add<EmissionNode>();
        emission.Color = color;
        emission.Strength = 1;
        material.Connect(emission.EmissionOutput, material.Output.SurfaceInput);
    }

    private static void Diffuse(CyclesScene scene, ShaderNode material, Vector3 color)
    {
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        diffuse.Color = color;
        material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);
    }

    private static void World(CyclesScene scene, Vector3 color)
    {
        ShaderNode world = scene.CreateShader();
        BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
        background.Color = color;
        background.Strength = 1;
        world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
        world.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        scene.Background.Shader = world;
    }

    private static bool Close(float actual, double expected) => Math.Abs(actual - expected) <= 5e-5 + 2e-4 * Math.Abs(expected);

    // The point of the mesh at pixel (x, y) by exact solve in double precision, independent of the library.
    private static Vector3? PointAt(int x, int y, int size, float[] uv, float[] corners, Transform3x4? transform)
    {
        double px = (x + .5) / size, py = (y + .5) / size;
        for (int t = 0; t < uv.Length / 6; t++)
        {
            double x0 = uv[t * 6], y0 = uv[t * 6 + 1], x1 = uv[t * 6 + 2], y1 = uv[t * 6 + 3], x2 = uv[t * 6 + 4], y2 = uv[t * 6 + 5];
            double d = (x0 - x2) * (y1 - y2) - (x1 - x2) * (y0 - y2);
            double u = ((px - x2) * (y1 - y2) - (x1 - x2) * (py - y2)) / d;
            double v = ((x0 - x2) * (py - y2) - (px - x2) * (y0 - y2)) / d;
            if (u < 0 || v < 0 || u + v > 1) continue;
            Vector3 p = default;
            for (int axis = 0; axis < 3; axis++)
            {
                double value = u * corners[Quad[t * 3] * 3 + axis] + v * corners[Quad[t * 3 + 1] * 3 + axis] + (1 - u - v) * corners[Quad[t * 3 + 2] * 3 + axis];
                if (axis == 0) p.X = (float)value; else if (axis == 1) p.Y = (float)value; else p.Z = (float)value;
            }
            return transform.HasValue ? Vector3.Transform(p, transform.Value.ToMatrix()) : p;
        }
        return null;
    }

    [Fact]
    public void settings_report_components_per_type()
    {
        //Arrange
        Dictionary<BakeType, int> expected = new() { [BakeType.Combined] = 4, [BakeType.AovColor] = 4, [BakeType.Roughness] = 1, [BakeType.ObjectId] = 1, [BakeType.AovValue] = 1, [BakeType.Position] = 3, [BakeType.Diffuse] = 3 };

        //Act and Assert
        foreach ((BakeType type, int components) in expected) new BakeSettings(1, 1, 1, type).Components.Should().Be(components);
    }

    [Fact]
    public void position_reads_the_transformed_surface_point_of_every_covered_pixel()
    {
        //Arrange
        Transform3x4 transform = Transform3x4.CreateTranslation(3, -2, 1);
        var (scene, target, _) = Build((s, m) => Diffuse(s, m, new Vector3(.5f)), transform: transform);
        using CyclesScene owned = scene;

        //Act
        FilmBuffer film = scene.Bake(target, new BakeSettings(Size, Size, 8, BakeType.Position));

        //Assert
        film.Components.Should().Be(3);
        film.Width.Should().Be(Size);
        int covered = 0;
        for (int y = 0; y < Size; y++)
        {
            for (int x = 0; x < Size; x++)
            {
                Vector3? point = PointAt(x, y, Size, LayoutUv, Corners, transform);
                int i = (y * Size + x) * 3;
                Vector3 expected = point ?? Vector3.Zero;
                if (point.HasValue) covered++;
                Close(film.Span[i], expected.X).Should().BeTrue();
                Close(film.Span[i + 1], expected.Y).Should().BeTrue();
                Close(film.Span[i + 2], expected.Z).Should().BeTrue();
            }
        }
        covered.Should().BeGreaterThan(0);
        covered.Should().BeLessThan(Size * Size);
    }

    [Fact]
    public void map_reports_coverage_barycentrics_and_differentials_a_hand_solve_agrees_with()
    {
        //Arrange
        var (scene, target, _) = Build((s, m) => Diffuse(s, m, new Vector3(.5f)));
        using CyclesScene owned = scene;

        //Act
        BakeMap map = scene.ReadBakeMap(target, new BakeSettings(Size, Size, 1, BakeType.Combined));

        //Assert
        map.Width.Should().Be(Size);
        map.Triangles.Length.Should().Be(Size * Size);
        map.Coordinates.Length.Should().Be(Size * Size * 6);
        for (int y = 0; y < Size; y++)
        {
            for (int x = 0; x < Size; x++)
                (map.GetTriangle(x, y) >= 0).Should().Be(PointAt(x, y, Size, LayoutUv, Corners, null).HasValue);
        }
        // Triangle 0's corners in pixels: u and v change linearly, so their pixel steps are its inverse matrix.
        int pixel = Enumerable.Range(0, Size * Size).First(i => map.GetTriangle(i % Size, i / Size) == 0);
        double x0 = LayoutUv[0] * Size, y0 = LayoutUv[1] * Size, x1 = LayoutUv[2] * Size, y1 = LayoutUv[3] * Size, x2 = LayoutUv[4] * Size, y2 = LayoutUv[5] * Size;
        double d = (x0 - x2) * (y1 - y2) - (x1 - x2) * (y0 - y2);
        Close(map.Coordinates[pixel * 6 + 2], (y1 - y2) / d).Should().BeTrue();
        Close(map.Coordinates[pixel * 6 + 3], -(x1 - x2) / d).Should().BeTrue();
        Close(map.Coordinates[pixel * 6 + 4], -(y0 - y2) / d).Should().BeTrue();
        Close(map.Coordinates[pixel * 6 + 5], (x0 - x2) / d).Should().BeTrue();
    }

    [Fact]
    public void combined_is_the_emission_colour_with_alpha_one_where_covered_and_zero_elsewhere()
    {
        //Arrange
        Vector3 color = new(1.5f, .25f, .75f);
        var (scene, target, _) = Build((s, m) => Emission(s, m, color));
        using CyclesScene owned = scene;

        //Act
        FilmBuffer film = scene.Bake(target, new BakeSettings(Size, Size, 4, BakeType.Combined));

        //Assert
        film.Pass.Should().Be(RenderPass.Combined);
        for (int y = 0; y < Size; y++)
        {
            for (int x = 0; x < Size; x++)
            {
                bool covered = PointAt(x, y, Size, LayoutUv, Corners, null).HasValue;
                ReadOnlySpan<float> rgba = film.Span.Slice((y * Size + x) * 4, 4);
                Close(rgba[0], covered ? color.X : 0).Should().BeTrue();
                Close(rgba[1], covered ? color.Y : 0).Should().BeTrue();
                Close(rgba[2], covered ? color.Z : 0).Should().BeTrue();
                Close(rgba[3], covered ? 1 : 0).Should().BeTrue();
            }
        }
    }

    [Fact]
    public void an_image_bakes_back_to_itself_at_any_sample_count()
    {
        //Arrange
        const int width = 8, height = 6;
        float[] pixels = new float[width * height * 4];
        for (int i = 0; i < width * height; i++)
        {
            pixels[i * 4] = (i % width + 1) / 8f;
            pixels[i * 4 + 1] = (i / width + 1) / 8f;
            pixels[i * 4 + 2] = (i % 5 + 1) / 16f;
            pixels[i * 4 + 3] = 1;
        }
        var (scene, target, _) = Build((s, m) =>
        {
            ImageTextureNode texture = m.Add<ImageTextureNode>();
            texture.Interpolation = ImageTextureInterpolation.Closest;
            texture.Bind(s.CreateImage<float>(width, height, ImageFormat.Float4, pixels));
            EmissionNode emission = m.Add<EmissionNode>();
            emission.Strength = 1;
            m.Connect(texture.ColorOutput, emission.ColorInput);
            m.Connect(emission.EmissionOutput, m.Output.SurfaceInput);
        }, FullUv);
        using CyclesScene owned = scene;

        foreach (uint samples in new uint[] { 1, 16 })
        {
            //Act
            FilmBuffer film = scene.Bake(target, new BakeSettings(width, height, samples, BakeType.Combined));

            //Assert
            for (int i = 0; i < pixels.Length; i++) Close(film.Span[i], pixels[i]).Should().BeTrue();
        }
    }

    [Fact]
    public void direct_light_under_a_constant_world_is_the_world_and_keeps_the_colour_on_request()
    {
        //Arrange
        Vector3 albedo = new(.5f, .25f, .125f), light = new(2, 1, .5f);
        var (scene, target, _) = Build((s, m) => Diffuse(s, m, albedo), FullUv);
        using CyclesScene owned = scene;
        World(scene, light);

        //Act
        FilmBuffer direct = scene.Bake(target, new BakeSettings(4, 4, 8, BakeType.DiffuseDirect));
        FilmBuffer withAlbedo = scene.Bake(target, new BakeSettings(4, 4, 8, BakeType.DiffuseDirect) { IncludeAlbedo = true });
        FilmBuffer color = scene.Bake(target, new BakeSettings(4, 4, 8, BakeType.DiffuseColor));

        //Assert
        direct.Components.Should().Be(3);
        for (int i = 0; i < 16; i++)
        {
            Close(direct.Span[i * 3], light.X).Should().BeTrue();
            Close(direct.Span[i * 3 + 1], light.Y).Should().BeTrue();
            Close(withAlbedo.Span[i * 3], light.X * albedo.X).Should().BeTrue();
            Close(color.Span[i * 3 + 2], albedo.Z).Should().BeTrue();
        }
    }

    [Fact]
    public void a_named_vertex_uv_attribute_selects_the_layout()
    {
        //Arrange
        var (scene, target, mesh) = Build((s, m) => Diffuse(s, m, new Vector3(.5f)));
        using CyclesScene owned = scene;
        mesh.SetAttribute("second", AttributeValue.Point2, AttributeDomain.Vertex, AttributeFlags.None, [.1f, .1f, .9f, .1f, .9f, .9f, .1f, .9f]);

        //Act
        BakeMap standard = scene.ReadBakeMap(target, new BakeSettings(Size, Size, 1, BakeType.Combined));
        BakeMap second = scene.ReadBakeMap(target, new BakeSettings(Size, Size, 1, BakeType.Combined) { UvAttribute = "second" });

        //Assert
        second.GetTriangle(Size / 2, Size / 2).Should().NotBe(-1);
        second.Triangles.ToArray().SequenceEqual(standard.Triangles.ToArray()).Should().BeFalse();
    }

    [Fact]
    public void render_after_a_bake_and_after_an_edit_is_bit_identical_to_a_new_scene()
    {
        //Arrange
        RenderSettings settings = new(24, 16, 8);
        Transform3x4 moved = Transform3x4.CreateTranslation(.5f, 0, 0);
        void Camera(CyclesScene s) { s.Camera.Matrix = Transform3x4.CreateTranslation(0, 0, -6); World(s, new Vector3(.3f, .4f, .5f)); }
        var (reference, _, _) = Build((s, m) => Diffuse(s, m, new Vector3(.7f, .25f, .1f)), transform: moved);
        using CyclesScene ownedReference = reference;
        Camera(reference);
        float[] expected = reference.Render(settings).ToArray();
        var (scene, target, _) = Build((s, m) => Diffuse(s, m, new Vector3(.7f, .25f, .1f)));
        using CyclesScene owned = scene;
        Camera(scene);

        //Act
        scene.Bake(target, new BakeSettings(Size, Size, 2, BakeType.Combined));
        target.Tfm = moved;
        float[] actual = scene.Render(settings).ToArray();

        //Assert
        actual.SequenceEqual(expected).Should().BeTrue();
    }

    [Fact]
    public void a_bake_clears_the_last_render_passes_and_keeps_the_callers_pass_nodes()
    {
        //Arrange
        var (scene, target, _) = Build((s, m) => Diffuse(s, m, new Vector3(.5f)), FullUv);
        using CyclesScene owned = scene;
        scene.Camera.Matrix = Transform3x4.CreateTranslation(0, 0, -6);
        scene.AddPass(PassType.Position, "P");
        scene.Render(new RenderSettings(8, 8, 1));

        //Act
        FilmBuffer position = scene.Bake(target, new BakeSettings(4, 4, 1, BakeType.Position));

        //Assert
        position.Components.Should().Be(3);
        Assert.Throws<InvalidOperationException>(() => scene.ReadPass("P"));
        scene.Render(new RenderSettings(8, 8, 1));
        scene.ReadPass("P").Components.Should().Be(3);
    }

    [Fact]
    public void an_object_without_uvs_or_of_another_scene_is_rejected()
    {
        //Arrange
        var (scene, target, _) = Build((s, m) => Diffuse(s, m, new Vector3(.5f)));
        using CyclesScene owned = scene;
        ObjectNode bare = scene.CreateObject(scene.CreateMesh(Corners, Quad, null));
        using CyclesScene other = TestScenes.CreateCpuScene();
        BakeSettings settings = new(Size, Size, 1, BakeType.Combined);

        //Act and Assert
        Assert.Throws<ArgumentNullException>(() => scene.Bake(null, settings));
        Assert.Throws<ArgumentException>(() => other.Bake(target, settings));
        NativeFailure failure = Assert.Throws<NativeFailure>(() => scene.Bake(bare, settings));
        failure.NativeStatus.Should().Be(NativeStatus.Invalid);
        failure.Message.Should().Contain("UV");
    }

    [Fact]
    public async Task async_bake_reports_progress_and_a_cancelled_token_stops_it()
    {
        //Arrange
        var (scene, target, _) = Build((s, m) => Emission(s, m, new Vector3(1)), FullUv);
        await using CyclesScene owned = scene;
        List<RenderStage> stages = [];
        Progress<RenderProgress> progress = new(p => { lock (stages) stages.Add(p.Stage); });
        using CancellationTokenSource cancelled = new();
        await cancelled.CancelAsync();

        //Act
        FilmBuffer film = await scene.BakeAsync(target, new BakeSettings(Size, Size, 2, BakeType.Emission), progress, TestContext.Current.CancellationToken);
        Task<FilmBuffer> refused = scene.BakeAsync(target, new BakeSettings(Size, Size, 2, BakeType.Emission), null, cancelled.Token);

        //Assert
        Close(film.Span[0], 1).Should().BeTrue();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => refused);
    }

    [Fact]
    public void capability_report_states_baking_per_backend_and_every_type()
    {
        //Arrange
        TestScenes.RequireNativeHost();

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();

        //Assert
        report.BakingBackends["cpu"].Should().Be("validated");
        if (report.CompiledBackends.Contains("cpu-embree")) report.BakingBackends["cpu-embree"].Should().Be("validated");
        if (report.CompiledBackends.Contains("webgpu")) report.BakingBackends["webgpu"].Should().Be("refused");
        if (report.CompiledBackends.Contains("metal")) report.BakingBackends["metal"].Should().Be("unvalidated");
        report.BakeTypes.Count.Should().Be(Enum.GetValues<BakeType>().Length);
    }

    [Fact]
    public void the_webgpu_device_refuses_a_bake_by_name_and_still_answers_the_map()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        if (CyclesScene.ResolveBackend(CyclesBackend.Gpu) != Backend.WebGpu) Assert.Skip("This binary's GPU backend is not WebGPU.");
        CyclesScene scene;
        try
        {
            scene = new CyclesScene(CyclesBackend.Gpu);
        }
        catch (CyclesException error)
        {
            Assert.Skip("No GPU device is available to the native host: " + error.Message);
            return;
        }
        using CyclesScene owned = scene;
        ShaderNode material = scene.CreateShader();
        Diffuse(scene, material, new Vector3(.5f));
        MeshNode mesh = scene.CreateMesh(Corners, Quad, material);
        mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, LayoutUv);
        ObjectNode target = scene.CreateObject(mesh);
        BakeSettings settings = new(Size, Size, 1, BakeType.Combined);

        //Act
        NativeFailure failure = Assert.Throws<NativeFailure>(() => scene.Bake(target, settings));
        BakeMap map = scene.ReadBakeMap(target, settings);

        //Assert
        failure.NativeStatus.Should().Be(NativeStatus.Unsupported);
        failure.Message.Should().Contain("WebGPU device does not bake");
        map.Triangles.ToArray().Any(t => t >= 0).Should().BeTrue();
    }

    [Fact]
    public void raw_tier_size_query_does_not_bake()
    {
        //Arrange
        var (scene, target, _) = Build((s, m) => Emission(s, m, new Vector3(1)));
        using CyclesScene owned = scene;

        //Act
        bool baked = scene.Host.Bake(new Node(target.Handle.Value), new BakeSettings(4, 2, 1, BakeType.Normal), Span<float>.Empty, out ulong required, out uint components);

        //Assert
        baked.Should().BeFalse();
        required.Should().Be(24UL);
        components.Should().Be(3U);
    }
}
