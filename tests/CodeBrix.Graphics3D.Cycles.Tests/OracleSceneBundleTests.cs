// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Runtime.InteropServices;
using CodeBrix.Graphics3D.Cycles.Bundles;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.GltfRender;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// The cbcOracleScene version 1 bundle loader (CodeBrix.Graphics3D.Cycles.Bundles) on the CPU reference.
public class OracleSceneBundleTests
{
    // The starter diffuse triangle of native/oracle/scenes/starter, inline geometry.
    private const string Triangle = """
        {"cbcOracleScene": 1, "id": "test/triangle", "render": {"width": 32, "height": 24, "samples": 8, "repeats": 2}, "colorConfig": null,
         "nodes": [
          {"handle": "integrator", "scene": "integrator", "set": {"use_guiding": false, "use_light_tree": false, "use_adaptive_sampling": false, "use_denoise": false,
            "caustics_reflective": false, "caustics_refractive": false, "min_bounce": 2, "max_bounce": 4, "max_diffuse_bounce": 4, "sampling_pattern": "tabulated_sobol"}},
          {"handle": "camera", "scene": "camera", "set": {"fov": 1.1, "matrix": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -4]}},
          {"handle": "material", "create": "shader", "graph": {"nodes": [{"handle": "diffuse", "type": "diffuse_bsdf", "set": {"roughness": 0.3}},
            {"handle": "value", "type": "color", "set": {"value": [0.7, 0.2, 0.1]}}], "connect": [["value", "Color", "diffuse", "Color"], ["diffuse", "BSDF", "output", "Surface"]]},
            "set": {"emission_sampling_method": "none"}},
          {"handle": "mesh", "create": "mesh", "set": {"used_shaders": ["material"]}, "mesh": MESH},
          {"handle": "object", "create": "object", "set": {"geometry": "mesh"}},
          {"handle": "world", "create": "shader", "graph": {"nodes": [{"handle": "background", "type": "background_shader", "set": {"color": [0.25, 0.4, 0.6], "strength": 0.8}}],
            "connect": [["background", "Background", "output", "Surface"]]}, "set": {"emission_sampling_method": "none"}},
          {"handle": "bg", "scene": "background", "set": {"shader": "world"}}],
         "images": []}
        """;

    private const string InlineMesh = """{"positions": [-1.5, -1, 0, 1.5, -1, 0, 0, 1.5, 0], "indices": [0, 1, 2]}""";
    private const string FileMesh = """{"positionsFile": "p.f32", "indicesFile": "i.u32"}""";

    private static readonly float[] Positions = [-1.5f, -1, 0, 1.5f, -1, 0, 0, 1.5f, 0];

    private static Dictionary<string, byte[]> MeshFiles() => new()
    {
        ["p.f32"] = MemoryMarshal.AsBytes(Positions.AsSpan()).ToArray(),
        ["i.u32"] = MemoryMarshal.AsBytes(new uint[] { 0, 1, 2 }.AsSpan()).ToArray(),
    };

    private static FilmBuffer Render(OracleSceneBundle bundle)
    {
        using CyclesScene scene = TestScenes.CreateCpuScene();
        bundle.Apply(scene);
        return scene.Render(bundle.Render);
    }

    [Fact]
    public void bundle_reads_the_document_header()
    {
        //Arrange / Act
        OracleSceneBundle bundle = OracleSceneBundle.FromJson(Triangle.Replace("MESH", FileMesh), MeshFiles());

        //Assert
        bundle.Id.Should().Be("test/triangle");
        bundle.Render.Should().Be(new RenderSettings(32, 24, 8));
        bundle.Repeats.Should().Be(2u);
        bundle.ColorConfig.Should().BeNull();
        bundle.FileNames.Should().Equal("p.f32", "i.u32");
    }

