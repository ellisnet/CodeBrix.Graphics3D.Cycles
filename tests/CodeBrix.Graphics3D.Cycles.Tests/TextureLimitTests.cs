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

// Cycles' texture limit (provisional CyclesScene.TextureLimit, cbc_host_texture_limit) on the CPU reference backend. The
// scene is one quad filling an orthographic view, shaded by an emission of an image texture read through its UVs (0 to 1),
// Linear interpolation, one sample. Cycles halves an image whose larger side exceeds the limit with a box filter on the
// host (ImageCache::load_full, util_image_resize_pixels), so an image of uniform 2 x 2 (or 4 x 4) blocks becomes, exactly,
// the image of the block colours: the film must equal the film of that smaller image bit for bit, and differ from the film
// of the full image (Linear filtering at another resolution).
public class TextureLimitTests
{
    private static readonly RenderSettings Settings = new(48, 48, 1);

    private static void Configure(CyclesScene scene)
    {
        IntegratorNode integrator = scene.Integrator;
        integrator.UseGuiding = false;
        integrator.UseLightTree = false;
        integrator.UseAdaptiveSampling = false;
        integrator.UseDenoise = false;
        integrator.MaxBounce = 0;
        integrator.SamplingPattern = IntegratorSamplingPattern.TabulatedSobol;
        scene.Camera.CameraType = CameraCameraType.Orthograph;
        scene.Camera.Matrix = Transform3x4.CreateTranslation(0, 0, -4);
        ShaderNode world = scene.CreateShader();
        BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
        background.Color = new Vector3(.05f, .04f, .03f);
        background.Strength = 1f;
        world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
        world.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        scene.Background.Shader = world;
    }

