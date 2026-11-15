// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Numerics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// The quaternion and spherical-harmonics attribute kinds (AttributeValue.Quaternion and .SphericalHarmonicsRest, provisional)
// through GeometryBaseNode.SetAttribute and GetAttribute and an Attribute node, on the CPU reference backend.
public class GeometryAttributeTests
{
    private const int Coefficients = 45;

    private static readonly RenderSettings Pixel = new(8, 8, 1);

    // A quad of two triangles in front of the camera: 4 vertices, 2 faces, 6 corners.
    private static readonly float[] QuadPositions = [-3, -3, 0, 3, -3, 0, 3, 3, 0, -3, 3, 0];
    private static readonly uint[] QuadIndices = [0, 1, 2, 0, 2, 3];

    // Quaternions of any length (Cycles stores them as given), w, x, y, z per element.
    private static float[] Quaternions(int count) =>
        Enumerable.Range(0, count).SelectMany(i => new[] { .9f - .1f * i, .1f + .07f * i, -.3f + .11f * i, .45f - .13f * i }).ToArray();

    // Coefficients in [-1, 1] from a fixed formula, both ends and zero included.
    private static float[] Harmonics(int count)
    {
        float[] values = new float[count * Coefficients];
        for (int i = 0; i < values.Length; i++) values[i] = MathF.Sin(1.3f * i + .7f) * 1.02f;
        for (int i = 0; i < values.Length; i++) values[i] = Math.Clamp(values[i], -1f, 1f);
        values[0] = -1f;
        values[1] = 0f;
        values[2] = 1f;
        return values;
    }

    // The value Cycles stores for a coefficient: quantize_spherical_harmonics, then unquantize_spherical_harmonics
    // (util/types_spherical_harmonics.h, with float_to_byte and byte_to_float of util/math_base.h), in binary32.
    private static float Stored(float value)
    {
        float rescaled = Math.Clamp((value + 1f) / 2f, 0f, 1f);
        byte quantized = rescaled <= 0f ? (byte)0 : rescaled > 1f - .5f / 255f ? (byte)255 : (byte)(255f * rescaled + .5f);
        return quantized * (1f / 255f) * 2f - 1f;
    }

    private static MeshNode Quad(CyclesScene scene, ShaderNode material = null) => scene.CreateMesh(QuadPositions, QuadIndices, material);

