// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text.RegularExpressions;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Rendering;
using CodeBrix.Graphics3D.Cycles.Validation;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Scene settings a consumer reaches through the typed API, on the CPU reference backend: each one either has its
// effect on the film or is refused with an error; none is set and silently lost.
public class SceneSettingsTests
{
    private static readonly RenderSettings Small = new(32, 24, 8);

    // A plane in front of the camera whose emission strength comes from the given output, over a black world.
    private static (ShaderNode Material, EmissionNode Emission, MeshNode Mesh) BuildEmissionPlane(CyclesScene scene)
    {
        scene.Integrator.UseLightTree = false;
        scene.Integrator.SamplingPattern = IntegratorSamplingPattern.TabulatedSobol;
        scene.Camera.Fov = 1.1f;
        scene.Camera.Matrix = Transform3x4.CreateTranslation(0, 0, -4);
        ShaderNode material = scene.CreateShader();
        EmissionNode emission = material.Add<EmissionNode>();
        emission.Color = Vector3.One;
        emission.Strength = 1f;
        material.Connect(emission.EmissionOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        MeshNode mesh = scene.CreateMesh([-3, -3, 0, 3, -3, 0, 3, 3, 0, -3, 3, 0], [0, 1, 2, 0, 2, 3], material);
        scene.CreateObject(mesh);
        ShaderNode world = scene.CreateShader();
        BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
        background.Strength = 0f;
        world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
        scene.Background.Shader = world;
        return (material, emission, mesh);
    }

    [Fact]
    public void Camera_viewplane_set_by_the_caller_is_kept_and_all_zero_restores_the_automatic_one()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        CameraNode camera = scene.Camera;
        FilmBuffer automatic = scene.Render(Small);
        float left = camera.ViewplaneLeft, right = camera.ViewplaneRight, bottom = camera.ViewplaneBottom, top = camera.ViewplaneTop;

        //Act
        camera.ViewplaneLeft = left;
        FilmBuffer sameExplicit = scene.Render(Small);
        camera.ViewplaneLeft = left + .4f;
        camera.ViewplaneRight = right + .4f;
        FilmBuffer shifted = scene.Render(Small);
        float shiftedLeft = camera.ViewplaneLeft;
        camera.ViewplaneLeft = camera.ViewplaneRight = camera.ViewplaneBottom = camera.ViewplaneTop = 0f;
        FilmBuffer restored = scene.Render(Small);

        //Assert
        left.Should().BeApproximately(-32f / 24f, 1e-6f);
        right.Should().BeApproximately(32f / 24f, 1e-6f);
        bottom.Should().Be(-1f);
        top.Should().Be(1f);
        sameExplicit.Span.SequenceEqual(automatic.Span).Should().BeTrue();
        shiftedLeft.Should().Be(left + .4f);
        shifted.Span.SequenceEqual(automatic.Span).Should().BeFalse();
        WebGpuValidationScenes.Compare(shifted.Span, automatic.Span).Practical.Should().BeGreaterThan(0);
        restored.Span.SequenceEqual(automatic.Span).Should().BeTrue();
        camera.ViewplaneLeft.Should().Be(left);
    }

    [Fact]
    public void Camera_viewplane_whose_left_is_not_below_its_right_fails_the_render()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        scene.Camera.ViewplaneLeft = 1f;
        scene.Camera.ViewplaneRight = -1f;

        //Act
        Action render = () => scene.Render(Small);

