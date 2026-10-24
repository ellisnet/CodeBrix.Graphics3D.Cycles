// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using CodeBrix.Graphics3D.Cycles.Bundles;
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
