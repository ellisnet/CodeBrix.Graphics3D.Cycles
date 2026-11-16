// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Numerics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.GltfRender;
using CodeBrix.Graphics3D.Cycles.Interop;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// UDIM tiles (provisional): several images bound to one Image Texture node (ImageTextureNode.BindTiles,
// cbc_host_image_bind_tiles) on the CPU reference backend. The scenes are a strip of quads seen by an orthographic camera
// (20 x 20 pixels per quad), shaded by an emission of the texture colour, one sample: quad k's UVs lie in the tile its
// origin names, so the film shows which image each tile read.
public class UdimTileTests
{
    private const float Inset = .25f;

    private static RenderSettings Settings(int quads) => new((uint)(20 * quads), 20, 1);

    private static readonly Vector3 MissingColor = new(.3f, .6f, .9f);

    private static void Configure(CyclesScene scene)
    {
        IntegratorNode integrator = scene.Integrator;
        integrator.UseGuiding = false;
        integrator.UseLightTree = false;
        integrator.UseAdaptiveSampling = false;
        integrator.UseDenoise = false;
        integrator.CausticsReflective = false;
        integrator.CausticsRefractive = false;
        integrator.MaxBounce = 2;
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

    // A strip of quads (1.8 units square, centred 2 units apart; the orthographic view of a W x 20 film spans the strip).
    // Quad k gets the UVs origins[k] + [inset, 1 - inset] and the face shader index faceShader(k).
    private static MeshNode Strip(CyclesScene scene, (float U, float V)[] origins, ShaderNode[] shaders, Func<int, int> faceShader, float inset = Inset)
    {
        int count = origins.Length;
        float[] positions = new float[count * 12];
        uint[] indices = new uint[count * 6];
        float[] uvs = new float[count * 12];
        int[] faces = new int[count * 2];
        for (int k = 0; k < count; k++)
        {
            float x0 = 2f * k - count + .1f, x1 = x0 + 1.8f, y0 = -.9f, y1 = .9f;
            float[] corners = [x0, y0, 0, x1, y0, 0, x1, y1, 0, x0, y1, 0];
            corners.CopyTo(positions, 12 * k);
            uint b = (uint)(4 * k);
            new[] { b, b + 1, b + 2, b, b + 2, b + 3 }.CopyTo(indices, 6 * k);
            (float u, float v) = origins[k];
            float u0 = u + inset, u1 = u + 1 - inset, v0 = v + inset, v1 = v + 1 - inset;
            float[] corner = [u0, v0, u1, v0, u1, v1, u0, v0, u1, v1, u0, v1];
            corner.CopyTo(uvs, 12 * k);
            faces[2 * k] = faces[2 * k + 1] = faceShader(k);
        }
        MeshNode mesh = scene.CreateMesh(positions, indices, shaders[0]);
        mesh.UsedShaders = shaders;
        mesh.Shader = faces;
        mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, uvs);
        scene.CreateObject(mesh);
        return mesh;
    }

    // An emission of the colour of an image texture read through the standard UV map.
    private static (ShaderNode Shader, ImageTextureNode Texture) Textured(CyclesScene scene, ImageTextureInterpolation interpolation)
    {
        ShaderNode shader = scene.CreateShader();
        UvmapNode uv = shader.Add<UvmapNode>();
        ImageTextureNode texture = shader.Add<ImageTextureNode>();
        texture.Interpolation = interpolation;
        texture.Extension = ImageTextureExtension.Periodic;
        texture.Missing = MissingColor;
        EmissionNode emission = shader.Add<EmissionNode>();
        shader.Connect(uv.UvOutput, texture.VectorInput);
        shader.Connect(texture.ColorOutput, emission.ColorInput);
        shader.Connect(emission.EmissionOutput, shader.Output.SurfaceInput);
        shader.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        return (shader, texture);
    }