        //Assert
        render.Should().Throw<CyclesException>().Which.Message.Should().Contain("viewplane");
    }

    [Fact]
    public void Render_owned_sockets_refuse_a_change_and_accept_their_current_value()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        scene.Render(Small);

        //Act
        Action width = () => scene.Camera.FullWidth = 64;
        Action height = () => scene.Camera.FullHeight = 64;
        Action samples = () => scene.Integrator.AaSamples = 3;
        scene.Camera.FullWidth = scene.Camera.FullWidth;
        scene.Integrator.AaSamples = scene.Integrator.AaSamples;

        //Assert
        width.Should().Throw<CyclesException>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
        height.Should().Throw<CyclesException>().Which.Message.Should().Contain("render configuration");
        samples.Should().Throw<CyclesException>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
        scene.Camera.FullWidth.Should().Be(32);
        scene.Camera.FullHeight.Should().Be(24);
        scene.Integrator.AaSamples.Should().Be(8);
    }

    [Fact]
    public void Film_display_settings_and_session_state_without_effect_on_the_film_refuse_a_change()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        FilmNode film = scene.Film;

        //Act
        Action displayPass = () => film.DisplayPass = FilmDisplayPass.Normal;
        Action activePixels = () => film.ShowActivePixels = true;
        Action sampleCount = () => film.UseSampleCount = true;
        film.DisplayPass = film.DisplayPass;
        film.ShowActivePixels = false;
        film.UseSampleCount = false;

        //Assert
        displayPass.Should().Throw<CyclesException>().Which.Message.Should().Contain("viewport display setting");
        activePixels.Should().Throw<CyclesException>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
        sampleCount.Should().Throw<CyclesException>().Which.Message.Should().Contain("sample_count");
        film.DisplayPass.Should().Be(FilmDisplayPass.Combined);
    }

    [Fact]
    public void Denoiser_upscale_factor_other_than_one_is_refused_where_the_cpu_denoiser_runs()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        scene.Integrator.DenoiserUpscaleFactor = 2f;
        FilmBuffer withoutDenoising = scene.Render(Small);
        scene.Integrator.UseDenoise = true;

        //Act
        Action render = () => scene.Render(Small);

        //Assert
        withoutDenoising.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        CyclesException error = render.Should().Throw<CyclesException>().Which;
        error.NativeStatus.Should().Be(NativeStatus.Unsupported);
        error.Message.Should().Contain("upscale");
    }

    [Fact]
    public void Dispersion_renders_on_the_cpu_backend_exactly_when_the_scale_and_the_transmission_weight_are_both_set_or_linked()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (ShaderNode material, _, _) = TestScenes.BuildTyped(scene);
        PrincipledBsdfNode principled = material.Add<PrincipledBsdfNode>();
        ValueNode value = material.Add<ValueNode>();
        value.Value = .5f;
        material.Connect(principled.BsdfOutput, material.Output.SurfaceInput);
        float scaleByDefault = principled.TransmissionDispersionScale;
        FilmBuffer withoutDispersion = scene.Render(Small);

        //Act
        principled.TransmissionDispersionScale = .5f;
        FilmBuffer withoutTransmission = scene.Render(Small);
        principled.TransmissionWeight = 1f;
        FilmBuffer dispersive = scene.Render(Small);
        principled.TransmissionDispersionScale = 0f;
        FilmBuffer transmissive = scene.Render(Small);
        material.Connect(value.ValueOutput, principled.TransmissionDispersionScaleInput);
        FilmBuffer linked = scene.Render(Small);
        principled.TransmissionWeight = 0f;
        FilmBuffer linkedScaleWithoutTransmission = scene.Render(Small);

        //Assert
        scaleByDefault.Should().Be(0f);
        withoutDispersion.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        withoutTransmission.Span.SequenceEqual(withoutDispersion.Span).Should().BeTrue();
        dispersive.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        dispersive.Span.SequenceEqual(transmissive.Span).Should().BeFalse();
        linked.Span.SequenceEqual(transmissive.Span).Should().BeFalse();
        linkedScaleWithoutTransmission.Span.SequenceEqual(withoutDispersion.Span).Should().BeTrue();
    }

    [Fact]
    public void Dispersion_is_refused_by_name_on_the_webgpu_backend()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        if (CyclesScene.ResolveBackend(CyclesBackend.Gpu) != Backend.WebGpu) Assert.Skip("This binary's GPU backend is not WebGPU.");
        CyclesScene created;
        try
        {
            created = new CyclesScene(CyclesBackend.Gpu);
        }
        catch (CyclesException unavailable)
        {
            Assert.Skip("No GPU device is available to the native host: " + unavailable.Message);
            return;
        }
        using CyclesScene scene = created;
        (ShaderNode material, _, _) = TestScenes.BuildTyped(scene);
        PrincipledBsdfNode principled = material.Add<PrincipledBsdfNode>();
        material.Connect(principled.BsdfOutput, material.Output.SurfaceInput);
        principled.TransmissionDispersionScale = .5f;
        principled.TransmissionWeight = 1f;

        //Act
        Action render = () => scene.Render(Small);

        //Assert
        CyclesException error = render.Should().Throw<CyclesException>().Which;
        error.NativeStatus.Should().Be(NativeStatus.Unsupported);
        error.Message.Should().Contain("WebGPU dispersion not implemented");
    }

    [Fact]
    public void Dlss_denoiser_is_refused_by_name_when_denoising()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        scene.Integrator.DenoiserType = IntegratorDenoiserType.Dlss;
        FilmBuffer withoutDenoising = scene.Render(Small);
        scene.Integrator.UseDenoise = true;

        //Act
        Action render = () => scene.Render(Small);
        Action again = () => scene.Render(Small);

        //Assert
        withoutDenoising.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        CyclesException error = render.Should().Throw<CyclesException>().Which;
        error.NativeStatus.Should().Be(NativeStatus.Unsupported);
        error.Message.Should().Contain("dlss");
        scene.Integrator.DenoiserType = IntegratorDenoiserType.Openimagedenoise;
        again.Should().NotThrow();
    }

    [Fact]
    public void Point_cloud_rendered_as_gaussian_splats_is_refused_by_name()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        PointCloudNode points = scene.CreatePointCloud([-.5f, 0f, 0f, .5f, 0f, 0f], [.3f, .2f]);
        scene.CreateObject(points);
        PointCloudRenderAs renderAsByDefault = points.RenderAs;
        points.RenderAs = PointCloudRenderAs.GaussianSplats;

        //Act
        Action render = () => scene.Render(Small);
        Action again = () => scene.Render(Small);

        //Assert
        renderAsByDefault.Should().Be(PointCloudRenderAs.Points);
        CyclesException error = render.Should().Throw<CyclesException>().Which;
        error.NativeStatus.Should().Be(NativeStatus.Unsupported);
        error.Message.Should().Contain("Gaussian splats");
        points.RenderAs = PointCloudRenderAs.Points;
        again.Should().NotThrow();
    }

    [Fact]
    public void Filter_glossy_is_one_by_default_and_zero_renders_a_different_glossy_film()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (ShaderNode material, _, _) = TestScenes.BuildTyped(scene);
        scene.Integrator.CausticsReflective = true;
        GlossyBsdfNode glossy = material.Add<GlossyBsdfNode>();
        glossy.Roughness = .05f;
        material.Connect(glossy.BsdfOutput, material.Output.SurfaceInput);
        ShaderNode floorMaterial = scene.CreateShader();
        DiffuseBsdfNode floorDiffuse = floorMaterial.Add<DiffuseBsdfNode>();
        floorMaterial.Connect(floorDiffuse.BsdfOutput, floorMaterial.Output.SurfaceInput);
        scene.CreateObject(scene.CreateMesh([-4, -1.2f, -3, 4, -1.2f, -3, 4, -1.2f, 3, -4, -1.2f, 3], [0, 1, 2, 0, 2, 3], floorMaterial));

        //Act
        float filterByDefault = scene.Integrator.FilterGlossy;
        FilmBuffer filtered = scene.Render(Small);
        scene.Integrator.FilterGlossy = 0f;
        FilmBuffer unfiltered = scene.Render(Small);

        //Assert
        filterByDefault.Should().Be(1f);
        unfiltered.Span.SequenceEqual(filtered.Span).Should().BeFalse();
    }

    [Fact]
    public void Sample_subset_renders_only_the_selected_samples()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        FilmBuffer plain = scene.Render(Small);
        IntegratorNode integrator = scene.Integrator;

        //Act
        integrator.UseSampleSubset = true;
        integrator.SampleSubsetOffset = 0;
        integrator.SampleSubsetLength = 8;
        FilmBuffer whole = scene.Render(Small);
        integrator.SampleSubsetOffset = 4;
        integrator.SampleSubsetLength = 4;
        FilmBuffer secondHalf = scene.Render(Small);
        integrator.SampleSubsetOffset = 0;
        FilmBuffer firstHalf = scene.Render(Small);
        integrator.UseSampleSubset = false;
        FilmBuffer plainAgain = scene.Render(Small);

        //Assert
        whole.Span.SequenceEqual(plain.Span).Should().BeTrue();
        secondHalf.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        secondHalf.Span.SequenceEqual(plain.Span).Should().BeFalse();
        secondHalf.Span.SequenceEqual(firstHalf.Span).Should().BeFalse();
        plainAgain.Span.SequenceEqual(plain.Span).Should().BeTrue();
        integrator.SampleSubsetOffset.Should().Be(0);
        integrator.SampleSubsetLength.Should().Be(4);
    }

    [Fact]
    public void Shader_link_between_different_value_types_renders_as_an_explicit_conversion()
    {
        //Arrange
        using CyclesScene implicitScene = TestScenes.CreateCpuScene();
        (ShaderNode implicitMaterial, EmissionNode implicitEmission, _) = BuildEmissionPlane(implicitScene);
        ColorNode implicitColor = implicitMaterial.Add<ColorNode>();
        implicitColor.Value = new Vector3(.2f, .6f, .9f);
        using CyclesScene explicitScene = TestScenes.CreateCpuScene();
        (ShaderNode explicitMaterial, EmissionNode explicitEmission, _) = BuildEmissionPlane(explicitScene);
        ColorNode explicitColor = explicitMaterial.Add<ColorNode>();
        explicitColor.Value = new Vector3(.2f, .6f, .9f);
        ConvertColorToFloatNode convert = explicitMaterial.Add<ConvertColorToFloatNode>();
        explicitMaterial.Connect(explicitColor.ColorOutput, convert.ValueColorInput);
        explicitMaterial.Connect(convert.ValueFloatOutput, explicitEmission.StrengthInput);
        using CyclesScene constantScene = TestScenes.CreateCpuScene();
        BuildEmissionPlane(constantScene);

        //Act
        implicitMaterial.Connect(implicitColor.ColorOutput, implicitEmission.StrengthInput);
        Action closureIntoValue = () => implicitMaterial.Connect(implicitEmission.EmissionOutput, implicitEmission.StrengthInput);
        FilmBuffer converted = implicitScene.Render(Small);
        FilmBuffer reference = explicitScene.Render(Small);
        FilmBuffer constant = constantScene.Render(Small);

        //Assert
        closureIntoValue.Should().Throw<CyclesException>().Which.NativeStatus.Should().Be(NativeStatus.Unsupported);
        converted.Span.SequenceEqual(reference.Span).Should().BeTrue();
        converted.Span.SequenceEqual(constant.Span).Should().BeFalse();
    }

    [Fact]
    public void Scene_time_node_reads_the_scene_attributes_time_and_frame()
    {
        //Arrange
        using CyclesScene timed = TestScenes.CreateCpuScene();
        (ShaderNode material, EmissionNode emission, _) = BuildEmissionPlane(timed);
        SceneTimeNode time = material.Add<SceneTimeNode>();
        material.Connect(time.FrameOutput, emission.StrengthInput);
        using CyclesScene constant = TestScenes.CreateCpuScene();
        (_, EmissionNode constantEmission, _) = BuildEmissionPlane(constant);
        constantEmission.Strength = 2.5f;

        //Act
        float defaultTime = timed.SceneAttributes.Time, defaultFrame = timed.SceneAttributes.Frame;
        FilmBuffer frameOne = timed.Render(Small);
        timed.SceneAttributes.Frame = 2.5f;
        timed.SceneAttributes.Time = .1f;
        FilmBuffer frameTwoAndAHalf = timed.Render(Small);
        FilmBuffer reference = constant.Render(Small);

        //Assert
        defaultTime.Should().Be(0f);
        defaultFrame.Should().Be(1f);
        timed.SceneAttributes.Frame.Should().Be(2.5f);
        WebGpuValidationScenes.Compare(frameTwoAndAHalf.Span, reference.Span).Practical.Should().Be(0);
        WebGpuValidationScenes.Compare(frameOne.Span, reference.Span).Practical.Should().BeGreaterThan(0);
    }

    [Fact]
    public void Pointiness_and_random_per_island_standard_attributes_feed_the_geometry_node()
    {
        //Arrange
        using CyclesScene pointiness = TestScenes.CreateCpuScene();
        (ShaderNode pointinessMaterial, EmissionNode pointinessEmission, MeshNode pointinessMesh) = BuildEmissionPlane(pointiness);
        GeometryNode pointinessGeometry = pointinessMaterial.Add<GeometryNode>();
        pointinessMaterial.Connect(pointinessGeometry.PointinessOutput, pointinessEmission.StrengthInput);
        using CyclesScene island = TestScenes.CreateCpuScene();
        (ShaderNode islandMaterial, EmissionNode islandEmission, MeshNode islandMesh) = BuildEmissionPlane(island);
        GeometryNode islandGeometry = islandMaterial.Add<GeometryNode>();
        islandMaterial.Connect(islandGeometry.RandomPerIslandOutput, islandEmission.StrengthInput);
        using CyclesScene constant = TestScenes.CreateCpuScene();
        (_, EmissionNode constantEmission, _) = BuildEmissionPlane(constant);
        constantEmission.Strength = .25f;
        using CyclesScene unset = TestScenes.CreateCpuScene();
        (ShaderNode unsetMaterial, EmissionNode unsetEmission, _) = BuildEmissionPlane(unset);
        GeometryNode unsetGeometry = unsetMaterial.Add<GeometryNode>();
        unsetMaterial.Connect(unsetGeometry.PointinessOutput, unsetEmission.StrengthInput);

        //Act
        pointinessMesh.SetAttribute("pointiness", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.Standard, [.25f, .25f, .25f, .25f]);
        islandMesh.SetAttribute("random_per_island", AttributeValue.Float, AttributeDomain.Face, AttributeFlags.Standard, [.25f, .25f]);
        Action wrongDomain = () => islandMesh.SetAttribute("random_per_island", AttributeValue.Float, AttributeDomain.Vertex, AttributeFlags.Standard, [.25f, .25f, .25f, .25f]);
        FilmBuffer fromPointiness = pointiness.Render(Small);
        FilmBuffer fromIsland = island.Render(Small);
        FilmBuffer reference = constant.Render(Small);
        FilmBuffer withoutAttribute = unset.Render(Small);

        //Assert
        wrongDomain.Should().Throw<CyclesException>();
        WebGpuValidationScenes.Compare(fromPointiness.Span, reference.Span).Practical.Should().Be(0);
        WebGpuValidationScenes.Compare(fromIsland.Span, reference.Span).Practical.Should().Be(0);
        WebGpuValidationScenes.Compare(withoutAttribute.Span, reference.Span).Practical.Should().BeGreaterThan(0);
    }

    [Fact]
    public void Catmull_clark_subdivision_is_refused_by_name()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (_, _, MeshNode mesh) = TestScenes.BuildTyped(scene);
        mesh.SubdivisionType = MeshSubdivisionType.CatmullClark;

        //Act
        Action render = () => scene.Render(Small);
        Action again = () => scene.Render(Small);

        //Assert
        CyclesException error = render.Should().Throw<CyclesException>().Which;
        error.NativeStatus.Should().Be(NativeStatus.Unsupported);
        error.Message.Should().Contain("Catmull-Clark");
        mesh.SubdivisionType = MeshSubdivisionType.None;
        again.Should().NotThrow();
    }

    // A quad, as a linear subdivision cage or as two triangles, in the lit-triangle scene.
    private static (CyclesScene Scene, MeshNode Mesh) BuildQuad(bool cage)
    {
        CyclesScene scene = TestScenes.CreateCpuScene();
        (ShaderNode material, _, MeshNode triangle) = TestScenes.BuildTyped(scene);
        float[] quad = [-1.5f, -1, 0, 1.5f, -1, 0, 1.5f, 1.5f, 0, -1.5f, 1.5f, 0];
        if (cage)
        {
            triangle.SetSubdivisionCage(quad, [4], [0, 1, 2, 3]);
            triangle.SubdivisionType = MeshSubdivisionType.Linear;
            triangle.SubdDicingRate = 2f;
        }
        else triangle.SetTriangles(quad, [0, 1, 2, 0, 2, 3]);
        return (scene, triangle);
    }

    [Fact]
    public void Linear_subdivision_cage_renders_as_the_quad_it_describes()
    {
        //Arrange
        (CyclesScene subdivided, MeshNode cage) = BuildQuad(true);
        (CyclesScene triangles, _) = BuildQuad(false);
        using CyclesScene plain = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(plain);

        //Act
        FilmBuffer diced = subdivided.Render(Small);
        FilmBuffer reference = triangles.Render(Small);
        FilmBuffer triangleOnly = plain.Render(Small);
        cage.SubdivisionType = MeshSubdivisionType.None;
        Action withoutType = () => subdivided.Render(Small);

        //Assert
        WebGpuValidationScenes.Compare(diced.Span, reference.Span).Practical.Should().Be(0);
        WebGpuValidationScenes.Compare(diced.Span, triangleOnly.Span).Practical.Should().BeGreaterThan(0);
        cage.SubdFaceCorners.Should().Equal(0, 1, 2, 3);
        withoutType.Should().Throw<CyclesException>().Which.Message.Should().Contain("subdivision_type");
        subdivided.Dispose();
        triangles.Dispose();
    }

    [Fact]
    public void Camera_border_renders_its_region_of_the_film_and_zero_outside_it()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        CameraNode camera = scene.Camera;
        PassNode depth = scene.AddPass(PassType.Depth, "Depth");
        FilmBuffer whole = scene.Render(Small);
        float[] wholeDepth = scene.ReadPass("Depth").Span.ToArray();
        float left = camera.BorderLeft, right = camera.BorderRight, bottom = camera.BorderBottom, top = camera.BorderTop;

        //Act
        camera.BorderLeft = .25f;
        camera.BorderRight = .875f;
        camera.BorderBottom = .5f;
        FilmBuffer region = scene.Render(Small);
        float[] regionDepth = scene.ReadPass("Depth").Span.ToArray();
        camera.BorderRight = .1f;
        Action inverted = () => scene.Render(Small);

        //Assert
        (left, right, bottom, top).Should().Be((0f, 1f, 0f, 1f));
        int inside = 0;
        for (int y = 0; y < 24; y++)
        for (int x = 0; x < 32; x++)
        {
            bool covered = x >= 8 && x < 28 && y >= 12;
            if (covered) inside++;
            for (int c = 0; c < 4; c++)
                region.Span[(y * 32 + x) * 4 + c].Should().Be(covered ? whole.Span[(y * 32 + x) * 4 + c] : 0f);
            regionDepth[y * 32 + x].Should().Be(covered ? wholeDepth[y * 32 + x] : 0f);
        }
        inside.Should().Be(20 * 12);
        inverted.Should().Throw<CyclesException>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
        depth.IsRemoved.Should().BeFalse();
    }

    [Fact]
    public void RenderTimeLimit_stops_a_long_render_early_and_zero_renders_every_sample()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        FilmBuffer plain = scene.Render(Small);
        Stopwatch watch = new();

        //Act
        scene.RenderTimeLimit = TimeSpan.FromSeconds(.25);
        watch.Start();
        FilmBuffer limited = scene.Render(new RenderSettings(32, 24, 1_000_000));
        watch.Stop();
        scene.RenderTimeLimit = TimeSpan.Zero;
        FilmBuffer unlimited = scene.Render(Small);
        Action negative = () => scene.RenderTimeLimit = TimeSpan.FromSeconds(-1);

        //Assert
        watch.Elapsed.Should().BeLessThan(TimeSpan.FromSeconds(60));
        limited.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        limited.Span.ToArray().Any(value => value > 0).Should().BeTrue();
        unlimited.Span.SequenceEqual(plain.Span).Should().BeTrue();
        scene.RenderTimeLimit.Should().Be(TimeSpan.Zero);
        negative.Should().Throw<ArgumentOutOfRangeException>();
    }

    [Fact]
    public void Ray_visibility_flags_read_and_write_the_visibility_socket()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (_, _, MeshNode mesh) = TestScenes.BuildTyped(scene);
        ObjectNode instance = scene.CreateObject(mesh, Transform3x4.CreateTranslation(0, 0, 1));
        FilmBuffer visible = scene.Render(Small);

        //Act
        RayVisibility before = instance.RayVisibility;
        instance.RayVisibility = RayVisibility.All & ~RayVisibility.Camera;
        scene.Background.RayVisibility = RayVisibility.All & ~RayVisibility.Camera;
        FilmBuffer hidden = scene.Render(Small);

        //Assert
        before.Should().Be(RayVisibility.All);
        instance.Visibility.Should().Be((uint)(RayVisibility.All & ~RayVisibility.Camera));
        scene.Background.Visibility.Should().Be(0xFEu);
        hidden.Span.SequenceEqual(visible.Span).Should().BeFalse();
    }

    [Fact]
    public void Denoiser_guides_and_cryptomatte_passes_are_flag_sets()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        DenoiserGuides guides = scene.Integrator.DenoiserGuides;
        scene.Integrator.DenoiserGuides = DenoiserGuides.Albedo;
        scene.Film.CryptomattePasses = FilmCryptomattePasses.Object | FilmCryptomattePasses.Material;

        //Assert
        guides.Should().Be(DenoiserGuides.Albedo | DenoiserGuides.Normal);
        scene.Integrator.DenoiserPasses.Should().Be(1);
        scene.Film.CryptomattePasses.Should().Be(FilmCryptomattePasses.Object | FilmCryptomattePasses.Material);
        typeof(FilmCryptomattePasses).IsDefined(typeof(FlagsAttribute), false).Should().BeTrue();
    }

    [Fact]
    public void Denoiser_guides_are_exactly_the_denoiser_pass_bits_of_the_pinned_cycles()
    {
        //Arrange
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        string header = File.ReadAllText(Path.Combine(RepositoryPaths.Root, "third_party", "cycles", "src", "device", "denoise.h"));

        //Act
        Dictionary<string, int> native = Regex.Matches(header, @"DENOISER_PASS_(\w+) = (?:(\d+) << (\d+)|0),")
            .ToDictionary(match => match.Groups[1].Value.Replace("_", ""), match => match.Groups[2].Success ? int.Parse(match.Groups[2].Value) << int.Parse(match.Groups[3].Value) : 0);
        Dictionary<string, int> managed = Enum.GetValues<DenoiserGuides>().ToDictionary(guide => guide.ToString().ToUpperInvariant(), guide => (int)guide);

        //Assert
        native.Should().NotBeEmpty();
        managed.Should().BeEquivalentTo(native);
    }

    [Fact]
    public void Denoiser_passes_bit_that_is_no_guide_is_refused_by_name_when_denoising()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        scene.Integrator.DenoiserPasses = (int)(DenoiserGuides.Albedo | DenoiserGuides.Normal) | 128;
        FilmBuffer withoutDenoising = scene.Render(Small);
        scene.Integrator.UseDenoise = true;

        //Act
        Action render = () => scene.Render(Small);
        Action again = () => scene.Render(Small);

        //Assert
        withoutDenoising.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        CyclesException error = render.Should().Throw<CyclesException>().Which;
        error.NativeStatus.Should().Be(NativeStatus.Invalid);
        error.Message.Should().Contain("denoiser_passes");
        scene.Integrator.DenoiserGuides = DenoiserGuides.Albedo | DenoiserGuides.Normal | DenoiserGuides.SpecularMotion;
        again.Should().NotThrow();
    }
}
