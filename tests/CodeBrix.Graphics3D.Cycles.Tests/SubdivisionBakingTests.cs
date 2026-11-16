// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Numerics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Lights;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Baking a subdivision surface (provisional, native/host/ABI.md "Baking"): a linear subdivision cage is baked through the
// triangles Cycles dices from it, its texels found through the diced mesh's UV attribute. On the CPU reference backend each
// bake is bit for bit the bake of an ordinary mesh built from the same tessellation (read back from the subdivided mesh after a
// bake: triangles, positions, vertex normals, UVs and face flags) and kept in object space as Cycles keeps every subdivision
// surface (an invisible second object uses it); with true displacement too, the ordinary mesh built from the undisplaced
// tessellation and displaced by Cycles in the same way. Pixel-space dicing and Catmull-Clark are refused by name; edits of the
// cage or of its subdivision attributes after a render or bake bake and render like a new scene.
public class SubdivisionBakingTests
{
    internal const int Width = 48, Height = 32;
    internal static readonly BakeType[] Types = [BakeType.Combined, BakeType.DiffuseColor, BakeType.Normal, BakeType.Position];

    // A tilted quad and a pentagon beside it (the cage of subdivision/linear-object-attribute): neither is planar, so each
    // patch is curved, and Cycles splits the pentagon into five quads around its centre.
    private static readonly float[] Cage = [-2.6f, -1.4f, .6f, -.4f, -1.6f, .2f, -.3f, 1.2f, -.4f, -2.5f, 1.3f, 0f,
        .4f, -1.3f, 0f, 2.4f, -1.5f, .5f, 2.7f, .4f, .2f, 1.6f, 1.6f, -.3f, .3f, .6f, .1f];
    private static readonly uint[] FaceCorners = [4, 5];
    private static readonly uint[] Corners = [0, 1, 2, 3, 4, 5, 6, 7, 8];

    // A second cage: the same quad and pentagon and a triangle above them (one more vertex), for the edit after a render.
    private static readonly float[] LargerCage = [.. Cage, 0f, 2.4f, .3f];
    private static readonly uint[] LargerFaceCorners = [4, 5, 3];
    private static readonly uint[] LargerCorners = [.. Corners, 3, 2, 9];

    // A corner UV layout of the faces' own shapes, each face an island of the image (by its bounding box in x and y).
    internal static float[] Layout(float[] cage, uint[] faceCorners, uint[] corners, float shift = 0)
    {
        float[] uv = new float[corners.Length * 2];
        float width = .94f / faceCorners.Length;
        for (int face = 0, start = 0; face < faceCorners.Length; start += (int)faceCorners[face++])
        {
            uint[] own = corners[start..(start + (int)faceCorners[face])];
            float x0 = own.Min(i => cage[i * 3]), x1 = own.Max(i => cage[i * 3]), y0 = own.Min(i => cage[i * 3 + 1]), y1 = own.Max(i => cage[i * 3 + 1]);
            for (int k = 0; k < own.Length; k++)
            {
                uv[(start + k) * 2] = .03f + face * width + (width - .03f) * (cage[own[k] * 3] - x0) / (x1 - x0);
                uv[(start + k) * 2 + 1] = .04f + shift + (.9f - shift) * (cage[own[k] * 3 + 1] - y0) / (y1 - y0);
            }
        }
        return uv;
    }