    // An emission of a constant colour: the film a texture lookup of that colour must reproduce bit for bit.
    private static ShaderNode Constant(CyclesScene scene, Vector3 color)
    {
        ShaderNode shader = scene.CreateShader();
        EmissionNode emission = shader.Add<EmissionNode>();
        emission.Color = color;
        shader.Connect(emission.EmissionOutput, shader.Output.SurfaceInput);
        shader.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        return shader;
    }

    private static readonly Vector3[] Colors = [new(.8f, .1f, .05f), new(.1f, .7f, .2f), new(.15f, .25f, .9f), new(.6f, .6f, .1f)];

    private static CyclesImage Solid(CyclesScene scene, Vector3 color, uint size = 2) =>
        scene.CreateImage<float>(size, size, ImageFormat.Float4, Enumerable.Range(0, (int)(size * size)).SelectMany(_ => new[] { color.X, color.Y, color.Z, 1f }).ToArray());

    // An 8 x 8 byte4 checker of two colours (distinct per seed).
    private static CyclesImage Checker(CyclesScene scene, int seed) =>
        scene.CreateImage<byte>(8, 8, ImageFormat.Byte4, Enumerable.Range(0, 64).SelectMany(i =>
        {
            bool odd = ((i % 8) + (i / 8)) % 2 == 1;
            return odd ? new byte[] { (byte)(40 + 60 * seed), 200, (byte)(220 - 50 * seed), 255 } : new byte[] { 230, (byte)(30 + 70 * seed), 60, 255 };
        }).ToArray());

    // The film of a strip whose quads have constant emissions of the given colours.
    private static FilmBuffer ConstantFilm(Vector3[] colors)
    {
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        ShaderNode[] shaders = colors.Select(color => Constant(scene, color)).ToArray();
        Strip(scene, colors.Select(_ => (0f, 0f)).ToArray(), shaders, k => k);
        return scene.Render(Settings(colors.Length));
    }

    private static float[] Pixel(FilmBuffer film, int x, int y) => film.Span.Slice((y * film.Width + x) * 4, 4).ToArray();

    [Fact]
    public void solid_tiles_render_each_quad_with_its_tiles_image_bit_for_bit()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        (ShaderNode shader, ImageTextureNode texture) = Textured(scene, ImageTextureInterpolation.Closest);
        using CyclesImage a = Solid(scene, Colors[0]), b = Solid(scene, Colors[1]), c = Solid(scene, Colors[2]), d = Solid(scene, Colors[3]);
        texture.BindTiles([new UdimTile(1001, a), new UdimTile(1002, b), new UdimTile(1003, c), new UdimTile(1011, d)]);
        Strip(scene, [(0, 0), (1, 0), (2, 0), (0, 1)], [shader], _ => 0);

        // The same strip, one image texture per quad with UVs in the unit square (face shader indices pick them).
        using CyclesScene perQuad = TestScenes.CreateCpuScene();
        Configure(perQuad);
        ShaderNode[] shaders = new ShaderNode[4];
        CyclesImage[] images = [.. Colors.Select(color => Solid(perQuad, color))];
        for (int k = 0; k < 4; k++)
        {
            (shaders[k], ImageTextureNode single) = Textured(perQuad, ImageTextureInterpolation.Closest);
            single.Bind(images[k]);
        }
        Strip(perQuad, [(0, 0), (0, 0), (0, 0), (0, 0)], shaders, k => k);

        //Act
        FilmBuffer film = scene.Render(Settings(4));
        FilmBuffer reference = perQuad.Render(Settings(4));
        FilmBuffer constant = ConstantFilm(Colors);

