// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Lights;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.GltfRender;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Edits of a mesh with true displacement after a render (native/host/ABI.md "Edits after a render"). Cycles adds a mesh's
// displacement to its current positions whenever it updates the mesh (GeometryManager::displace), so the host restores the
// authored attributes first. The scene: a grid displaced along its normal by noise of the world position times a custom
// vertex attribute, a second displaced material, lit by a point lamp from the side. Every edit after a render is judged
// against a new scene that applies the same calls before its first render, bit for bit on the CPU reference.
public class DisplacementEditTests
{
    private readonly ITestOutputHelper output;

    public DisplacementEditTests(ITestOutputHelper output) => this.output = output;

    internal static readonly RenderSettings Settings = new(48, 32, 8);

    internal sealed record Parts(MeshNode Mesh, ObjectNode Object, ShaderNode Displaced, DisplacementNode Displacement,
        ShaderNode Second, ShaderNode Unused, float[] Positions, float[] Weights);

    private const int Cells = 16;

    internal static Parts Build(CyclesScene scene)
    {
        IntegratorNode integrator = scene.Integrator;
        integrator.UseGuiding = false;
        integrator.UseLightTree = false;
        integrator.UseAdaptiveSampling = false;
        integrator.UseDenoise = false;
        integrator.CausticsReflective = false;
        integrator.CausticsRefractive = false;
        integrator.MaxBounce = 3;
        integrator.SamplingPattern = IntegratorSamplingPattern.TabulatedSobol;
        scene.Camera.Fov = .9f;
        scene.Camera.Matrix = Transform3x4.CreateTranslation(0, 0, -4.5f);

        ShaderNode displaced = scene.CreateShader();
        DiffuseBsdfNode bsdf = displaced.Add<DiffuseBsdfNode>();
        bsdf.Color = new Vector3(.8f, .55f, .35f);
        GeometryNode geometry = displaced.Add<GeometryNode>();
        NoiseTextureNode noise = displaced.Add<NoiseTextureNode>();
        noise.Scale = 2.5f;
        AttributeNode weight = displaced.Add<AttributeNode>();
        weight.Attribute = "weight";
        MathNode multiply = displaced.Add<MathNode>();
        multiply.MathType = MathMathType.Multiply;
        DisplacementNode displacement = displaced.Add<DisplacementNode>();
        displacement.Scale = .45f;
        displaced.Connect(bsdf.BsdfOutput, displaced.Output.SurfaceInput);
        displaced.Connect(geometry.PositionOutput, noise.VectorInput);
        displaced.Connect(noise.FacOutput, multiply.Value1Input);
        displaced.Connect(weight.FacOutput, multiply.Value2Input);
        displaced.Connect(multiply.ValueOutput, displacement.HeightInput);
        displaced.Connect(displacement.DisplacementOutput, displaced.Output.DisplacementInput);
        displaced.DisplacementMethod = ShaderDisplacementMethod.True;
        displaced.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;

        ShaderNode second = scene.CreateShader();
        DiffuseBsdfNode secondBsdf = second.Add<DiffuseBsdfNode>();
        secondBsdf.Color = new Vector3(.3f, .5f, .8f);
        NoiseTextureNode secondNoise = second.Add<NoiseTextureNode>();
        secondNoise.Scale = 4f;
        DisplacementNode secondDisplacement = second.Add<DisplacementNode>();
        secondDisplacement.Scale = .3f;
        second.Connect(secondBsdf.BsdfOutput, second.Output.SurfaceInput);
        second.Connect(secondNoise.FacOutput, secondDisplacement.HeightInput);
        second.Connect(secondDisplacement.DisplacementOutput, second.Output.DisplacementInput);
        second.DisplacementMethod = ShaderDisplacementMethod.True;
        second.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;

        // A third displaced material that no geometry references until the "new-material" edit (Cycles skips it when it
        // compiles the shaders of a render).
        ShaderNode unused = scene.CreateShader();
        DiffuseBsdfNode unusedBsdf = unused.Add<DiffuseBsdfNode>();
        unusedBsdf.Color = new Vector3(.4f, .75f, .3f);
        WaveTextureNode wave = unused.Add<WaveTextureNode>();
        wave.Scale = 3f;
        DisplacementNode unusedDisplacement = unused.Add<DisplacementNode>();
        unusedDisplacement.Scale = .2f;
        unused.Connect(unusedBsdf.BsdfOutput, unused.Output.SurfaceInput);
        unused.Connect(wave.FacOutput, unusedDisplacement.HeightInput);
        unused.Connect(unusedDisplacement.DisplacementOutput, unused.Output.DisplacementInput);
        unused.DisplacementMethod = ShaderDisplacementMethod.True;
        unused.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;

        List<float> grid = [];
        List<float> weights = [];
        for (int y = 0; y <= Cells; y++)
            for (int x = 0; x <= Cells; x++)
            {
                grid.AddRange([-1.6f + 3.2f * x / Cells, -1.1f + 2.2f * y / Cells, 0f]);
                weights.Add(.5f + (x * 7 + y * 3) % 11 / 10f);
            }
        float[] positions = [.. grid];
        float[] vertexWeights = [.. weights];
        MeshNode mesh = scene.CreateMesh(positions, GridIndices(), displaced);
        // The second material is one of the mesh's shaders from the start; faces move to it with the "face-shaders" edit.
        mesh.UsedShaders = [displaced, second];
        mesh.SetAttribute("weight", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.None, vertexWeights);
        ObjectNode placed = scene.CreateObject(mesh, new Transform3x4(1, 0, 0, 0, 0, .94f, -.34f, 0, 0, .34f, .94f, 0));

        ShaderNode lamp = scene.CreateShader();
        EmissionNode emission = lamp.Add<EmissionNode>();
        emission.Color = Vector3.One;
        emission.Strength = 1f;
        lamp.Connect(emission.EmissionOutput, lamp.Output.SurfaceInput);
        PointLightNode light = scene.CreateLight<PointLightNode>();
        light.Strength = new Vector3(90f, 85f, 80f);
        light.Radius = .1f;
        light.UsedShaders = [lamp];
        scene.CreateObject(light, Transform3x4.CreateTranslation(-2.6f, 1.8f, -2.2f));

        ShaderNode world = scene.CreateShader();
        BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
        background.Color = new Vector3(.08f, .1f, .12f);
        background.Strength = 1f;
        world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
        world.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        scene.Background.Shader = world;
        return new Parts(mesh, placed, displaced, displacement, second, unused, positions, vertexWeights);
    }