    // The lit scene around the target: a sphere lamp, a constant world, the target's textured diffuse material (a checker in
    // its UV layout) and, when displaced, true displacement by a wave in the same layout.
    private static ShaderNode Room(CyclesScene scene, bool displaced)
    {
        IntegratorNode integrator = scene.Integrator;
        integrator.UseGuiding = false;
        integrator.UseLightTree = false;
        integrator.UseAdaptiveSampling = false;
        integrator.UseDenoise = false;
        integrator.SamplingPattern = IntegratorSamplingPattern.TabulatedSobol;
        scene.Camera.Matrix = Transform3x4.CreateTranslation(0, 0, -7);
        ShaderNode world = scene.CreateShader();
        BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
        background.Color = new Vector3(.3f, .35f, .4f);
        background.Strength = 1;
        world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
        world.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        scene.Background.Shader = world;
        ShaderNode lamp = scene.CreateShader();
        EmissionNode glow = lamp.Add<EmissionNode>();
        glow.Color = Vector3.One;
        glow.Strength = 1;
        lamp.Connect(glow.EmissionOutput, lamp.Output.SurfaceInput);
        PointLightNode light = scene.CreateLight<PointLightNode>();
        light.Strength = new Vector3(200, 190, 180);
        light.Radius = .4f;
        light.UsedShaders = [lamp];
        scene.CreateObject(light, Transform3x4.CreateTranslation(1, 2, -3));

        ShaderNode material = scene.CreateShader();
        TextureCoordinateNode coordinates = material.Add<TextureCoordinateNode>();
        CheckerTextureNode checker = material.Add<CheckerTextureNode>();
        checker.Scale = 6;
        checker.Color1 = new Vector3(.8f, .3f, .2f);
        checker.Color2 = new Vector3(.2f, .5f, .7f);
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        material.Connect(coordinates.UvOutput, checker.VectorInput);
        material.Connect(checker.ColorOutput, diffuse.ColorInput);
        material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);
        if (displaced)
        {
            WaveTextureNode wave = material.Add<WaveTextureNode>();
            wave.Scale = 3;
            DisplacementNode displacement = material.Add<DisplacementNode>();
            displacement.Midlevel = .5f;
            displacement.Scale = .25f;
            material.Connect(coordinates.UvOutput, wave.VectorInput);
            material.Connect(wave.FacOutput, displacement.HeightInput);
            material.Connect(displacement.DisplacementOutput, material.Output.DisplacementInput);
            material.DisplacementMethod = ShaderDisplacementMethod.True;
        }
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        return material;
    }

    // The scene with the cage as the bake target, diced in object space (the space a bake can dice in), the quad smooth (normals
    // interpolated from the cage's vertex normals) and the pentagon flat (the normals of the diced triangles).
    internal static (ObjectNode Target, MeshNode Mesh) Build(CyclesScene scene, bool displaced)
    {
        ShaderNode material = Room(scene, displaced);
        MeshNode mesh = scene.CreateMesh([], [], material);
        mesh.SetSubdivisionCage(Cage, FaceCorners, Corners);
        mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard | AttributeFlags.Subdivision, Layout(Cage, FaceCorners, Corners));
        mesh.SubdivisionType = MeshSubdivisionType.Linear;
        mesh.SubdAdaptiveSpace = MeshSubdAdaptiveSpace.Object;
        mesh.SubdDicingRate = .3f;
        mesh.SubdMaxLevel = 6;
        mesh.SubdSmooth = [true, false];
        return (scene.CreateObject(mesh), mesh);
    }

    // Cycles' tessellation of a mesh as the last render or bake left it.
    internal sealed record Tessellation(int[] Triangles, float[] Positions, float[] Normals, float[] Uv, bool[] Smooth, int[] Shader);

    internal static Tessellation Read(MeshNode mesh) => new(mesh.Triangles, mesh.GetAttribute("P", AttributeFlags.Standard),
        mesh.GetAttribute("N", AttributeFlags.Standard), mesh.GetAttribute("uv", AttributeFlags.Standard), mesh.Smooth, mesh.Shader);

    // The undisplaced tessellation of the cage, read after a bake of the scene without displacement.
    internal static Tessellation Diced()
    {
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (ObjectNode target, MeshNode mesh) = Build(scene, false);
        scene.Bake(target, new BakeSettings(4, 4, 1, BakeType.Position));
        return Read(mesh);
    }

    // The same scene with an ordinary mesh built from the tessellation as the target. A second object that no ray sees uses the
    // mesh too, so Cycles keeps it in object space as it keeps every subdivision surface (ObjectManager::apply_static_transforms
    // bakes only single-user meshes without subdivision into world space, and that path rounds differently).
    internal static (ObjectNode Target, MeshNode Mesh) BuildPlain(CyclesScene scene, Tessellation diced, bool displaced)
    {
        ShaderNode material = Room(scene, displaced);
        MeshNode mesh = scene.CreateMesh(diced.Positions, [.. diced.Triangles.Select(i => (uint)i)], material);
        mesh.Smooth = diced.Smooth;
        mesh.Shader = diced.Shader;
        mesh.SetAttribute("N", AttributeValue.Normal, AttributeDomain.VertexNormal, AttributeFlags.Standard, diced.Normals);
        mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, diced.Uv);
        ObjectNode target = scene.CreateObject(mesh);
        scene.CreateObject(mesh).Visibility = (uint)RayVisibility.None;
        return (target, mesh);
    }

    internal static BakeSettings Settings(BakeType type) => new(Width, Height, 8, type);

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void a_subdivision_surface_bakes_bit_for_bit_like_the_mesh_of_its_tessellation(bool displaced)
    {
        //Arrange
        Tessellation diced = Diced();
        using CyclesScene subdivided = TestScenes.CreateCpuScene();
        (ObjectNode target, _) = Build(subdivided, displaced);
        using CyclesScene plain = TestScenes.CreateCpuScene();
        (ObjectNode plainTarget, MeshNode plainMesh) = BuildPlain(plain, diced, displaced);
        using CyclesScene undisplaced = TestScenes.CreateCpuScene();
        (ObjectNode undisplacedTarget, _) = Build(undisplaced, false);

        //Act
        BakeMap map = subdivided.ReadBakeMap(target, Settings(BakeType.Combined));
        BakeMap plainMap = plain.ReadBakeMap(plainTarget, Settings(BakeType.Combined));
        FilmBuffer[] films = [.. Types.Select(type => subdivided.Bake(target, Settings(type)))];
        FilmBuffer[] plainFilms = [.. Types.Select(type => plain.Bake(plainTarget, Settings(type)))];
        FilmBuffer flat = undisplaced.Bake(undisplacedTarget, Settings(BakeType.Position));

        //Assert
        // Cycles diced the two faces into hundreds of triangles, smooth and flat, and the ordinary mesh holds exactly that tessellation.
        (diced.Triangles.Length / 3).Should().BeGreaterThan(100);
        diced.Smooth.Distinct().Count().Should().Be(2);
        plainMesh.GetAttribute("N", AttributeFlags.Standard).SequenceEqual(diced.Normals).Should().BeTrue();
        int covered = map.Triangles.ToArray().Count(triangle => triangle >= 0);
        covered.Should().BeGreaterThan(Width * Height / 2);
        covered.Should().BeLessThan(Width * Height);
        map.Triangles.ToArray().Max().Should().BeLessThan(diced.Triangles.Length / 3);
        map.Triangles.SequenceEqual(plainMap.Triangles).Should().BeTrue();
        map.Coordinates.SequenceEqual(plainMap.Coordinates).Should().BeTrue();
        for (int i = 0; i < Types.Length; i++)
        {
            films[i].Components.Should().Be(Settings(Types[i]).Components);
            films[i].Span.ToArray().All(float.IsFinite).Should().BeTrue();
            films[i].Span.SequenceEqual(plainFilms[i].Span).Should().BeTrue(Types[i].ToString());
        }
        // The displacement moved the baked surface; without it the position is the undisplaced tessellation's.
        films[3].Span.SequenceEqual(flat.Span).Should().Be(!displaced);
        // The bake map is the one the bake used: reading it again after the bakes gives the same.
        subdivided.ReadBakeMap(target, Settings(BakeType.Combined)).Triangles.SequenceEqual(map.Triangles).Should().BeTrue();
    }

    [Fact]
    public void pixel_space_dicing_and_catmull_clark_are_refused_by_name_and_the_uv_layout_is_a_subdivision_attribute()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (ObjectNode target, MeshNode mesh) = Build(scene, false);
        BakeSettings settings = Settings(BakeType.Combined);

        //Act
        mesh.SubdAdaptiveSpace = MeshSubdAdaptiveSpace.Pixel;
        NativeFailure pixel = Assert.Throws<NativeFailure>(() => scene.Bake(target, settings));
        NativeFailure pixelMap = Assert.Throws<NativeFailure>(() => scene.ReadBakeMap(target, settings));
        mesh.SubdAdaptiveSpace = MeshSubdAdaptiveSpace.Object;
        NativeFailure missing = Assert.Throws<NativeFailure>(() => scene.Bake(target, settings with { UvAttribute = "second" }));
        mesh.SubdivisionType = MeshSubdivisionType.CatmullClark;
        NativeFailure catmullClark = Assert.Throws<NativeFailure>(() => scene.Bake(target, settings));
        mesh.SubdivisionType = MeshSubdivisionType.Linear;
        FilmBuffer film = scene.Bake(target, settings);

        //Assert
        foreach (NativeFailure failure in new[] { pixel, pixelMap })
        {
            failure.NativeStatus.Should().Be(NativeStatus.Unsupported);
            failure.Message.Should().Contain("bakes of subdivision surfaces diced in pixel space");
        }
        missing.NativeStatus.Should().Be(NativeStatus.Invalid);
        missing.Message.Should().Contain("among its subdivision attributes");
        catmullClark.NativeStatus.Should().Be(NativeStatus.Unsupported);
        catmullClark.Message.Should().Contain("Catmull-Clark subdivision needs OpenSubdiv");
        film.Span.ToArray().Where((value, index) => index % 4 == 3).Count(alpha => alpha == 1).Should().BeGreaterThan(Width * Height / 2);
    }

    // Another subdivision surface of the baked scene diced in pixel space (provisional): Cycles' device update would dice it in
    // the scene's dicing camera, which a bake does not set (the camera of the last render, or Cycles' default before any), so
    // a bake of any object is refused by name while one is in the scene, also of an ordinary mesh, before and after a render;
    // the bake map (the target alone) is not, a cage without an object is not diced into anything a ray meets, and in object
    // space the bake is bit for bit that of the same scene built with it so.
    [Fact]
    public void a_bake_is_refused_by_name_while_another_subdivision_surface_is_diced_in_pixel_space()
    {
        //Arrange
        BakeSettings settings = Settings(BakeType.Combined);
        MeshNode Other(CyclesScene scene, MeshSubdAdaptiveSpace space)
        {
            MeshNode other = scene.CreateMesh([], []);
            other.SetSubdivisionCage([.. Cage.Select((value, i) => i % 3 == 2 ? value - 3f : value)], FaceCorners, Corners);
            other.SubdivisionType = MeshSubdivisionType.Linear;
            other.SubdAdaptiveSpace = space;
            return other;
        }
        using CyclesScene reference = TestScenes.CreateCpuScene();
        (ObjectNode referenceTarget, _) = Build(reference, false);
        reference.CreateObject(Other(reference, MeshSubdAdaptiveSpace.Object));
        FilmBuffer expected = reference.Bake(referenceTarget, settings);
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (ObjectNode target, _) = Build(scene, false);
        MeshNode other = Other(scene, MeshSubdAdaptiveSpace.Pixel);
        FilmBuffer withoutObject = scene.Bake(target, settings);
        scene.CreateObject(other);
        MeshNode plain = scene.CreateMesh([-1f, -3f, 1f, 1f, -3f, 1f, 1f, -2f, 1f], [0, 1, 2]);
        plain.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, [0f, 0f, 1f, 0f, 1f, 1f]);
        ObjectNode plainTarget = scene.CreateObject(plain);
        void Refused(ObjectNode baked)
        {
            NativeFailure error = Assert.Throws<NativeFailure>(() => scene.Bake(baked, settings));
            error.NativeStatus.Should().Be(NativeStatus.Unsupported);
            error.Message.Should().Contain("Unsupported feature: bakes of subdivision surfaces diced in pixel space");
            error.Message.Should().Contain("another subdivision surface of the baked scene");
        }

        //Act / Assert
        withoutObject.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        Refused(target);
        Refused(plainTarget);
        scene.ReadBakeMap(target, settings).Triangles.ToArray().Count(triangle => triangle >= 0).Should().BeGreaterThan(Width * Height / 2);
        scene.Render(new RenderSettings(32, 24, 4));
        Refused(target);
        other.SubdAdaptiveSpace = MeshSubdAdaptiveSpace.Object;
        scene.Remove(plainTarget);
        scene.Bake(target, settings).Span.SequenceEqual(expected.Span).Should().BeTrue();
    }

    [Fact]
    public void a_triangle_mesh_with_linear_subdivision_and_no_cage_bakes_its_triangles()
    {
        //Arrange
        Tessellation diced = Diced();
        using CyclesScene typed = TestScenes.CreateCpuScene();
        (ObjectNode target, MeshNode mesh) = BuildPlain(typed, diced, false);
        mesh.SubdivisionType = MeshSubdivisionType.Linear;
        using CyclesScene plain = TestScenes.CreateCpuScene();
        (ObjectNode plainTarget, _) = BuildPlain(plain, diced, false);

        //Act
        FilmBuffer film = typed.Bake(target, Settings(BakeType.Combined));
        FilmBuffer reference = plain.Bake(plainTarget, Settings(BakeType.Combined));

        //Assert: Cycles dices no faces (Mesh::tessellate returns when there are none), so the triangles are baked as they are.
        film.Span.SequenceEqual(reference.Span).Should().BeTrue();
    }

    [Fact]
    public void after_an_edit_of_the_cage_or_of_its_uv_layout_a_bake_and_a_render_match_a_new_scene()
    {
        //Arrange
        RenderSettings render = new(32, 24, 4);
        BakeSettings settings = Settings(BakeType.Combined);
        float[] shifted = Layout(Cage, FaceCorners, Corners, .05f);
        float[] larger = Layout(LargerCage, LargerFaceCorners, LargerCorners);
        (FilmBuffer Bake, FilmBuffer Render) Fresh(Action<MeshNode> edit)
        {
            using CyclesScene fresh = TestScenes.CreateCpuScene();
            (ObjectNode target, MeshNode mesh) = Build(fresh, false);
            edit(mesh);
            return (fresh.Bake(target, settings), fresh.Render(render));
        }
        (FilmBuffer Bake, FilmBuffer Render) original = Fresh(_ => { });
        (FilmBuffer Bake, FilmBuffer Render) newLayout = Fresh(mesh => mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner,
            AttributeFlags.Standard | AttributeFlags.Subdivision, shifted));
        (FilmBuffer Bake, FilmBuffer Render) newCage = Fresh(mesh =>
        {
            mesh.SetSubdivisionCage(LargerCage, LargerFaceCorners, LargerCorners);
            mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard | AttributeFlags.Subdivision, larger);
            mesh.SubdSmooth = [true, false, true];
        });
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (ObjectNode sceneTarget, MeshNode sceneMesh) = Build(scene, false);

        //Act
        FilmBuffer firstRender = scene.Render(render);
        FilmBuffer firstBake = scene.Bake(sceneTarget, settings);
        FilmBuffer renderAfterBake = scene.Render(render);
        sceneMesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard | AttributeFlags.Subdivision, shifted);
        FilmBuffer layoutRender = scene.Render(render);
        FilmBuffer layoutBake = scene.Bake(sceneTarget, settings);
        sceneMesh.SetSubdivisionCage(LargerCage, LargerFaceCorners, LargerCorners);
        sceneMesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard | AttributeFlags.Subdivision, larger);
        sceneMesh.SubdSmooth = [true, false, true];
        FilmBuffer cageBake = scene.Bake(sceneTarget, settings);
        FilmBuffer cageRender = scene.Render(render);

        //Assert
        firstRender.Span.SequenceEqual(original.Render.Span).Should().BeTrue();
        firstBake.Span.SequenceEqual(original.Bake.Span).Should().BeTrue();
        renderAfterBake.Span.SequenceEqual(original.Render.Span).Should().BeTrue();
        layoutRender.Span.SequenceEqual(newLayout.Render.Span).Should().BeTrue();
        layoutBake.Span.SequenceEqual(newLayout.Bake.Span).Should().BeTrue();
        cageBake.Span.SequenceEqual(newCage.Bake.Span).Should().BeTrue();
        cageRender.Span.SequenceEqual(newCage.Render.Span).Should().BeTrue();
        // Each edit changed the films.
        newLayout.Bake.Span.SequenceEqual(original.Bake.Span).Should().BeFalse();
        newLayout.Render.Span.SequenceEqual(original.Render.Span).Should().BeFalse();
        newCage.Bake.Span.SequenceEqual(newLayout.Bake.Span).Should().BeFalse();
        newCage.Render.Span.SequenceEqual(original.Render.Span).Should().BeFalse();
    }
}