    // A plane (or a point covering the view) whose emission comes from one output of an Attribute node reading "name",
    // over a black world: a camera ray sees the emission exactly, so each pixel holds the node's output.
    internal static void BuildOutputScene(CyclesScene scene, Action<CyclesScene, ShaderNode> geometry, string name, string output,
        Vector3? missing = null, float? missingAlpha = null)
    {
        scene.Integrator.UseLightTree = false;
        scene.Integrator.SamplingPattern = IntegratorSamplingPattern.TabulatedSobol;
        scene.Camera.Fov = 1.1f;
        scene.Camera.Matrix = Transform3x4.CreateTranslation(0, 0, -4);
        ShaderNode material = scene.CreateShader();
        AttributeNode attribute = material.Add<AttributeNode>();
        attribute.Attribute = name;
        if (missing.HasValue) attribute.Missing = missing.Value;
        if (missingAlpha.HasValue) attribute.MissingAlpha = missingAlpha.Value;
        EmissionNode emission = material.Add<EmissionNode>();
        emission.Color = Vector3.One;
        emission.Strength = 1f;
        if (output == "Color") material.Connect(attribute.ColorOutput, emission.ColorInput);
        else if (output == "Vector") material.Connect(attribute.VectorOutput, emission.ColorInput);
        else material.Connect(output == "Fac" ? attribute.FacOutput : attribute.AlphaOutput, emission.StrengthInput);
        material.Connect(emission.EmissionOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        ShaderNode world = scene.CreateShader();
        BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
        background.Strength = 0f;
        world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
        scene.Background.Shader = world;
        geometry(scene, material);
    }

    private static FilmBuffer RenderOutput(Action<CyclesScene, ShaderNode> geometry, string name, string output,
        Vector3? missing = null, float? missingAlpha = null)
    {
        using CyclesScene scene = TestScenes.CreateCpuScene();
        BuildOutputScene(scene, geometry, name, output, missing, missingAlpha);
        return scene.Render(Pixel);
    }

    // The geometry of BuildOutputScene, placed by an object: the quad with the attribute on its Mesh domain, one point
    // covering the view with it on its Vertex domain, or (provisional, quaternions) one thick curve of two keys along y
    // covering the view with it on its CurveKey, Curve or Mesh domain, as many values as the domain has elements, repeated
    // from values (none when values is null).
    internal static Action<CyclesScene, ShaderNode> Place(string geometry, string name, AttributeValue value, float[] values) =>
        (scene, material) =>
        {
            GeometryBaseNode placed = geometry switch
            {
                "mesh" => Quad(scene, material),
                "points" => scene.CreatePointCloud([0f, 0f, 0f], [3f], material),
                _ => scene.CreateHair([0f, -3f, 0f, 0f, 3f, 0f], [3f, 3f], [0u], CurveShape.Thick, material),
            };
            (AttributeDomain domain, int count) = geometry switch
            {
                "mesh" => (AttributeDomain.Mesh, 1),
                "points" => (AttributeDomain.Vertex, 1),
                "curves-key" => (AttributeDomain.CurveKey, 2),
                "curves-curve" => (AttributeDomain.Curve, 1),
                _ => (AttributeDomain.Mesh, 1),
            };
            if (values != null)
                placed.SetAttribute(name, value, domain, AttributeFlags.None, [.. Enumerable.Range(0, count).SelectMany(_ => values)]);
            scene.CreateObject(placed);
        };

    // The red, green and blue of the centre pixel.
    internal static float[] Centre(FilmBuffer film)
    {
        ReadOnlySpan<float> row = film.GetRow(film.Height / 2);
        int x = film.Width / 2 * 4;
        return [row[x], row[x + 1], row[x + 2]];
    }

    [Fact]
    public void Quaternion_attribute_reads_back_exactly_on_every_mesh_domain_and_on_points()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        MeshNode mesh = Quad(scene);
        PointCloudNode points = scene.CreatePointCloud([0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f, 0f], [.1f, .2f, .3f]);
        MeshNode cage = scene.CreateMesh(QuadPositions, QuadIndices);
        cage.SetSubdivisionCage(QuadPositions, [4], [0, 1, 2, 3]);
        (GeometryBaseNode Geometry, string Name, AttributeDomain Domain, AttributeFlags Flags, int Count)[] cases =
        [
            (mesh, "q-mesh", AttributeDomain.Mesh, AttributeFlags.None, 1),
            (mesh, "q-face", AttributeDomain.Face, AttributeFlags.None, 2),
            (mesh, "q-vertex", AttributeDomain.Vertex, AttributeFlags.None, 4),
            (mesh, "q-corner", AttributeDomain.Corner, AttributeFlags.None, 6),
            (points, "q-points", AttributeDomain.Vertex, AttributeFlags.None, 3),
            (points, "q-cloud", AttributeDomain.Mesh, AttributeFlags.None, 1),
            (cage, "q-cage", AttributeDomain.Vertex, AttributeFlags.Subdivision, 4),
        ];

        //Act
        foreach (var (geometry, name, domain, flags, count) in cases)
            geometry.SetAttribute(name, AttributeValue.Quaternion, domain, flags, Quaternions(count));

        //Assert
        foreach (var (geometry, name, _, flags, count) in cases)
            geometry.GetAttribute(name, flags).Should().Equal(Quaternions(count), name);
    }

    [Fact]
    public void Spherical_harmonics_attribute_reads_back_the_stored_8_bit_value_on_meshes_and_points()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        MeshNode mesh = Quad(scene);
        PointCloudNode points = scene.CreatePointCloud([0f, 0f, 0f, 1f, 0f, 0f], [.1f, .2f]);
        (GeometryBaseNode Geometry, string Name, AttributeDomain Domain, int Count)[] cases =
        [
            (mesh, "sh-mesh", AttributeDomain.Mesh, 1),
            (mesh, "sh-face", AttributeDomain.Face, 2),
            (mesh, "sh-vertex", AttributeDomain.Vertex, 4),
            (mesh, "sh-corner", AttributeDomain.Corner, 6),
            (points, "sh-points", AttributeDomain.Vertex, 2),
        ];

