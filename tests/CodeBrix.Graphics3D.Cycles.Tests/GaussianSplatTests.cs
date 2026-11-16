// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Point clouds rendered as Gaussian splats (PointCloudRenderAs.GaussianSplats) with the standard point attributes
// radiance:base, scale and rotation, and optionally radiance:sh (provisional), on the CPU backends.
public class GaussianSplatTests
{
    private const int Count = 24;

    private static readonly RenderSettings Small = new(32, 24, 4);

    // A ball of splats in front of the lit triangle of TestScenes.BuildTyped.
    private static float[] Positions() => Enumerable.Range(0, Count).SelectMany(i =>
    {
        float t = (i + .5f) / Count, height = 1f - 2f * t, ring = MathF.Sqrt(1f - height * height), phase = .3f + 2.4f * i;
        return new[] { .9f * ring * MathF.Cos(phase), .9f * height, -1f + .5f * ring * MathF.Sin(phase) };
    }).ToArray();

    // The authored radii, far from the bounding radii Cycles computes for these splats.
    private static float[] Radii() => Enumerable.Range(0, Count).Select(i => .02f + .01f * (i % 5)).ToArray();

    // Band-0 colour coefficients and an opacity per splat.
    private static float[] RadianceBase() => Enumerable.Range(0, Count).SelectMany(i =>
        new[] { -1.2f + .1f * i, 1.4f - .11f * i, .3f * MathF.Sin(i), .35f + .025f * i }).ToArray();

    private static float[] Scales(float factor = 1f) => Enumerable.Range(0, Count).SelectMany(i =>
        new[] { factor * (.12f + .01f * i), factor * (.08f + .005f * (i % 7)), factor * .03f }).ToArray();

    // Rotations from an axis and an angle, w, x, y, z, some longer or shorter than 1 (Cycles normalizes them).
    private static float[] Rotations() => Enumerable.Range(0, Count).SelectMany(i =>
    {
        float angle = .4f + .23f * i, length = .7f + .05f * (i % 9);
        float x = .3f + .02f * i, y = .8f - .03f * i, z = .5f, norm = MathF.Sqrt(x * x + y * y + z * z), s = MathF.Sin(angle / 2) / norm;
        return new[] { length * MathF.Cos(angle / 2), length * s * x, length * s * y, length * s * z };
    }).ToArray();

    private static float[] Harmonics() => Enumerable.Range(0, Count * 45).Select(k => .25f * MathF.Sin(.37f * k)).ToArray();

    // The lit triangle scene with a splat cloud in front of it, Cycles' default splat shader unless material is given; the
    // standard attribute named by skip is left out.
    private static PointCloudNode BuildSplats(CyclesScene scene, string skip = null, ShaderNode material = null, bool harmonics = false)
    {
        TestScenes.BuildTyped(scene);
        PointCloudNode splats = scene.CreatePointCloud(Positions(), Radii(), material);
        splats.RenderAs = PointCloudRenderAs.GaussianSplats;
        if (skip != "radiance:base") splats.SetAttribute("radiance:base", AttributeValue.Float4, AttributeDomain.Vertex, AttributeFlags.Standard, RadianceBase());
        if (skip != "scale") splats.SetAttribute("scale", AttributeValue.Vector, AttributeDomain.Vertex, AttributeFlags.Standard, Scales());
        if (skip != "rotation") splats.SetAttribute("rotation", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.Standard, Rotations());
        if (harmonics) splats.SetAttribute("radiance:sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Vertex, AttributeFlags.Standard, Harmonics());
        return splats;
    }

    private static FilmBuffer Render(Action<CyclesScene> build, CyclesBackend backend = CyclesBackend.CpuReference)
    {
        TestScenes.RequireNativeHost();
        using CyclesScene scene = new(backend);
        build(scene);
        return scene.Render(Small);
    }

    [Fact]
    public void A_splat_cloud_renders_on_the_cpu_backends_and_repeats_bit_for_bit()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        PointCloudNode splats = BuildSplats(scene);
        scene.CreateObject(splats);

        //Act
        FilmBuffer first = scene.Render(Small);
        FilmBuffer second = scene.Render(Small);
        FilmBuffer withHarmonics = Render(s => s.CreateObject(BuildSplats(s, harmonics: true)));
        FilmBuffer asPoints = Render(s => { PointCloudNode p = BuildSplats(s); p.RenderAs = PointCloudRenderAs.Points; s.CreateObject(p); });
        FilmBuffer withoutCloud = Render(s => TestScenes.BuildTyped(s));
        FilmBuffer embree = Render(s => s.CreateObject(BuildSplats(s)), CyclesBackend.Cpu);

        //Assert
        first.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        first.Span.SequenceEqual(second.Span).Should().BeTrue();
        first.Span.SequenceEqual(withHarmonics.Span).Should().BeFalse();
        first.Span.SequenceEqual(asPoints.Span).Should().BeFalse();
        first.Span.SequenceEqual(withoutCloud.Span).Should().BeFalse();
        embree.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        embree.Span.SequenceEqual(withoutCloud.Span).Should().BeFalse();
    }