    [Fact]
    public void bundle_builds_the_scene_and_matches_the_typed_facade_bit_for_bit()
    {
        //Arrange
        OracleSceneBundle bundle = OracleSceneBundle.FromJson(Triangle.Replace("MESH", InlineMesh), new Dictionary<string, byte[]>());
        using CyclesScene typed = TestScenes.CreateCpuScene();
        GltfSceneConverter.ApplyDeterministicIntegrator(typed);
        typed.Camera.Fov = 1.1f;
        typed.Camera.Matrix = Transform3x4.CreateTranslation(0, 0, -4);
        var material = typed.CreateShader();
        material.EmissionSamplingMethod = Generated.Scene.ShaderEmissionSamplingMethod.None;
        var diffuse = material.Add<Generated.Shaders.DiffuseBsdfNode>();
        diffuse.Roughness = .3f;
        var color = material.Add<Generated.Shaders.ColorNode>();
        color.Value = new System.Numerics.Vector3(.7f, .2f, .1f);
        material.Connect(color.ColorOutput, diffuse.ColorInput);
        material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);
        typed.CreateObject(typed.CreateMesh(Positions, [0, 1, 2], material));
        var world = typed.CreateShader();
        var background = world.Add<Generated.Shaders.BackgroundShaderNode>();
        background.Color = new System.Numerics.Vector3(.25f, .4f, .6f);
        background.Strength = .8f;
        world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
        world.EmissionSamplingMethod = Generated.Scene.ShaderEmissionSamplingMethod.None;
        typed.Background.Shader = world;

        //Act
        FilmBuffer fromBundle = Render(bundle);
        FilmBuffer fromTyped = typed.Render(bundle.Render);