    // The edits: sockets of the mesh (smooth, face shaders, a material no geometry used before), an attribute the
    // displacement reads, the displacement shader (a node value; its method, Bump, which switches the displacement off), the
    // object's transform, and a new instance.
    internal static void Edit(CyclesScene scene, Parts parts, string edit)
    {
        switch (edit)
        {
            case "smooth":
                parts.Mesh.Smooth = [.. parts.Mesh.Smooth.Select(_ => true)];
                break;
            case "face-shaders":
                parts.Mesh.Shader = [.. parts.Mesh.Shader.Select((_, face) => face / 2 % 3 == 0 ? 1 : 0)];
                break;
            case "new-material":
                parts.Mesh.UsedShaders = [parts.Displaced, parts.Second, parts.Unused];
                parts.Mesh.Shader = [.. parts.Mesh.Shader.Select((_, face) => face % 5 == 0 ? 2 : 0)];
                break;
            case "attribute":
                parts.Mesh.SetAttribute("weight", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.None,
                    [.. parts.Weights.Select(w => 2f - w)]);
                break;
            case "displacement-scale":
                parts.Displacement.Scale = .25f;
                break;
            case "transform":
                parts.Object.Tfm = new Transform3x4(.96f, -.1f, 0, .15f, .09f, .9f, -.4f, -.1f, .04f, .4f, .91f, .2f);
                break;
            case "instance":
                scene.CreateObject(parts.Mesh, new Transform3x4(.4f, 0, 0, 1.3f, 0, .4f, 0, .7f, 0, 0, .4f, -.6f));
                break;
            case "bump":
                parts.Displaced.DisplacementMethod = ShaderDisplacementMethod.Bump;
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(edit), edit, null);
        }
    }

    private static FilmBuffer Fresh(params string[] edits)
    {
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Parts parts = Build(scene);
        foreach (string edit in edits) Edit(scene, parts, edit);
        return scene.Render(Settings);
    }

    [Fact]
    public void a_displaced_mesh_renders_like_a_new_scene_and_repeats_bit_for_bit()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Build(scene);

        //Act
        FilmBuffer first = scene.Render(Settings);
        FilmBuffer second = scene.Render(Settings);
        FilmBuffer flat = Fresh("bump");

        //Assert
        first.Span.SequenceEqual(Fresh().Span).Should().BeTrue();
        second.Span.SequenceEqual(first.Span).Should().BeTrue();
        first.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        // The displacement is visible: without it (Bump) the film differs well beyond the practical criterion.
        FilmComparison versusFlat = FilmComparison.Compare(first.Span, flat.Span);
        output.WriteLine($"true displacement vs bump: {versusFlat}");
        versusFlat.PracticalFailures.Should().BeGreaterThan(100);
    }

    [Theory]
    [InlineData("smooth")]
    [InlineData("face-shaders")]
    [InlineData("new-material")]
    [InlineData("attribute")]
    [InlineData("displacement-scale")]
    [InlineData("transform")]
    [InlineData("instance")]
    [InlineData("bump")]
    public void an_edit_after_a_render_renders_bit_for_bit_like_a_new_scene_built_with_it(string edit)
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Parts parts = Build(scene);
        FilmBuffer first = scene.Render(Settings);

        //Act
        Edit(scene, parts, edit);
        FilmBuffer edited = scene.Render(Settings);
        FilmBuffer again = scene.Render(Settings);
        FilmBuffer fresh = Fresh(edit);
        FilmComparison versusFresh = FilmComparison.Compare(edited.Span, fresh.Span);
        FilmComparison versusFirst = FilmComparison.Compare(edited.Span, first.Span);
        output.WriteLine($"{edit}: edited after a render vs a new scene with the edit: {versusFresh}; vs the first render: {versusFirst}");

        //Assert
        edited.Span.SequenceEqual(fresh.Span).Should().BeTrue();
        again.Span.SequenceEqual(edited.Span).Should().BeTrue();
        edited.Span.SequenceEqual(first.Span).Should().BeFalse();
    }

    [Fact]
    public void successive_edits_and_displacement_switched_off_and_on_render_like_new_scenes()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Parts parts = Build(scene);
        FilmBuffer first = scene.Render(Settings);

        //Act
        Edit(scene, parts, "smooth");
        FilmBuffer smooth = scene.Render(Settings);
        Edit(scene, parts, "transform");
        FilmBuffer moved = scene.Render(Settings);
        Edit(scene, parts, "displacement-scale");
        FilmBuffer scaled = scene.Render(Settings);
        Edit(scene, parts, "bump");
        FilmBuffer bump = scene.Render(Settings);
        parts.Displaced.DisplacementMethod = ShaderDisplacementMethod.True;
        FilmBuffer back = scene.Render(Settings);

        //Assert
        first.Span.SequenceEqual(Fresh().Span).Should().BeTrue();
        smooth.Span.SequenceEqual(Fresh("smooth").Span).Should().BeTrue();
        moved.Span.SequenceEqual(Fresh("smooth", "transform").Span).Should().BeTrue();
        scaled.Span.SequenceEqual(Fresh("smooth", "transform", "displacement-scale").Span).Should().BeTrue();
        bump.Span.SequenceEqual(Fresh("smooth", "transform", "displacement-scale", "bump").Span).Should().BeTrue();
        back.Span.SequenceEqual(scaled.Span).Should().BeTrue();
    }

    [Fact]
    public void reads_after_a_render_return_the_authored_values_and_a_replaced_mesh_renders_like_a_new_scene()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Parts parts = Build(scene);
        scene.Render(Settings);
        float[] replaced = [.. parts.Positions.Select((value, index) => index % 3 == 2 ? value + .05f : value)];

        //Act
        float[] positions = parts.Mesh.GetAttribute("P");
        float[] weights = parts.Mesh.GetAttribute("weight");
        parts.Mesh.SetTriangles(replaced, GridIndices());
        parts.Mesh.SetAttribute("weight", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.None, parts.Weights);
        FilmBuffer edited = scene.Render(Settings);
        float[] after = parts.Mesh.GetAttribute("P");
        using CyclesScene fresh = TestScenes.CreateCpuScene();
        Parts freshParts = Build(fresh);
        freshParts.Mesh.SetTriangles(replaced, GridIndices());
        freshParts.Mesh.SetAttribute("weight", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.None, freshParts.Weights);
        FilmBuffer expected = fresh.Render(Settings);

        //Assert
        positions.Should().Equal(parts.Positions);
        weights.Should().Equal(parts.Weights);
        after.Should().Equal(replaced);
        freshParts.Mesh.GetAttribute("P").Should().Equal(replaced);
        edited.Span.SequenceEqual(expected.Span).Should().BeTrue();
    }

    internal static uint[] GridIndices()
    {
        List<uint> indices = [];
        for (int y = 0; y < Cells; y++)
            for (int x = 0; x < Cells; x++)
            {
                uint a = (uint)(y * (Cells + 1) + x), b = a + 1, c = a + Cells + 1, d = c + 1;
                indices.AddRange([a, b, c, b, d, c]);
            }
        return [.. indices];
    }
}