    // The quad (the orthographic view spans -1..1 in both directions) and its texture node, unbound.
    private static ImageTextureNode TexturedQuad(CyclesScene scene)
    {
        ShaderNode shader = scene.CreateShader();
        UvmapNode uv = shader.Add<UvmapNode>();
        ImageTextureNode texture = shader.Add<ImageTextureNode>();
        texture.Interpolation = ImageTextureInterpolation.Linear;
        texture.Extension = ImageTextureExtension.Clamp;
        EmissionNode emission = shader.Add<EmissionNode>();
        shader.Connect(uv.UvOutput, texture.VectorInput);
        shader.Connect(texture.ColorOutput, emission.ColorInput);
        shader.Connect(emission.EmissionOutput, shader.Output.SurfaceInput);
        shader.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        MeshNode mesh = scene.CreateMesh([-1f, -1f, 0, 1f, -1f, 0, 1f, 1f, 0, -1f, 1f, 0], [0, 1, 2, 0, 2, 3], shader);
        mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, [0f, 0f, 1f, 0f, 1f, 1f, 0f, 0f, 1f, 1f, 0f, 1f]);
        scene.CreateObject(mesh);
        return texture;
    }

    // The colour of block (bx, by) of a byte4 image: distinct, varied blocks.
    private static byte[] BlockByte4(int bx, int by) => [(byte)(17 * bx + 3 * by), (byte)(255 - 9 * by - 2 * bx), (byte)((37 * bx + 53 * by) % 256), 255];

    // A size x size byte4 image of uniform block x block squares.
    private static byte[] Byte4Blocks(int size, int block) =>
        [.. Enumerable.Range(0, size * size).SelectMany(i => BlockByte4(i % size / block, i / size / block))];

    // The colour of block (bx, by) of a float4 image: binary fractions with few significant bits and values above 1, so a
    // box filter of a uniform block reproduces them exactly.
    private static float[] BlockFloat4(int bx, int by) => [.25f * (bx % 11) + .0625f * by, 6f - .375f * by, .5f + .125f * ((bx + by) % 9), 1f];

    private static float[] Float4Blocks(int size, int block) =>
        [.. Enumerable.Range(0, size * size).SelectMany(i => BlockFloat4(i % size / block, i / size / block))];

    // A new CPU scene with the quad, the texture limit set first, and the image bound.
    private static FilmBuffer Render(uint limit, Func<CyclesScene, CyclesImage> image)
    {
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        scene.TextureLimit = limit;
        ImageTextureNode texture = TexturedQuad(scene);
        using CyclesImage bound = image(scene);
        texture.Bind(bound);
        return scene.Render(Settings);
    }

    private static Func<CyclesScene, CyclesImage> Byte4(int size, int block) =>
        scene => scene.CreateImage<byte>((uint)size, (uint)size, ImageFormat.Byte4, Byte4Blocks(size, block));

    private static Func<CyclesScene, CyclesImage> Float4(int size, int block) =>
        scene => scene.CreateImage<float>((uint)size, (uint)size, ImageFormat.Float4, Float4Blocks(size, block));

    [Fact]
    public void a_byte4_image_above_the_limit_renders_exactly_like_the_image_halved_to_it()
    {
        //Arrange
        Func<CyclesScene, CyclesImage> full = Byte4(64, 2);

        //Act
        FilmBuffer limited = Render(32, full);
        FilmBuffer halved = Render(0, Byte4(32, 1));
        FilmBuffer unlimited = Render(0, full);
        // Cycles rounds the scale down to a power of two: 40 / 64 halves the image once, as 32 / 64 does.
        FilmBuffer forty = Render(40, full);
        // A limit the image does not exceed, and the limit 0, keep it at its size.
        FilmBuffer sixtyFour = Render(64, full);

        //Assert
        limited.Span.SequenceEqual(halved.Span).Should().BeTrue();
        limited.Span.SequenceEqual(unlimited.Span).Should().BeFalse();
        forty.Span.SequenceEqual(halved.Span).Should().BeTrue();
        sixtyFour.Span.SequenceEqual(unlimited.Span).Should().BeTrue();
        unlimited.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(100);
    }

    [Fact]
    public void a_float4_hdr_image_is_quartered_exactly_by_a_limit_of_a_quarter_of_its_side()
    {
        //Arrange
        Func<CyclesScene, CyclesImage> full = Float4(64, 4);

        //Act
        FilmBuffer limited = Render(16, full);
        FilmBuffer quartered = Render(0, Float4(16, 1));
        FilmBuffer unlimited = Render(0, full);
        // 31 / 64 is below a half: the image is quartered.
        FilmBuffer thirtyOne = Render(31, full);

        //Assert
        limited.Span.SequenceEqual(quartered.Span).Should().BeTrue();
        thirtyOne.Span.SequenceEqual(quartered.Span).Should().BeTrue();
        limited.Span.SequenceEqual(unlimited.Span).Should().BeFalse();
        limited.Span.ToArray().Max().Should().BeGreaterThan(1f);
    }

    [Fact]
    public void cycles_own_sky_texture_image_is_reduced_too()
    {
        //Arrange
        FilmBuffer Sky(uint limit)
        {
            using CyclesScene scene = TestScenes.CreateCpuScene();
            Configure(scene);
            scene.TextureLimit = limit;
            scene.Camera.CameraType = CameraCameraType.Panorama;
            scene.Camera.PanoramaType = CameraPanoramaType.Equirectangular;
            scene.Camera.Matrix = Transform3x4.Identity;
            ShaderNode world = scene.CreateShader();
            SkyTextureNode sky = world.Add<SkyTextureNode>();
            sky.SkyType = SkyTextureSkyType.MultipleScattering;
            sky.SunDisc = false;
            sky.SunElevation = .3f;
            BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
            background.Strength = 1f;
            world.Connect(sky.ColorOutput, background.ColorInput);
            world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
            world.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
            scene.Background.Shader = world;
            return scene.Render(Settings);
        }

        //Act
        FilmBuffer full = Sky(0);
        FilmBuffer again = Sky(0);
        FilmBuffer reduced = Sky(8);

        //Assert
        full.Span.SequenceEqual(again.Span).Should().BeTrue();
        reduced.Span.SequenceEqual(full.Span).Should().BeFalse();
        reduced.Span.ToArray().All(float.IsFinite).Should().BeTrue();
    }

    [Fact]
    public void the_limit_is_fixed_once_a_render_has_loaded_images_and_free_until_then()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        scene.TextureLimit = 0;

        //Act
        // A render of a scene with no image loads none: the limit may still change, and applies to an image bound later.
        scene.Render(Settings);
        scene.TextureLimit = 32;
        ImageTextureNode texture = TexturedQuad(scene);
        using CyclesImage image = Byte4(64, 2)(scene);
        texture.Bind(image);
        FilmBuffer film = scene.Render(Settings);
        Action changed = () => scene.TextureLimit = 16;
        Action removed = () => scene.TextureLimit = 0;
        NativeFailure lowLevel = ((Action)(() => scene.Host.SetTextureLimit(64))).Should().Throw<NativeFailure>().Which;

        //Assert
        film.Span.SequenceEqual(Render(32, Byte4(64, 2)).Span).Should().BeTrue();
        changed.Should().Throw<InvalidOperationException>().Which.InnerException.Should().BeOfType<NativeFailure>();
        removed.Should().Throw<InvalidOperationException>();
        lowLevel.NativeStatus.Should().Be(NativeStatus.NotReady);
        lowLevel.Message.Should().Contain("fixed once a render or bake has loaded images");
        scene.TextureLimit.Should().Be(32u);
        scene.TextureLimit = 32;
        scene.Render(Settings).Span.SequenceEqual(film.Span).Should().BeTrue();
    }

    [Fact]
    public void a_limit_above_int_max_is_refused_and_the_value_reads_back()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        Action facade = () => scene.TextureLimit = (uint)int.MaxValue + 1;
        NativeFailure native = ((Action)(() => scene.Host.SetTextureLimit(uint.MaxValue))).Should().Throw<NativeFailure>().Which;
        scene.TextureLimit = 1024;

        //Assert
        facade.Should().Throw<ArgumentOutOfRangeException>();
        native.NativeStatus.Should().Be(NativeStatus.Invalid);
        scene.TextureLimit.Should().Be(1024u);
        scene.TextureLimit = (uint)int.MaxValue;
        scene.TextureLimit.Should().Be((uint)int.MaxValue);
    }
}