        //Assert
        film.Span.SequenceEqual(reference.Span).Should().BeTrue();
        film.Span.SequenceEqual(constant.Span).Should().BeTrue();
        float[][] centres = [.. Enumerable.Range(0, 4).Select(k => Pixel(film, 10 + 20 * k, 10))];
        centres.Select(p => (p[0], p[1], p[2])).Distinct().Count().Should().Be(4);
        foreach (CyclesImage image in images) image.Dispose();
    }

    [Fact]
    public void checker_tiles_with_linear_interpolation_match_one_image_per_quad()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        (ShaderNode shader, ImageTextureNode texture) = Textured(scene, ImageTextureInterpolation.Linear);
        CyclesImage[] images = [.. Enumerable.Range(0, 3).Select(seed => Checker(scene, seed))];
        texture.BindTiles([new UdimTile(1001, images[0]), new UdimTile(1002, images[1]), new UdimTile(1013, images[2])]);
        Strip(scene, [(0, 0), (1, 0), (2, 1)], [shader], _ => 0, .05f);

        using CyclesScene perQuad = TestScenes.CreateCpuScene();
        Configure(perQuad);
        ShaderNode[] shaders = new ShaderNode[3];
        for (int k = 0; k < 3; k++)
        {
            (shaders[k], ImageTextureNode single) = Textured(perQuad, ImageTextureInterpolation.Linear);
            using CyclesImage image = Checker(perQuad, k);
            single.Bind(image);
        }
        Strip(perQuad, [(0, 0), (0, 0), (0, 0)], shaders, k => k, .05f);

        //Act
        FilmBuffer film = scene.Render(Settings(3));
        FilmBuffer reference = perQuad.Render(Settings(3));
        FilmComparison comparison = FilmComparison.Compare(film.Span, reference.Span);

        //Assert
        // Cycles shifts the UV into its tile (uv - int(uv)); its rounding differs from a UV authored in the unit square, so
        // Linear filtering may differ in the last bits: the practical criterion.
        comparison.PracticalFailures.Should().Be(0);
        film.Span.ToArray().Where((_, i) => i % 4 == 0).Distinct().Count().Should().BeGreaterThan(20);
        foreach (CyclesImage image in images) image.Dispose();
    }

    [Fact]
    public void a_uv_outside_every_bound_tile_reads_the_missing_colour_and_one_image_may_fill_two_tiles()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        (ShaderNode shader, ImageTextureNode texture) = Textured(scene, ImageTextureInterpolation.Closest);
        using CyclesImage a = Solid(scene, Colors[0]), b = Solid(scene, Colors[1]);
        // The same image at tiles 1001 and 1003: Cycles keys a tile's image by its loader's tile number, so it must be two
        // device images; tile 1002 is unbound.
        texture.BindTiles([new UdimTile(1003, a), new UdimTile(1001, a), new UdimTile(1010, b)]);
        // Tiles 1001, 1002 (unbound), 1003, 1010, u >= 10, v <= -1 and u <= -1.
        Strip(scene, [(0, 0), (1, 0), (2, 0), (9, 0), (10, 0), (0, -2), (-2, 0)], [shader], _ => 0);

        //Act
        FilmBuffer film = scene.Render(Settings(7));
        FilmBuffer expected = ConstantFilm([Colors[0], MissingColor, Colors[0], Colors[1], MissingColor, MissingColor, MissingColor]);

        //Assert
        film.Span.SequenceEqual(expected.Span).Should().BeTrue();
        Pixel(film, 50, 10).Should().Equal(Pixel(film, 10, 10));
        Pixel(film, 30, 10).Should().NotEqual(Pixel(film, 10, 10));
    }

    [Fact]
    public void every_invalid_binding_is_refused_by_name_and_changes_nothing()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        (ShaderNode shader, ImageTextureNode texture) = Textured(scene, ImageTextureInterpolation.Closest);
        using CyclesImage a = Solid(scene, Colors[0]), b = Solid(scene, Colors[1]);
        using CyclesImage bytes = scene.CreateImage<byte>(1, 1, ImageFormat.Byte4, [10, 20, 30, 255]);
        texture.BindTiles([new UdimTile(1001, a), new UdimTile(1002, b)]);
        Strip(scene, [(0, 0), (1, 0)], [shader], _ => 0);
        FilmBuffer before = scene.Render(Settings(2));
        EnvironmentTextureNode environment = shader.Add<EnvironmentTextureNode>();
        DiffuseBsdfNode diffuse = shader.Add<DiffuseBsdfNode>();
        Image released = scene.Host.CreateImage(1, 1, ImageFormat.Float4, new byte[16]);
        scene.Host.ReleaseImage(released);
        using CyclesScene other = TestScenes.CreateCpuScene();
        Image foreign = other.Host.CreateImage(1, 1, ImageFormat.Float4, new byte[16]);
        CyclesImage disposed = Solid(scene, Colors[2]);
        disposed.Dispose();
        using CyclesImage foreignImage = Solid(other, Colors[2]);
        NativeFailure Refused(Action bind) { bind.Should().Throw<NativeFailure>(); try { bind(); } catch (NativeFailure failure) { return failure; } return null; }

        //Act
        NativeFailure onEnvironment = Refused(() => scene.Host.BindImageTiles(environment.Handle, [1001], [a.Handle]));
        NativeFailure onOtherNode = Refused(() => scene.Host.BindImageTiles(diffuse.Handle, [1001], [a.Handle]));
        NativeFailure below = Refused(() => texture.BindTiles([new UdimTile(1001, a), new UdimTile(1000, b)]));
        NativeFailure negative = Refused(() => texture.BindTiles([new UdimTile(-1, a)]));
        NativeFailure twice = Refused(() => texture.BindTiles([new UdimTile(1002, a), new UdimTile(1002, b)]));
        NativeFailure formats = Refused(() => texture.BindTiles([new UdimTile(1001, a), new UdimTile(1002, bytes)]));
        NativeFailure none = Refused(() => texture.BindTiles([]));
        NativeFailure releasedImage = Refused(() => scene.Host.BindImageTiles(texture.Handle, [1001, 1002], [a.Handle, released]));
        NativeFailure foreignHandle = Refused(() => scene.Host.BindImageTiles(texture.Handle, [1001], [foreign]));
        Action disposedImage = () => texture.BindTiles([new UdimTile(1001, disposed)]);
        Action otherScene = () => texture.BindTiles([new UdimTile(1001, foreignImage)]);
        Action lengths = () => scene.Host.BindImageTiles(texture.Handle, [1001, 1002], [a.Handle]);
        NativeStatus abiLengths = CyclesAbi.BindImageTiles(0, 0, [1001, 1002], [1]);
        FilmBuffer after = scene.Render(Settings(2));

        //Assert
        onEnvironment.NativeStatus.Should().Be(NativeStatus.Unsupported);
        onEnvironment.Message.Should().Contain("UDIM tiles bind to Image Texture nodes only");
        onOtherNode.NativeStatus.Should().Be(NativeStatus.Invalid);
        onOtherNode.Message.Should().Contain("UDIM tiles bind to an Image Texture node");
        below.NativeStatus.Should().Be(NativeStatus.Invalid);
        below.Message.Should().Contain("UDIM tile number 1000 is outside 1001 to 2147483647");
        negative.Message.Should().Contain("UDIM tile number 4294967295 is outside 1001 to 2147483647");
        twice.NativeStatus.Should().Be(NativeStatus.Invalid);
        twice.Message.Should().Contain("UDIM tile 1002 is listed twice");
        formats.NativeStatus.Should().Be(NativeStatus.Invalid);
        formats.Message.Should().Contain("The image of UDIM tile 1002 has another format than the image of tile 1001");
        none.NativeStatus.Should().Be(NativeStatus.Invalid);
        none.Message.Should().Contain("UDIM tiles need at least one tile");
        releasedImage.NativeStatus.Should().Be(NativeStatus.Invalid);
        releasedImage.Message.Should().Contain("Invalid, released or foreign image handle for UDIM tile 1002");
        foreignHandle.Message.Should().Contain("Invalid, released or foreign image handle for UDIM tile 1001");
        disposedImage.Should().Throw<ObjectDisposedException>();
        otherScene.Should().Throw<ArgumentException>();
        lengths.Should().Throw<ArgumentException>();
        abiLengths.Should().Be(NativeStatus.Invalid);
        after.Span.SequenceEqual(before.Span).Should().BeTrue();
    }

    [Fact]
    public void edits_after_a_render_apply_like_a_new_scene()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        (ShaderNode shader, ImageTextureNode texture) = Textured(scene, ImageTextureInterpolation.Closest);
        using CyclesImage a = Solid(scene, Colors[0]), b = Solid(scene, Colors[1]), c = Solid(scene, Colors[2]);
        texture.BindTiles([new UdimTile(1001, a), new UdimTile(1002, b), new UdimTile(1003, c)]);
        Strip(scene, [(0, 0), (1, 0), (2, 0)], [shader], _ => 0);
        FilmBuffer first = scene.Render(Settings(3));

        //Act
        // A member image changes its pixels (and size): the next render reads them.
        b.Update<float>(1, 1, ImageFormat.Float4, [Colors[3].X, Colors[3].Y, Colors[3].Z, 1f]);
        FilmBuffer updated = scene.Render(Settings(3));
        // An update that would leave the tiles with two formats is refused, and nothing changes.
        Action mixed = () => c.Update<byte>(1, 1, ImageFormat.Byte4, [1, 2, 3, 255]);
        mixed.Should().Throw<NativeFailure>().Which.Message.Should().Contain("would differ from the format of UDIM tile 1001");
        FilmBuffer afterRefusal = scene.Render(Settings(3));
        // A new binding replaces the tiles, a plain Bind replaces them with one image, Unbind removes the binding.
        texture.BindTiles([new UdimTile(1003, a), new UdimTile(1002, c)]);
        FilmBuffer rebound = scene.Render(Settings(3));
        texture.Bind(c);
        FilmBuffer plain = scene.Render(Settings(3));
        texture.Unbind();
        FilmBuffer unbound = scene.Render(Settings(3));
        texture.BindTiles([new UdimTile(1001, a), new UdimTile(1002, b), new UdimTile(1003, c)]);
        FilmBuffer again = scene.Render(Settings(3));

        //Assert
        first.Span.SequenceEqual(ConstantFilm([Colors[0], Colors[1], Colors[2]]).Span).Should().BeTrue();
        updated.Span.SequenceEqual(ConstantFilm([Colors[0], Colors[3], Colors[2]]).Span).Should().BeTrue();
        afterRefusal.Span.SequenceEqual(updated.Span).Should().BeTrue();
        rebound.Span.SequenceEqual(ConstantFilm([MissingColor, Colors[2], Colors[0]]).Span).Should().BeTrue();
        // One image: the UV's integer part is not removed, so Periodic extension wraps it into the image.
        plain.Span.SequenceEqual(ConstantFilm([Colors[2], Colors[2], Colors[2]]).Span).Should().BeTrue();
        unbound.Span.SequenceEqual(ConstantFilm([MissingColor, MissingColor, MissingColor]).Span).Should().BeTrue();
        again.Span.SequenceEqual(updated.Span).Should().BeTrue();
    }

    [Fact]
    public void one_image_at_every_tile_may_change_its_format()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        (ShaderNode shader, ImageTextureNode texture) = Textured(scene, ImageTextureInterpolation.Closest);
        using CyclesImage a = Solid(scene, Colors[0]);
        texture.BindTiles([new UdimTile(1001, a), new UdimTile(1002, a)]);
        Strip(scene, [(0, 0), (1, 0)], [shader], _ => 0);
        scene.Render(Settings(2));

        //Act
        a.Update<Half>(1, 1, ImageFormat.Half4, [(Half).5f, (Half).25f, (Half).125f, (Half)1f]);
        FilmBuffer film = scene.Render(Settings(2));

        //Assert
        film.Span.SequenceEqual(ConstantFilm([new(.5f, .25f, .125f), new(.5f, .25f, .125f)]).Span).Should().BeTrue();
    }

    // The node's tiles socket (provisional refusal) is Cycles' list of UDIM tiles read from image files, which this library
    // does not read: a set that would change it is refused by name, pointing to BindTiles (cbc_host_image_bind_tiles); its
    // default, the empty list, reads back and may be set again, before and after a render, and the bound tiles render on.
    [Fact]
    public void a_change_of_the_file_name_tiles_socket_is_refused_by_name_and_its_default_stays_accepted()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        (ShaderNode shader, ImageTextureNode texture) = Textured(scene, ImageTextureInterpolation.Closest);
        using CyclesImage a = Solid(scene, Colors[0]), b = Solid(scene, Colors[1]);
        texture.BindTiles([new UdimTile(1001, a), new UdimTile(1002, b)]);
        Strip(scene, [(0, 0), (1, 0)], [shader], _ => 0);
        const string why = "The Image Texture node's tiles socket is Cycles' list of UDIM tiles read from image files, which this "
            + "library does not read; bind images as UDIM tiles with cbc_host_image_bind_tiles";
        Action typed = () => texture.Tiles = [1001, 1002];
        Action byName = () => scene.Host.SetInts(texture.Handle, "tiles", [1001L]);
        ImageTextureNode unbound = scene.CreateShader().Add<ImageTextureNode>();
        Action onUnbound = () => unbound.Tiles = [1001];

        //Act
        int[] initial = texture.Tiles;
        texture.Tiles = [];
        scene.Host.SetInts(texture.Handle, "tiles", []);
        FilmBuffer film = scene.Render(Settings(2));
        texture.Tiles = [];

        //Assert
        initial.Should().BeEmpty();
        NativeFailure refused = typed.Should().Throw<NativeFailure>().Which;
        refused.NativeStatus.Should().Be(NativeStatus.Invalid);
        refused.Message.Should().Contain(why);
        byName.Should().Throw<NativeFailure>().Which.Message.Should().Contain(why);
        onUnbound.Should().Throw<NativeFailure>().Which.Message.Should().Contain(why);
        texture.Tiles.Should().BeEmpty();
        unbound.Tiles.Should().BeEmpty();
        film.Span.SequenceEqual(ConstantFilm([Colors[0], Colors[1]]).Span).Should().BeTrue();
        scene.Render(Settings(2)).Span.SequenceEqual(film.Span).Should().BeTrue();
        TestScenes.RequireNativeHost();
        CyclesAbi.Create(Backend.CpuReference, 1, out ulong host).Should().Be(NativeStatus.Ok);
        try
        {
            CyclesAbi.CreateNode(host, "shader", out ulong graph).Should().Be(NativeStatus.Ok);
            CyclesAbi.ShaderNode(host, graph, "image_texture", out ulong node).Should().Be(NativeStatus.Ok);
            CyclesAbi.SetInts(host, node, "tiles", [1001L]).Should().Be(NativeStatus.Invalid);
            CyclesAbi.LastError().Should().Contain(why);
            CyclesAbi.SetInts(host, node, "tiles", []).Should().Be(NativeStatus.Ok);
        }
        finally
        {
            CyclesAbi.Destroy(host);
        }
    }

    [Fact]
    public void the_status_code_tier_binds_tiles_and_reports_a_refusal_through_the_last_error()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        CyclesAbi.Create(Backend.CpuReference, 1, out ulong host).Should().Be(NativeStatus.Ok);
        try
        {
            CyclesAbi.CreateNode(host, "shader", out ulong shader).Should().Be(NativeStatus.Ok);
            CyclesAbi.ShaderNode(host, shader, "image_texture", out ulong texture).Should().Be(NativeStatus.Ok);
            CyclesAbi.CreateImage(host, 1, 1, ImageFormat.Byte4, [1, 2, 3, 255], out ulong image).Should().Be(NativeStatus.Ok);

            //Act
            NativeStatus bound = CyclesAbi.BindImageTiles(host, texture, [1002, 1001], [image, image]);
            NativeStatus refused = CyclesAbi.BindImageTiles(host, texture, [999], [image]);
            string message = CyclesAbi.LastError();

            //Assert
            bound.Should().Be(NativeStatus.Ok);
            refused.Should().Be(NativeStatus.Invalid);
            message.Should().Contain("UDIM tile number 999 is outside 1001 to 2147483647");
        }
        finally
        {
            CyclesAbi.Destroy(host);
        }
    }
}