        //Act
        foreach (var (geometry, name, domain, count) in cases)
            geometry.SetAttribute(name, AttributeValue.SphericalHarmonicsRest, domain, AttributeFlags.None, Harmonics(count));

        //Assert
        foreach (var (geometry, name, _, count) in cases)
        {
            float[] set = Harmonics(count);
            float[] read = geometry.GetAttribute(name);
            read.Should().Equal(set.Select(Stored).ToArray(), name);
            read.Zip(set, (r, s) => MathF.Abs(r - s)).Max().Should().BeLessThanOrEqualTo(1f / 255f + 1e-6f);
        }
        float[] ends = mesh.GetAttribute("sh-mesh");
        ends[0].Should().Be(-1f);
        ends[2].Should().Be(1f);
    }

    [Fact]
    public void Spherical_harmonics_values_outside_minus_one_to_one_are_refused_by_name_and_never_clipped()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        MeshNode mesh = Quad(scene);
        float[] valid = Harmonics(1);
        mesh.SetAttribute("sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Mesh, AttributeFlags.None, valid);
        float[] above = (float[])valid.Clone();
        above[17] = 1.0001f;
        float[] below = (float[])valid.Clone();
        below[44] = -1.5f;
        float[] notANumber = (float[])valid.Clone();
        notANumber[3] = float.NaN;

        //Act
        Action aboveOne = () => mesh.SetAttribute("sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Mesh, AttributeFlags.None, above);
        Action belowMinusOne = () => mesh.SetAttribute("sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Mesh, AttributeFlags.None, below);
        Action nonFinite = () => mesh.SetAttribute("sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Mesh, AttributeFlags.None, notANumber);

        //Assert
        foreach (Action refused in new[] { aboveOne, belowMinusOne })
        {
            NativeFailure error = refused.Should().Throw<NativeFailure>().Which;
            error.NativeStatus.Should().Be(NativeStatus.Invalid);
            error.Message.Should().Contain("Spherical-harmonics coefficients must lie in [-1, 1]");
        }
        nonFinite.Should().Throw<NativeFailure>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
        mesh.GetAttribute("sh").Should().Equal(valid.Select(Stored).ToArray());
    }

    [Fact]
    public void Spherical_harmonics_on_curves_and_attributes_with_wrong_domains_or_counts_are_refused_by_name()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        MeshNode mesh = Quad(scene);
        HairNode hair = scene.CreateHair([0f, 0f, 0f, 0f, 1f, 0f], [.1f, .1f], [0u]);
        PointCloudNode points = scene.CreatePointCloud([0f, 0f, 0f], [.1f]);
        MeshNode cage = scene.CreateMesh(QuadPositions, QuadIndices);
        cage.SetSubdivisionCage(QuadPositions, [4], [0, 1, 2, 3]);

        //Act
        Action harmonicsOnHair = () => hair.SetAttribute("sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Curve, AttributeFlags.None, Harmonics(1));
        Action harmonicsOnKeys = () => hair.SetAttribute("sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.CurveKey, AttributeFlags.None, Harmonics(2));
        Action keyNormalDomain = () => hair.SetAttribute("q", AttributeValue.Quaternion, AttributeDomain.CurveKeyNormal, AttributeFlags.None, Quaternions(2));
        Action shortOnHair = () => hair.SetAttribute("q", AttributeValue.Quaternion, AttributeDomain.CurveKey, AttributeFlags.None, Quaternions(1));
        Action curveOnPoints = () => points.SetAttribute("q", AttributeValue.Quaternion, AttributeDomain.Curve, AttributeFlags.None, Quaternions(1));
        Action subdivided = () => cage.SetAttribute("sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Vertex, AttributeFlags.Subdivision, Harmonics(4));
        Action shortQuaternion = () => mesh.SetAttribute("q", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.None, Quaternions(4)[..15]);
        Action shortHarmonics = () => mesh.SetAttribute("sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Face, AttributeFlags.None, Harmonics(2)[..89]);
        Action normalDomain = () => mesh.SetAttribute("q", AttributeValue.Quaternion, AttributeDomain.CornerNormal, AttributeFlags.None, Quaternions(6));
        Action standardName = () => mesh.SetAttribute("uv", AttributeValue.Quaternion, AttributeDomain.Corner, AttributeFlags.Standard, Quaternions(6));

        //Assert
        foreach (Action unsupported in new[] { harmonicsOnHair, harmonicsOnKeys })
        {
            NativeFailure error = unsupported.Should().Throw<NativeFailure>().Which;
            error.NativeStatus.Should().Be(NativeStatus.Unsupported);
            error.Message.Should().Contain("spherical-harmonics attributes are accepted on meshes and point clouds only (the pinned kernels read them only for Gaussian splats");
        }
        NativeFailure subdivisionError = subdivided.Should().Throw<NativeFailure>().Which;
        subdivisionError.NativeStatus.Should().Be(NativeStatus.Unsupported);
        subdivisionError.Message.Should().Contain("spherical-harmonics attributes cannot be subdivision attributes");
        foreach (Action invalid in new[] { shortQuaternion, shortHarmonics, normalDomain, standardName, keyNormalDomain, shortOnHair, curveOnPoints })
            invalid.Should().Throw<NativeFailure>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
    }

    // Provisional (decision 23): quaternions on the curve domains the pinned kernels read attributes from, CurveKey,
    // Curve and Mesh, read back exactly; their values need not be of unit length.
    [Fact]
    public void Quaternion_attributes_on_the_curve_key_curve_and_mesh_domains_of_hair_read_back_exactly()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        HairNode hair = scene.CreateHair([0f, 0f, 0f, 0f, 1f, 0f, 1f, 0f, 0f, 1f, 1f, 0f, 1f, 2f, 0f], [.1f, .1f, .1f, .1f, .1f], [0u, 2u]);

        //Act
        hair.SetAttribute("per-key", AttributeValue.Quaternion, AttributeDomain.CurveKey, AttributeFlags.None, Quaternions(5));
        hair.SetAttribute("per-curve", AttributeValue.Quaternion, AttributeDomain.Curve, AttributeFlags.None, Quaternions(2));
        hair.SetAttribute("whole", AttributeValue.Quaternion, AttributeDomain.Mesh, AttributeFlags.None, Quaternions(1));

        //Assert
        hair.GetAttribute("per-key").Should().Equal(Quaternions(5));
        hair.GetAttribute("per-curve").Should().Equal(Quaternions(2));
        hair.GetAttribute("whole").Should().Equal(Quaternions(1));
        TestScenes.RequireNativeHost();
        scene.Render(Pixel).Span.ToArray().All(float.IsFinite).Should().BeTrue();
    }

    // Provisional (decision 23): a quaternion on the keys of a curve is mixed along the segment one component at a time,
    // unnormalized (curve_attribute, kernel/geom/curve.h): the centre of a vertical two-key curve, halfway along it, reads
    // the average of its two keys.
    [Fact]
    public void An_attribute_node_mixes_a_quaternion_between_the_keys_of_a_curve_one_component_at_a_time()
    {
        //Arrange
        float[] first = [.9f, .1f, .2f, .3f], second = [.1f, .7f, .6f, .5f];
        Action<CyclesScene, ShaderNode> placed = (scene, material) =>
        {
            HairNode hair = scene.CreateHair([0f, -3f, 0f, 0f, 3f, 0f], [3f, 3f], [0u], CurveShape.Thick, material);
            hair.SetAttribute("q", AttributeValue.Quaternion, AttributeDomain.CurveKey, AttributeFlags.None, [.. first, .. second]);
            scene.CreateObject(hair);
        };

        //Act
        float[] color = Centre(RenderOutput(placed, "q", "Color"));
        float[] alpha = Centre(RenderOutput(placed, "q", "Alpha"));

        //Assert
        for (int c = 0; c < 3; c++) color[c].Should().BeApproximately((first[c] + second[c]) / 2f, .03f);
        alpha[0].Should().BeApproximately((first[3] + second[3]) / 2f, .03f);
        color[0].Should().NotBe(first[0]);
    }

    [Theory]
    [InlineData("mesh")]
    [InlineData("points")]
    [InlineData("curves-key")]
    [InlineData("curves-curve")]
    [InlineData("curves-mesh")]
    public void An_attribute_node_reads_a_quaternion_as_w_x_y_with_their_average_as_fac_and_z_as_alpha(string geometry)
    {
        //Arrange
        float[] q = [.625f, .25f, .375f, .8125f];
        Action<CyclesScene, ShaderNode> placed = Place(geometry, "q", AttributeValue.Quaternion, q);

        //Act
        float[] color = Centre(RenderOutput(placed, "q", "Color"));
        float[] vector = Centre(RenderOutput(placed, "q", "Vector"));
        float[] fac = Centre(RenderOutput(placed, "q", "Fac"));
        float[] alpha = Centre(RenderOutput(placed, "q", "Alpha"));

        //Assert
        color.Should().Equal(q[0], q[1], q[2]);
        vector.Should().Equal(q[0], q[1], q[2]);
        fac[0].Should().BeApproximately((q[0] + q[1] + q[2]) / 3f, 1e-6f);
        alpha.Should().Equal(q[3], q[3], q[3]);
    }

    [Theory]
    [InlineData("mesh")]
    [InlineData("points")]
    public void An_attribute_node_finds_spherical_harmonics_and_reads_black_with_alpha_one_and_a_missing_one_reads_its_missing_inputs(string geometry)
    {
        //Arrange
        Vector3 missing = new(.625f, .125f, .5f);
        const float missingAlpha = .25f;
        Action<CyclesScene, ShaderNode> present = Place(geometry, "sh", AttributeValue.SphericalHarmonicsRest, Harmonics(1));
        Action<CyclesScene, ShaderNode> absent = Place(geometry, "sh", AttributeValue.SphericalHarmonicsRest, null);

        //Act
        float[] foundColor = Centre(RenderOutput(present, "sh", "Color", missing, missingAlpha));
        float[] foundAlpha = Centre(RenderOutput(present, "sh", "Alpha", missing, missingAlpha));
        float[] missingColor = Centre(RenderOutput(absent, "sh", "Color", missing, missingAlpha));
        float[] missingAlphaOutput = Centre(RenderOutput(absent, "sh", "Alpha", missing, missingAlpha));

        //Assert
        foundColor.Should().Equal(0f, 0f, 0f);
        foundAlpha.Should().Equal(1f, 1f, 1f);
        missingColor.Should().Equal(missing.X, missing.Y, missing.Z);
        missingAlphaOutput.Should().Equal(missingAlpha, missingAlpha, missingAlpha);
    }

    [Fact]
    public void Quaternion_and_spherical_harmonics_attributes_set_after_a_render_match_a_scene_built_with_them()
    {
        //Arrange
        RenderSettings settings = new(16, 12, 4);
        float[] before = Quaternions(4);
        float[] after = before.Select(v => v * .5f + .25f).ToArray();
        float[] harmonics = Harmonics(4);
        MeshNode Build(CyclesScene scene, float[] quaternions)
        {
            MeshNode quad = null;
            BuildOutputScene(scene, (s, material) => scene.CreateObject(quad = Quad(s, material)), "q", "Color");
            quad.SetAttribute("q", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.None, quaternions);
            quad.SetAttribute("sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Vertex, AttributeFlags.None, harmonics);
            return quad;
        }
        using CyclesScene edited = TestScenes.CreateCpuScene();
        using CyclesScene fresh = TestScenes.CreateCpuScene();
        MeshNode late = Build(edited, before);
        Build(fresh, after);
        FilmBuffer first = edited.Render(settings);

        //Act
        float[] readAfterRender = late.GetAttribute("q");
        late.SetAttribute("q", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.None, after);
        FilmBuffer editedFilm = edited.Render(settings);
        FilmBuffer freshFilm = fresh.Render(settings);

        //Assert
        readAfterRender.Should().Equal(before);
        late.GetAttribute("q").Should().Equal(after);
        late.GetAttribute("sh").Should().Equal(harmonics.Select(Stored).ToArray());
        first.Span.SequenceEqual(editedFilm.Span).Should().BeFalse();
        editedFilm.Span.SequenceEqual(freshFilm.Span).Should().BeTrue();
    }
}