    [Theory]
    [InlineData("radiance:base")]
    [InlineData("scale")]
    [InlineData("rotation")]
    public void A_splat_cloud_without_one_of_its_standard_attributes_is_refused_naming_it(string missing)
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        PointCloudNode splats = BuildSplats(scene, skip: missing);
        scene.CreateObject(splats);

        //Act
        Action render = () => scene.Render(Small);

        //Assert
        CyclesException error = render.Should().Throw<CyclesException>().Which;
        error.NativeStatus.Should().Be(NativeStatus.Invalid);
        error.Message.Should().Contain("Gaussian splats need the standard point attributes radiance:base, scale and rotation");
        error.Message.Should().Contain("(missing: " + missing + ")");
    }

    [Fact]
    public void Splat_attributes_set_without_the_standard_flag_are_custom_attributes_and_the_cloud_is_refused()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        PointCloudNode splats = scene.CreatePointCloud(Positions(), Radii());
        splats.RenderAs = PointCloudRenderAs.GaussianSplats;
        splats.SetAttribute("radiance:base", AttributeValue.Float4, AttributeDomain.Vertex, AttributeFlags.None, RadianceBase());
        splats.SetAttribute("scale", AttributeValue.Vector, AttributeDomain.Vertex, AttributeFlags.None, Scales());
        splats.SetAttribute("rotation", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.None, Rotations());
        scene.CreateObject(splats);

        //Act
        Action render = () => scene.Render(Small);

        //Assert
        CyclesException error = render.Should().Throw<CyclesException>().Which;
        error.NativeStatus.Should().Be(NativeStatus.Invalid);
        error.Message.Should().Contain("set with CBC_ATTR_STANDARD (missing: radiance:base, scale, rotation)");
    }

    [Theory]
    [InlineData(5, 0f, 0f, "has zero length")]
    [InlineData(11, 1e-20f, 1e-20f, "has zero length")]
    [InlineData(17, 3e19f, 0f, "is too long")]
    public void A_splat_rotation_whose_squared_length_is_not_a_normal_binary32_number_is_refused_by_name(int point, float w, float xyz, string words)
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        PointCloudNode splats = BuildSplats(scene);
        scene.CreateObject(splats);
        float[] rotations = Rotations();
        rotations[4 * point] = w;
        rotations[4 * point + 1] = rotations[4 * point + 2] = rotations[4 * point + 3] = xyz;
        splats.SetAttribute("rotation", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.Standard, rotations);

        //Act
        Action render = () => scene.Render(Small);
        Action fixedRender = () =>
        {
            splats.SetAttribute("rotation", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.Standard, Rotations());
            scene.Render(Small);
        };

        //Assert
        CyclesException error = render.Should().Throw<CyclesException>().Which;
        error.NativeStatus.Should().Be(NativeStatus.Invalid);
        error.Message.Should().Contain("Gaussian splat rotation of point " + point + " " + words);
        fixedRender.Should().NotThrow();
    }

    [Fact]
    public void The_authored_radius_reads_back_after_a_render_and_deformation_motion_takes_it_as_the_center_step()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        PointCloudNode splats = BuildSplats(scene);
        scene.CreateObject(splats);
        float[] positions = Positions(), radii = Radii();
        float[] steps = [.. positions.Select(v => v - .05f), .. positions, .. positions.Select(v => v + .05f)];
        float[] stepRadii = [.. radii, .. radii, .. radii];
        scene.Render(Small);

        //Act
        float[] afterRender = splats.GetAttribute("radius");
        float[] standardAfterRender = splats.GetAttribute("radius", AttributeFlags.Standard);
        Action motion = () => splats.SetMotion(3, steps, stepRadii);
        scene.Integrator.MotionBlur = true;
        scene.Camera.Shuttertime = .5f;
        Func<FilmBuffer> blurred = () => scene.Render(Small);

        //Assert
        afterRender.Should().Equal(radii);
        standardAfterRender.Should().Equal(radii);
        motion.Should().NotThrow();
        blurred().Span.ToArray().All(float.IsFinite).Should().BeTrue();
        splats.GetAttribute("radius").Should().Equal(radii);
        splats.GetAttribute("P").Should().Equal(positions);
    }

    [Fact]
    public void A_splat_cloud_switched_to_points_after_a_render_renders_like_points_with_the_authored_radius()
    {
        //Arrange
        using CyclesScene edited = TestScenes.CreateCpuScene();
        PointCloudNode splats = BuildSplats(edited);
        edited.CreateObject(splats);
        FilmBuffer asSplats = edited.Render(Small);

        //Act
        splats.RenderAs = PointCloudRenderAs.Points;
        FilmBuffer switched = edited.Render(Small);
        FilmBuffer fresh = Render(s => { PointCloudNode p = BuildSplats(s); p.RenderAs = PointCloudRenderAs.Points; s.CreateObject(p); });

        //Assert
        asSplats.Span.SequenceEqual(switched.Span).Should().BeFalse();
        switched.Span.SequenceEqual(fresh.Span).Should().BeTrue();
        splats.GetAttribute("radius").Should().Equal(Radii());
    }

    [Fact]
    public void Edits_of_a_splat_cloud_after_a_render_match_a_scene_built_with_them()
    {
        //Arrange
        Transform3x4 moved = new(.8f, 0f, .6f, .2f, 0f, 1f, 0f, -.1f, -.6f, 0f, .8f, .3f);
        using CyclesScene edited = TestScenes.CreateCpuScene();
        PointCloudNode splats = BuildSplats(edited, harmonics: true);
        ObjectNode placed = edited.CreateObject(splats);
        FilmBuffer before = edited.Render(Small);

        //Act
        splats.SetAttribute("scale", AttributeValue.Vector, AttributeDomain.Vertex, AttributeFlags.Standard, Scales(1.5f));
        placed.Tfm = moved;
        FilmBuffer after = edited.Render(Small);
        FilmBuffer fresh = Render(s =>
        {
            PointCloudNode p = BuildSplats(s, harmonics: true);
            p.SetAttribute("scale", AttributeValue.Vector, AttributeDomain.Vertex, AttributeFlags.Standard, Scales(1.5f));
            s.CreateObject(p, moved);
        });

        //Assert
        before.Span.SequenceEqual(after.Span).Should().BeFalse();
        after.Span.SequenceEqual(fresh.Span).Should().BeTrue();
    }

    // Cycles bakes a single-user point cloud under a uniform scale into world space (the static BVH), but never a splat
    // cloud: its scale, rotation and spherical harmonics stay in object space. A second user of the geometry that no ray
    // sees keeps any cloud instanced, so the single-user splat film equals the film with that second user, while the same
    // cloud rendered as plain points is baked and its film differs from the instanced one.
    [Fact]
    public void The_static_transform_is_never_applied_to_a_splat_cloud()
    {
        //Arrange
        Transform3x4 uniform = new(.96f, 0f, .72f, .3f, 0f, 1.2f, 0f, -.2f, -.72f, 0f, .96f, .4f);
        Action<CyclesScene> Build(PointCloudRenderAs renderAs, bool secondUser) => s =>
        {
            PointCloudNode p = BuildSplats(s, harmonics: true);
            p.RenderAs = renderAs;
            s.CreateObject(p, uniform);
            if (!secondUser) return;
            ObjectNode unseen = s.CreateObject(p, Transform3x4.CreateTranslation(0f, 0f, 40f));
            unseen.RayVisibility = RayVisibility.None;
        };

        //Act
        FilmBuffer singleUser = Render(Build(PointCloudRenderAs.GaussianSplats, false));
        FilmBuffer instanced = Render(Build(PointCloudRenderAs.GaussianSplats, true));
        FilmBuffer pointsSingleUser = Render(Build(PointCloudRenderAs.Points, false));
        FilmBuffer pointsInstanced = Render(Build(PointCloudRenderAs.Points, true));

        //Assert
        singleUser.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        singleUser.Span.SequenceEqual(instanced.Span).Should().BeTrue();
        pointsSingleUser.Span.SequenceEqual(pointsInstanced.Span).Should().BeFalse();
    }

    [Fact]
    public void Splat_attributes_are_standard_point_attributes_with_their_own_value_kinds()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        PointCloudNode splats = scene.CreatePointCloud(Positions(), Radii());
        MeshNode mesh = scene.CreateMesh(TestScenes.TrianglePositions, TestScenes.TriangleIndices);
        float[] harmonics = Harmonics();

        //Act
        splats.SetAttribute("radiance:base", AttributeValue.Float4, AttributeDomain.Vertex, AttributeFlags.Standard, RadianceBase());
        splats.SetAttribute("scale", AttributeValue.Vector, AttributeDomain.Vertex, AttributeFlags.Standard, Scales());
        splats.SetAttribute("rotation", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.Standard, Rotations());
        splats.SetAttribute("radiance:sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Vertex, AttributeFlags.Standard, harmonics);
        Action onMesh = () => mesh.SetAttribute("rotation", AttributeValue.Quaternion, AttributeDomain.Vertex, AttributeFlags.Standard, new float[12]);
        Action radiance = () => splats.SetAttribute("radiance", AttributeValue.Float4, AttributeDomain.Vertex, AttributeFlags.Standard, RadianceBase());
        Action[] wrongKinds =
        [
            () => splats.SetAttribute("radiance:base", AttributeValue.Color, AttributeDomain.Vertex, AttributeFlags.Standard, Scales()),
            () => splats.SetAttribute("scale", AttributeValue.Point, AttributeDomain.Vertex, AttributeFlags.Standard, Scales()),
            () => splats.SetAttribute("rotation", AttributeValue.Float4, AttributeDomain.Vertex, AttributeFlags.Standard, Rotations()),
            () => splats.SetAttribute("radiance:sh", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.Standard, new float[Count]),
            () => splats.SetAttribute("scale", AttributeValue.Vector, AttributeDomain.Mesh, AttributeFlags.Standard, Scales()[..3]),
        ];

        //Assert
        splats.GetAttribute("radiance:base", AttributeFlags.Standard).Should().Equal(RadianceBase());
        splats.GetAttribute("scale", AttributeFlags.Standard).Should().Equal(Scales());
        splats.GetAttribute("rotation", AttributeFlags.Standard).Should().Equal(Rotations());
        splats.GetAttribute("radiance:sh", AttributeFlags.Standard).Zip(harmonics, (read, set) => MathF.Abs(read - set)).Max()
            .Should().BeLessThanOrEqualTo(1f / 255f + 1e-6f);
        foreach (Action unsupported in new[] { onMesh, radiance })
        {
            NativeFailure error = unsupported.Should().Throw<NativeFailure>().Which;
            error.NativeStatus.Should().Be(NativeStatus.Unsupported);
            error.Message.Should().Contain("Standard attribute is not exposed for this geometry");
        }
        foreach (Action invalid in wrongKinds)
            invalid.Should().Throw<NativeFailure>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
        splats.GetAttribute("scale", AttributeFlags.Standard).Should().Equal(Scales());
    }

    // The bounding radius Cycles computes for a splat at its device update (PointCloud::update_gsplat_radii): 0 below an
    // opacity of 0.01, else the largest component of |sqrt(2 ln(opacity / 0.01)) * scale|.
    internal static float[] BoundingRadii(float[] radianceBase, float[] scales) => Enumerable.Range(0, radianceBase.Length / 4).Select(i =>
    {
        float opacity = radianceBase[4 * i + 3];
        if (opacity < .01f) return 0f;
        float factor = MathF.Sqrt(2f * MathF.Log(opacity / .01f));
        return MathF.Max(MathF.Max(MathF.Abs(factor * scales[3 * i]), MathF.Abs(factor * scales[3 * i + 1])), MathF.Abs(factor * scales[3 * i + 2]));
    }).ToArray();

    // Provisional (AttributeFlags.SplatBoundingRadius): the radius Cycles computed for each splat at the last render, read
    // beside the authored one; not ready before the first render and after an edit of the cloud, until a render computes
    // it again; a splat whose opacity is below 0.01 bounds nothing (0).
    [Fact]
    public void The_bounding_radius_cycles_computed_at_the_last_render_reads_with_its_flag_and_is_not_ready_before()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        PointCloudNode splats = BuildSplats(scene);
        scene.CreateObject(splats);
        float[] faint = RadianceBase();
        faint[4 * 3 + 3] = .005f;
        Func<float[]> bounds = () => splats.GetAttribute("radius", AttributeFlags.SplatBoundingRadius);
        Func<float[]> standardBounds = () => splats.GetAttribute("radius", AttributeFlags.Standard | AttributeFlags.SplatBoundingRadius);
        void NotReady()
        {
            NativeFailure error = bounds.Should().Throw<NativeFailure>().Which;
            error.NativeStatus.Should().Be(NativeStatus.NotReady);
            error.Message.Should().Contain("is read after a render or bake has computed it");
        }

        //Act / Assert
        NotReady();
        scene.Render(Small);
        float[] computed = bounds();
        computed.Should().Equal(BoundingRadii(RadianceBase(), Scales()));
        standardBounds().Should().Equal(computed);
        computed.Should().NotEqual(Radii());
        splats.GetAttribute("radius").Should().Equal(Radii());
        scene.Render(Small);
        bounds().Should().Equal(computed);

        splats.SetAttribute("scale", AttributeValue.Vector, AttributeDomain.Vertex, AttributeFlags.Standard, Scales(1.5f));
        NotReady();
        splats.GetAttribute("radius").Should().Equal(Radii());
        scene.Render(Small);
        bounds().Should().Equal(BoundingRadii(RadianceBase(), Scales(1.5f)));

        splats.SetAttribute("radiance:base", AttributeValue.Float4, AttributeDomain.Vertex, AttributeFlags.Standard, faint);
        NotReady();
        scene.Render(Small);
        float[] withFaint = bounds();
        withFaint.Should().Equal(BoundingRadii(faint, Scales(1.5f)));
        withFaint[3].Should().Be(0f);
        splats.GetAttribute("radius").Should().Equal(Radii());
    }

    // A refused attribute call is no edit (provisional): SetAttribute, SetAttributeBytes and RemoveAttribute check every
    // argument before the host prepares the edit (which writes the authored radius back and makes the bounding radius not
    // ready), so after each refusal the bounding radius still reads Cycles' value, the authored radius reads back, and the
    // next render is bit for bit the one before; an accepted removal is an edit.
    [Fact]
    public void Refused_attribute_calls_change_nothing_and_the_bounding_radius_stays_readable()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        PointCloudNode splats = BuildSplats(scene);
        splats.SetAttribute("weight", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.None, new float[Count]);
        scene.CreateObject(splats);
        FilmBuffer first = scene.Render(Small);
        float[] computed = splats.GetAttribute("radius", AttributeFlags.SplatBoundingRadius);
        float[] outOfRange = Harmonics();
        outOfRange[7] = 1.5f;
        (Action Call, NativeStatus Status, string Words)[] refused =
        [
            (() => splats.SetAttribute("scale", AttributeValue.Vector, AttributeDomain.Vertex, AttributeFlags.Standard, new float[3 * Count - 3]),
                NativeStatus.Invalid, "Attribute count does not match geometry"),
            (() => splats.SetAttribute("radius", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.None, Radii()),
                NativeStatus.Invalid, "Use standard mode to replace a standard attribute"),
            (() => splats.SetAttribute("weight", AttributeValue.Float, AttributeDomain.Corner, AttributeFlags.None, new float[Count]),
                NativeStatus.Invalid, "Corner attributes require a mesh"),
            (() => splats.SetAttribute("radiance:sh", AttributeValue.SphericalHarmonicsRest, AttributeDomain.Vertex, AttributeFlags.Standard, outOfRange),
                NativeStatus.Invalid, "Spherical-harmonics coefficients must lie in [-1, 1]"),
            (() => splats.SetAttribute("radius", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.Standard, [.. Radii().Select(r => float.NaN)]),
                NativeStatus.Invalid, "Attribute contains a nonfinite value"),
            (() => splats.SetAttributeBytes("tint", AttributeDomain.CornerByte, AttributeFlags.None, new byte[4 * Count]),
                NativeStatus.Invalid, "Byte corner colors require a mesh"),
            (() => splats.RemoveAttribute("radius", AttributeFlags.Standard), NativeStatus.Invalid, "Cannot remove required geometry storage"),
            (() => splats.RemoveAttribute("missing"), NativeStatus.Invalid, "Unknown attribute"),
            (() => splats.RemoveAttribute("weight", AttributeFlags.Standard), NativeStatus.Invalid, "Attribute is not standard"),
            (() => scene.Host.RemoveAttribute(splats.Handle, "weight", AttributeFlags.SplatBoundingRadius), NativeStatus.Invalid, "Unknown attribute flags"),
        ];

        //Act / Assert
        foreach ((Action call, NativeStatus status, string words) in refused)
        {
            NativeFailure error = call.Should().Throw<NativeFailure>().Which;
            error.NativeStatus.Should().Be(status, words);
            error.Message.Should().Contain(words);
            splats.GetAttribute("radius", AttributeFlags.SplatBoundingRadius).Should().Equal(computed, words);
            splats.GetAttribute("radius").Should().Equal(Radii(), words);
        }
        scene.Render(Small).Span.SequenceEqual(first.Span).Should().BeTrue();
        splats.GetAttribute("radius", AttributeFlags.SplatBoundingRadius).Should().Equal(computed);
        splats.RemoveAttribute("weight");
        NativeFailure edited = ((Func<float[]>)(() => splats.GetAttribute("radius", AttributeFlags.SplatBoundingRadius))).Should().Throw<NativeFailure>().Which;
        edited.NativeStatus.Should().Be(NativeStatus.NotReady);
    }

    // The flag reads a splat cloud's radius only: any other geometry, name or flag is refused by name, through the facade
    // and through CyclesHost, and a cloud switched to points after a render is no longer a splat cloud.
    [Fact]
    public void The_splat_bounding_radius_flag_is_refused_by_name_for_anything_but_the_radius_of_a_splat_cloud()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        PointCloudNode splats = BuildSplats(scene);
        scene.CreateObject(splats);
        PointCloudNode points = scene.CreatePointCloud(Positions(), Radii());
        MeshNode mesh = scene.CreateMesh(TestScenes.TrianglePositions, TestScenes.TriangleIndices);
        scene.Render(Small);
        (Action Read, string Words)[] refused =
        [
            (() => points.GetAttribute("radius", AttributeFlags.SplatBoundingRadius), "reads the bounding radius of a Gaussian splat cloud only"),
            (() => mesh.GetAttribute("P", AttributeFlags.SplatBoundingRadius), "reads the standard attribute radius only"),
            (() => splats.GetAttribute("scale", AttributeFlags.SplatBoundingRadius), "reads the standard attribute radius only"),
            (() => splats.GetAttribute("radius", AttributeFlags.Subdivision | AttributeFlags.SplatBoundingRadius), "not a subdivision attribute"),
            (() => splats.GetAttribute("radius", (AttributeFlags)8 | AttributeFlags.SplatBoundingRadius), "Unknown attribute flags"),
            (() => scene.Host.GetAttributeBytes(splats.Handle, "radius", AttributeFlags.SplatBoundingRadius, Span<byte>.Empty, out _), "Unknown attribute flags"),
            (() => scene.Host.RemoveAttribute(splats.Handle, "radius", AttributeFlags.SplatBoundingRadius), "Unknown attribute flags"),
        ];

        //Act / Assert
        scene.Host.GetAttribute(splats.Handle, "radius", AttributeFlags.SplatBoundingRadius, Span<float>.Empty, out ulong required).Should().BeFalse();
        required.Should().Be((ulong)Count);
        foreach ((Action read, string words) in refused)
        {
            NativeFailure error = read.Should().Throw<NativeFailure>().Which;
            error.NativeStatus.Should().Be(NativeStatus.Invalid);
            error.Message.Should().Contain(words);
        }
        splats.RenderAs = PointCloudRenderAs.Points;
        Action switched = () => splats.GetAttribute("radius", AttributeFlags.SplatBoundingRadius);
        switched.Should().Throw<NativeFailure>().Which.Message.Should().Contain("reads the bounding radius of a Gaussian splat cloud only");
        splats.GetAttribute("radius").Should().Equal(Radii());
    }

    // A caller shader reads the splat's radiance through an Attribute node named "radiance": with no attribute of that
    // name, Cycles evaluates it from the coefficients, as its default splat shader does (an emission of strength 1).
    [Fact]
    public void A_caller_shader_reading_the_radiance_renders_like_the_default_splat_shader()
    {
        //Arrange
        static ShaderNode Emission(CyclesScene scene)
        {
            ShaderNode material = scene.CreateShader();
            AttributeNode radiance = material.Add<AttributeNode>();
            radiance.Attribute = "radiance";
            EmissionNode emission = material.Add<EmissionNode>();
            emission.Strength = 1f;
            material.Connect(radiance.ColorOutput, emission.ColorInput);
            material.Connect(emission.EmissionOutput, material.Output.SurfaceInput);
            return material;
        }

        //Act
        FilmBuffer byDefault = Render(s => s.CreateObject(BuildSplats(s, harmonics: true)));
        FilmBuffer byCaller = Render(s => s.CreateObject(BuildSplats(s, material: Emission(s), harmonics: true)));

        //Assert
        byCaller.Span.SequenceEqual(byDefault.Span).Should().BeTrue();
    }
}