        //Assert
        FilmComparison.Compare(fromBundle.Span, fromTyped.Span).BitwiseEqual.Should().BeTrue();
        fromBundle.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(2);
    }

    [Fact]
    public void raw_geometry_files_render_bit_for_bit_like_inline_geometry()
    {
        //Arrange
        OracleSceneBundle inline = OracleSceneBundle.FromJson(Triangle.Replace("MESH", InlineMesh), new Dictionary<string, byte[]>());
        OracleSceneBundle files = OracleSceneBundle.FromJson(Triangle.Replace("MESH", FileMesh), MeshFiles());

        //Act / Assert
        FilmComparison.Compare(Render(files).Span, Render(inline).Span).BitwiseEqual.Should().BeTrue();
    }

    [Fact]
    public void point_and_particle_entries_render_bit_for_bit_like_the_typed_facade()
    {
        //Arrange
        const string nodes = @"
            {""handle"":""integrator"",""scene"":""integrator"",""set"":{""use_light_tree"":false}},
            {""handle"":""system"",""create"":""particle_system"",""particles"":[{""index"":3,""age"":0.5,""lifetime"":1,""location"":[1,1,1],""rotation"":[0,0,0,1],""size"":0.25,""velocity"":[1,0,0],""angularVelocity"":[0,1,0]}]},
            {""handle"":""points"",""create"":""pointcloud"",""points"":{""positions"":[-0.5,0,0,0.5,0,0],""radius"":[0.3,0.2],
              ""attributes"":[{""name"":""point_random"",""value"":""float"",""domain"":""vertex"",""flags"":[""standard""],""data"":[0.25,0.75]}],
              ""motion"":{""steps"":3,""positions"":[-0.5,0,0.1,0.5,0,0.1,-0.5,0,0,0.5,0,0,-0.5,0,-0.1,0.5,0,-0.1],""radius"":[0.3,0.2,0.3,0.2,0.4,0.2]}}},
            {""handle"":""object"",""create"":""object"",""set"":{""geometry"":""points"",""particle_system"":""system"",""particle_index"":0}}";
        string json = @"{""cbcOracleScene"":1,""id"":""t"",""render"":{""width"":32,""height"":24,""samples"":4},""colorConfig"":null,""images"":[],""nodes"":[" + nodes + "]}";
        using CyclesScene loaded = TestScenes.CreateCpuScene();
        using CyclesScene typed = TestScenes.CreateCpuScene();
        loaded.Integrator.UseLightTree = false;
        OracleSceneBundle.FromJson(json, new Dictionary<string, byte[]>()).Apply(loaded);
        typed.Integrator.UseLightTree = false;
        ParticleSystemNode system = typed.CreateParticleSystem([new Particle(3, .5f, 1f, Vector3.One, Vector4.UnitW, .25f, Vector3.UnitX, Vector3.UnitY)]);
        PointCloudNode points = typed.CreatePointCloud([-.5f, 0f, 0f, .5f, 0f, 0f], [.3f, .2f]);
        points.SetAttribute("point_random", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.Standard, [.25f, .75f]);
        points.SetMotion(3, [-.5f, 0f, .1f, .5f, 0f, .1f, -.5f, 0f, 0f, .5f, 0f, 0f, -.5f, 0f, -.1f, .5f, 0f, -.1f], [.3f, .2f, .3f, .2f, .4f, .2f]);
        ObjectNode placed = typed.CreateObject(points);
        placed.ParticleSystem = system;
        placed.ParticleIndex = 0;

        //Act
        FilmBuffer a = loaded.Render(new RenderSettings(32, 24, 4));
        FilmBuffer b = typed.Render(new RenderSettings(32, 24, 4));

        //Assert
        a.Span.SequenceEqual(b.Span).Should().BeTrue();
    }

    [Fact]
    public void hair_entries_render_bit_for_bit_like_the_typed_facade()
    {
        //Arrange
        const string nodes = @"
            {""handle"":""integrator"",""scene"":""integrator"",""set"":{""use_light_tree"":false}},
            {""handle"":""hair"",""create"":""hair"",""hair"":{""keys"":[-0.5,-0.5,0,-0.1,-0.1,0,0.1,0.1,0,0.5,0.5,0],""radius"":[0.2,0.15,0.15,0.1],""firstKey"":[0],""shape"":""thick"",
              ""attributes"":[{""name"":""curve_random"",""value"":""float"",""domain"":""curve"",""flags"":[""standard""],""data"":[0.25]}]}},
            {""handle"":""object"",""create"":""object"",""set"":{""geometry"":""hair""}}";
        string json = @"{""cbcOracleScene"":1,""id"":""t"",""render"":{""width"":32,""height"":24,""samples"":4,""curveSubdivisions"":2},""colorConfig"":null,""images"":[],""nodes"":[" + nodes + "]}";
        using CyclesScene loaded = TestScenes.CreateCpuScene();
        using CyclesScene typed = TestScenes.CreateCpuScene();
        loaded.Integrator.UseLightTree = false;
        OracleSceneBundle.FromJson(json, new Dictionary<string, byte[]>()).Apply(loaded);
        typed.Integrator.UseLightTree = false;
        typed.SetCurveSubdivisions(2);
        HairNode hair = typed.CreateHair([-.5f, -.5f, 0f, -.1f, -.1f, 0f, .1f, .1f, 0f, .5f, .5f, 0f], [.2f, .15f, .15f, .1f], [0u], CurveShape.Thick);
        hair.SetAttribute("curve_random", AttributeValue.Float, AttributeDomain.Curve, AttributeFlags.Standard, [.25f]);
        typed.CreateObject(hair);

        //Act
        FilmBuffer a = loaded.Render(new RenderSettings(32, 24, 4));
        FilmBuffer b = typed.Render(new RenderSettings(32, 24, 4));

        //Assert
        a.Span.SequenceEqual(b.Span).Should().BeTrue();
    }

    [Fact]
    public void bundle_hair_motion_renders_like_the_typed_api()
    {
        //Arrange
        string keys = "[-0.5,-0.5,0,-0.1,-0.1,0,0.1,0.1,0,0.5,0.5,0]";
        float[] steps = [-.6f, -.5f, 0f, -.2f, -.1f, 0f, 0f, .1f, 0f, .4f, .5f, 0f, -.5f, -.5f, 0f, -.1f, -.1f, 0f, .1f, .1f, 0f, .5f, .5f, 0f, -.4f, -.5f, 0f, 0f, -.1f, 0f, .2f, .1f, 0f, .6f, .5f, 0f];
        float[] radii = [.1f, .1f, .1f, .1f, .2f, .15f, .15f, .1f, .3f, .2f, .2f, .1f];
        string list(float[] values) => "[" + string.Join(",", values.Select(v => v.ToString("R", System.Globalization.CultureInfo.InvariantCulture))) + "]";
        string nodes = @"{""handle"":""integrator"",""scene"":""integrator"",""set"":{""motion_blur"":true,""use_light_tree"":false}},{""handle"":""camera"",""scene"":""camera"",""set"":{""shuttertime"":0.6}},
            {""handle"":""hair"",""create"":""hair"",""hair"":{""keys"":" + keys + @",""radius"":[0.2,0.15,0.15,0.1],""firstKey"":[0],""shape"":""thick"",""motion"":{""steps"":3,""keys"":" + list(steps) + @",""radius"":" + list(radii) + @"}}},
            {""handle"":""object"",""create"":""object"",""set"":{""geometry"":""hair""}}";
        string json = @"{""cbcOracleScene"":1,""id"":""t"",""render"":{""width"":32,""height"":24,""samples"":4},""colorConfig"":null,""images"":[],""nodes"":[" + nodes + "]}";
        using CyclesScene loaded = TestScenes.CreateCpuScene();
        using CyclesScene typed = TestScenes.CreateCpuScene();
        OracleSceneBundle.FromJson(json, new Dictionary<string, byte[]>()).Apply(loaded);
        typed.Integrator.UseLightTree = false;
        typed.Integrator.MotionBlur = true;
        typed.Camera.Shuttertime = .6f;
        HairNode hair = typed.CreateHair([-.5f, -.5f, 0f, -.1f, -.1f, 0f, .1f, .1f, 0f, .5f, .5f, 0f], [.2f, .15f, .15f, .1f], [0u], CurveShape.Thick);
        hair.SetMotionKeys(3, steps, radii);
        typed.CreateObject(hair);

        //Act
        FilmBuffer a = loaded.Render(new RenderSettings(32, 24, 4));
        FilmBuffer b = typed.Render(new RenderSettings(32, 24, 4));

        //Assert
        a.Span.SequenceEqual(b.Span).Should().BeTrue();
    }

    // The committed scene with both of these attribute kinds (native/oracle/scenes/attributes): the loader passes the
    // value names "quaternion" and "spherical_harmonics_rest" to the host, which reads the quaternions back exactly and the
    // coefficients within half an 8-bit step, and the scene renders repeat-identically.
    [Fact]
    public void quaternion_and_spherical_harmonics_attributes_of_a_committed_scene_reach_the_host()
    {
        //Arrange
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        string path = Path.Combine(RepositoryPaths.Root, "native", "oracle", "scenes", "attributes", "quaternion-and-spherical-harmonics.json");
        OracleSceneBundle bundle = OracleSceneBundle.Load(path);
        using System.Text.Json.JsonDocument document = System.Text.Json.JsonDocument.Parse(bundle.Json);
        System.Text.Json.JsonElement sphere = document.RootElement.GetProperty("nodes").EnumerateArray().First(n => n.GetProperty("handle").GetString() == "sphere");
        float[] Data(string name) => sphere.GetProperty("mesh").GetProperty("attributes").EnumerateArray()
            .First(a => a.GetProperty("name").GetString() == name).GetProperty("data").EnumerateArray().Select(v => v.GetSingle()).ToArray();
        using CyclesScene scene = TestScenes.CreateCpuScene();
        RenderSettings settings = new(16, 12, 2);

        //Act
        IReadOnlyDictionary<string, Node> nodes = bundle.Apply(scene);
        GeometryBaseNode mesh = scene.Wrap<GeometryBaseNode>(nodes["sphere"]);
        FilmBuffer first = scene.Render(settings);
        FilmBuffer second = scene.Render(settings);

        //Assert
        mesh.GetAttribute("q").Should().Equal(Data("q"));
        float[] coefficients = Data("sh");
        mesh.GetAttribute("sh").Zip(coefficients, (read, set) => MathF.Abs(read - set)).Max().Should().BeLessThanOrEqualTo(1f / 255f + 1e-6f);
        first.Span.SequenceEqual(second.Span).Should().BeTrue();
        first.Span.ToArray().All(float.IsFinite).Should().BeTrue();
    }

    // A committed Gaussian splat scene (native/oracle/scenes/gsplat): the loader passes the standard flag and the render_as
    // socket to the host, the splats render repeat-identically, and the authored rotation and radius read back after the
    // render (Cycles bounds the splats with a radius of its own).
    [Fact]
    public void a_committed_gaussian_splat_scene_renders_through_the_loader()
    {
        //Arrange
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        string path = Path.Combine(RepositoryPaths.Root, "native", "oracle", "scenes", "gsplat", "spherical-harmonics.json");
        OracleSceneBundle bundle = OracleSceneBundle.Load(path);
        using System.Text.Json.JsonDocument document = System.Text.Json.JsonDocument.Parse(bundle.Json);
        System.Text.Json.JsonElement cloud = document.RootElement.GetProperty("nodes").EnumerateArray().First(n => n.GetProperty("handle").GetString() == "splats");
        float[] Values(System.Text.Json.JsonElement array) => array.EnumerateArray().Select(v => v.GetSingle()).ToArray();
        float[] Data(string name) => Values(cloud.GetProperty("points").GetProperty("attributes").EnumerateArray()
            .First(a => a.GetProperty("name").GetString() == name).GetProperty("data"));
        using CyclesScene scene = TestScenes.CreateCpuScene();
        RenderSettings settings = new(16, 12, 2);

        //Act
        IReadOnlyDictionary<string, Node> nodes = bundle.Apply(scene);
        PointCloudNode splats = scene.Wrap<PointCloudNode>(nodes["splats"]);
        FilmBuffer first = scene.Render(settings);
        FilmBuffer second = scene.Render(settings);

        //Assert
        splats.RenderAs.Should().Be(PointCloudRenderAs.GaussianSplats);
        splats.GetAttribute("rotation", AttributeFlags.Standard).Should().Equal(Data("rotation"));
        splats.GetAttribute("radius").Should().Equal(Values(cloud.GetProperty("points").GetProperty("radius")));
        first.Span.SequenceEqual(second.Span).Should().BeTrue();
        first.Span.ToArray().All(float.IsFinite).Should().BeTrue();
    }

    // A committed UDIM scene (native/oracle/scenes/images): the image pseudo-socket's object form binds the tiles in document
    // order through the loader; the scene renders repeat-identically, differs from the same document with one tile
    // removed, and a tile key that is not a number or names no image is refused by name.
    [Fact]
    public void a_committed_udim_scene_binds_its_tiles_through_the_loader()
    {
        //Arrange
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        string path = Path.Combine(RepositoryPaths.Root, "native", "oracle", "scenes", "images", "udim-missing-tile.json");
        OracleSceneBundle bundle = OracleSceneBundle.Load(path);
        Dictionary<string, byte[]> files = bundle.FileNames.ToDictionary(name => name, bundle.ReadFile);
        const string tiles = "\"image\": {\n        \"1003\": \"a\",\n        \"1001\": \"a\",";
        bundle.Json.Should().Contain(tiles);
        OracleSceneBundle withoutTile = OracleSceneBundle.FromJson(bundle.Json.Replace(tiles, "\"image\": {\n        \"1003\": \"a\","), files);
        OracleSceneBundle notANumber = OracleSceneBundle.FromJson(bundle.Json.Replace("\"1003\": \"a\"", "\"u1003\": \"a\""), files);
        OracleSceneBundle unknownImage = OracleSceneBundle.FromJson(bundle.Json.Replace("\"1003\": \"a\"", "\"1003\": \"z\""), files);

        //Act
        FilmBuffer first = Render(bundle);
        FilmBuffer second = Render(bundle);
        FilmBuffer missingTile = Render(withoutTile);
        Action badKey = () => Render(notANumber);
        Action badImage = () => Render(unknownImage);

        //Assert
        first.Span.SequenceEqual(second.Span).Should().BeTrue();
        first.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        missingTile.Span.SequenceEqual(first.Span).Should().BeFalse();
        badKey.Should().Throw<FormatException>().Which.Message.Should().Contain("UDIM tile 'u1003'");
        badImage.Should().Throw<FormatException>().Which.Message.Should().Contain("Unknown image handle for UDIM tile 1003");
    }

    // A committed tiled image scene (native/oracle/scenes/images, provisional): an images entry with "tiled" becomes a
    // tiled image whose levels the loader serves from the entry's file and the files of "tiled.levels", which the bundle
    // lists among its files; a second scene renders it within the practical criterion (the last bits of a Linear or Cubic
    // lookup follow the order in which Cycles' texture cache loaded the tiles, which several threads do not fix), and
    // unlike the same document with its smallest level replaced by black pixels (the quad that shows the whole image reads
    // that level).
    [Fact]
    public void a_committed_tiled_image_scene_reads_its_levels_through_the_loader()
    {
        //Arrange
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        string path = Path.Combine(RepositoryPaths.Root, "native", "oracle", "scenes", "images", "tiled-mip-planes.json");
        OracleSceneBundle bundle = OracleSceneBundle.Load(path);
        Dictionary<string, byte[]> files = bundle.FileNames.ToDictionary(name => name, bundle.ReadFile);
        const string smallest = "tiled-mip-planes.image.level-2.byte4";
        Dictionary<string, byte[]> blackened = new(files) { [smallest] = new byte[files[smallest].Length] };
        OracleSceneBundle withBlackLevel = OracleSceneBundle.FromJson(bundle.Json, blackened);

        //Act
        FilmBuffer first = Render(bundle);
        FilmBuffer second = Render(bundle);
        FilmBuffer black = Render(withBlackLevel);

        //Assert
        bundle.FileNames.Should().Contain(new[] { "tiled-mip-planes.image.level-0.byte4", "tiled-mip-planes.image.level-1.byte4", smallest });
        FilmComparison.Compare(first.Span, second.Span).PracticalFailures.Should().Be(0);
        first.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        FilmComparison.Compare(black.Span, first.Span).PracticalFailures.Should().BeGreaterThan(0);
    }

    // The texture limit (provisional render.textureLimit): the loader sets the scene's TextureLimit before it binds the
    // images, so the committed 64x64 block image under a limit of 32 renders bit for bit like its committed twin, the
    // 32x32 image of the block colours, and unlike the same document without the key.
    [Fact]
    public void a_committed_texture_limit_scene_sets_the_limit_through_the_loader()
    {
        //Arrange
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        string folder = Path.Combine(RepositoryPaths.Root, "native", "oracle", "scenes", "images");
        OracleSceneBundle limited = OracleSceneBundle.Load(Path.Combine(folder, "texture-limit-blocks.json"));
        OracleSceneBundle halved = OracleSceneBundle.Load(Path.Combine(folder, "texture-limit-blocks-halved.json"));
        Dictionary<string, byte[]> files = limited.FileNames.ToDictionary(name => name, limited.ReadFile);
        const string key = ",\n  \"textureLimit\": 32\n";
        limited.Json.Should().Contain(key);
        OracleSceneBundle unlimited = OracleSceneBundle.FromJson(limited.Json.Replace(key, "\n"), files);
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        limited.Apply(scene);
        FilmBuffer film = scene.Render(limited.Render);
        FilmBuffer twin = Render(halved);
        FilmBuffer full = Render(unlimited);

        //Assert
        scene.TextureLimit.Should().Be(32u);
        film.Span.SequenceEqual(twin.Span).Should().BeTrue();
        film.Span.SequenceEqual(full.Span).Should().BeFalse();
    }

    // BVH spatial splits (provisional render.bvhSpatialSplit): the loader switches them on before it builds the scene, the
    // film is bit for bit the document's film without the key (a speed setting), and a value that is not a boolean is
    // refused.
    [Fact]
    public void the_bvh_spatial_split_key_switches_the_setting_and_changes_no_film()
    {
        //Arrange
        static string Document(string extra) => Triangle.Replace("MESH", InlineMesh).Replace("\"samples\": 8", "\"samples\": 8" + extra);
        OracleSceneBundle split = OracleSceneBundle.FromJson(Document(", \"bvhSpatialSplit\": true"), new Dictionary<string, byte[]>());
        OracleSceneBundle plain = OracleSceneBundle.FromJson(Document(string.Empty), new Dictionary<string, byte[]>());
        OracleSceneBundle number = OracleSceneBundle.FromJson(Document(", \"bvhSpatialSplit\": 1"), new Dictionary<string, byte[]>());
        split.Json.Should().Contain("\"bvhSpatialSplit\": true");
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        split.Apply(scene);
        FilmBuffer film = scene.Render(split.Render);
        Action notBoolean = () => Render(number);

        //Assert
        scene.UseBvhSpatialSplit.Should().BeTrue();
        film.Span.SequenceEqual(Render(plain).Span).Should().BeTrue();
        notBoolean.Should().Throw<InvalidOperationException>();
    }

    // Tiled rendering (provisional render.tileSize): the loader sets the scene's tile size before it builds the scene, the
    // film is bit for bit the document's film without the key (the CPU reference renders tiles exactly), the 32 x 24
    // film rendered in 12 tiles kept in memory, and a size Cycles would change is refused.
    [Fact]
    public void the_tile_size_key_renders_in_tiles_and_changes_no_film()
    {
        //Arrange
        static string Document(string extra) => Triangle.Replace("MESH", InlineMesh).Replace("\"samples\": 8", "\"samples\": 8" + extra);
        OracleSceneBundle tiled = OracleSceneBundle.FromJson(Document(", \"tileSize\": 8"), new Dictionary<string, byte[]>());
        OracleSceneBundle plain = OracleSceneBundle.FromJson(Document(string.Empty), new Dictionary<string, byte[]>());
        OracleSceneBundle invalid = OracleSceneBundle.FromJson(Document(", \"tileSize\": 7"), new Dictionary<string, byte[]>());
        tiled.Json.Should().Contain("\"tileSize\": 8");
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        tiled.Apply(scene);
        FilmBuffer film = scene.Render(tiled.Render);
        Action refused = () => Render(invalid);

        //Assert
        scene.TileSize.Should().Be(8u);
        TiledRenderingTests.Report(scene).TilesKept.Should().Be(12);
        film.Span.SequenceEqual(Render(plain).Span).Should().BeTrue();
        refused.Should().Throw<ArgumentOutOfRangeException>();
    }

    [Fact]
    public void bundle_saves_and_loads_from_a_directory_unchanged()
    {
        //Arrange
        OracleSceneBundle bundle = OracleSceneBundle.FromJson(Triangle.Replace("MESH", FileMesh), MeshFiles());
        string directory = Path.Combine(Path.GetTempPath(), "cycles-bundle-" + Guid.NewGuid().ToString("N"));
        try
        {
            //Act
            string path = bundle.Save(directory);
            OracleSceneBundle loaded = OracleSceneBundle.Load(path);
            Action again = () => bundle.Save(directory);

            //Assert
            loaded.Json.Should().Be(bundle.Json);
            loaded.ReadFile("p.f32").Should().Equal(bundle.ReadFile("p.f32"));
            FilmComparison.Compare(Render(loaded).Span, Render(bundle).Span).BitwiseEqual.Should().BeTrue();
            again.Should().Throw<IOException>();
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void bundle_rejects_another_format_version()
    {
        //Arrange / Act
        Action load = () => OracleSceneBundle.FromJson(Triangle.Replace("MESH", InlineMesh).Replace("\"cbcOracleScene\": 1", "\"cbcOracleScene\": 2"), new Dictionary<string, byte[]>());

        //Assert
        load.Should().Throw<FormatException>();
    }

    [Fact]
    public void bundle_rejects_file_names_outside_its_directory()
    {
        //Arrange / Act
        Action load = () => OracleSceneBundle.FromJson(Triangle.Replace("MESH", FileMesh.Replace("p.f32", "../p.f32")), MeshFiles());

        //Assert
        load.Should().Throw<FormatException>();
    }

    [Fact]
    public void apply_names_an_unknown_socket()
    {
        //Arrange
        OracleSceneBundle bundle = OracleSceneBundle.FromJson(Triangle.Replace("MESH", InlineMesh).Replace("\"fov\": 1.1", "\"no_such_socket\": 1.1"), new Dictionary<string, byte[]>());
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        Action apply = () => bundle.Apply(scene);

        //Assert
        apply.Should().Throw<InvalidOperationException>().Which.Message.Should().Contain("no_such_socket");
    }

    [Fact]
    public void apply_rejects_a_pixel_file_of_the_wrong_size()
    {
        //Arrange
        string json = Triangle.Replace("MESH", InlineMesh).Replace("\"images\": []", "\"images\": [{\"handle\": \"img\", \"width\": 2, \"height\": 2, \"format\": \"byte4\", \"file\": \"img.byte4\"}]");
        OracleSceneBundle bundle = OracleSceneBundle.FromJson(json, new Dictionary<string, byte[]> { ["img.byte4"] = new byte[15] });
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        Action apply = () => bundle.Apply(scene);

        //Assert
        apply.Should().Throw<FormatException>().Which.Message.Should().Contain("img");
    }
}
