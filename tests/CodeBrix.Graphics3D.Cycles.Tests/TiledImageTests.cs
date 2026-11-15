// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.GltfRender;
using CodeBrix.Graphics3D.Cycles.Rendering;
using CodeBrix.Imaging.PixelFormats;
using SilverAssertions;
using Xunit;
using ImagingImage = CodeBrix.Imaging.Image;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Tiled images (provisional CyclesScene.CreateTiledImage, cbc_host_image_create_tiled): Cycles' texture cache, fed by an
// ITiledImageReader, on the CPU reference backend. The scenes are quads seen by an orthographic camera, shaded by an
// emission of an image texture read through its UVs, one sample: the film shows the texel each pixel's sample read, so a
// tiled image is compared with the same pixels bound as a plain image (CreateImage), and its mip levels with plain images
// of each level's colour.
public class TiledImageTests(ITestOutputHelper output)
{
    // ---- Readers -------------------------------------------------------------------------------------------------------

    // Serves the levels of an image held in memory (rows bottom first, one array per level) and records every call.
    internal sealed class LevelReader : ITiledImageReader
    {
        private readonly uint imageWidth, imageHeight;
        private readonly int pixelBytes, tileSize;
        private readonly byte[][] levels;
        private int inside;

        public readonly ConcurrentQueue<(int Level, int X, int Y, int Width, int Height, int Thread)> Reads = new();
        public volatile bool Overlapped;
        public volatile int DelayMilliseconds;
        public Func<int, int, int, int, int, Exception> Fail;
        public Action Inside;

        public LevelReader(uint width, uint height, int bytesPerPixel, byte[][] imageLevels, int imageTileSize = TiledImage.TileSize)
        {
            imageWidth = width;
            imageHeight = height;
            pixelBytes = bytesPerPixel;
            levels = imageLevels;
            tileSize = imageTileSize;
        }

        public byte[][] Levels() => levels;

        public void Replace(int level, byte[] pixels) => levels[level] = pixels;

        public void ReadTile(int level, int x, int y, int width, int height, Span<byte> destination, int rowBytes)
        {
            if (Interlocked.Increment(ref inside) != 1) Overlapped = true;
            try
            {
                Reads.Enqueue((level, x, y, width, height, Environment.CurrentManagedThreadId));
                if (DelayMilliseconds > 0) Thread.Sleep(DelayMilliseconds);
                Inside?.Invoke();
                Exception failure = Fail?.Invoke(level, x, y, width, height);
                if (failure != null) throw failure;
                (uint levelWidth, uint levelHeight) = TiledImage.LevelSize(imageWidth, imageHeight, level, tileSize);
                if (x < 0 || y < 0 || x + width > levelWidth || y + height > levelHeight) throw new InvalidOperationException("The rectangle is outside the level.");
                for (int row = 0; row < height; row++)
                    levels[level].AsSpan(((y + row) * (int)levelWidth + x) * pixelBytes, width * pixelBytes).CopyTo(destination[(row * rowBytes)..]);
            }
            finally
            {
                Interlocked.Decrement(ref inside);
            }
        }
    }

    // ---- Images --------------------------------------------------------------------------------------------------------

    // A byte4 pattern of distinct texels: rows bottom first.
    internal static byte[] Pattern(uint width, uint height)
    {
        byte[] pixels = new byte[width * height * 4];
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
            {
                uint hash = unchecked((uint)(x * 73856093) ^ (uint)(y * 19349663));
                hash = unchecked(hash * 2654435761u);
                int at = (int)((y * width + x) * 4);
                pixels[at] = (byte)(hash >> 24);
                pixels[at + 1] = (byte)(hash >> 16);
                pixels[at + 2] = (byte)(x * 255 / Math.Max(1, (int)width - 1));
                pixels[at + 3] = 255;
            }
        return pixels;
    }

    // The levels of a byte image by a 2 x 2 box reduction, as Cycles makes its own (the reader's choice, not the library's).
    internal static byte[][] BoxLevels(byte[] level0, uint width, uint height, int channels, int tileSize = TiledImage.TileSize)
    {
        int count = TiledImage.LevelCount(width, height, tileSize);
        byte[][] levels = new byte[count][];
        levels[0] = level0;
        for (int level = 1; level < count; level++)
        {
            (uint sw, uint sh) = TiledImage.LevelSize(width, height, level - 1, tileSize);
            (uint dw, uint dh) = TiledImage.LevelSize(width, height, level, tileSize);
            byte[] source = levels[level - 1], target = new byte[dw * dh * channels];
            for (int y = 0; y < dh; y++)
                for (int x = 0; x < dw; x++)
                    for (int c = 0; c < channels; c++)
                    {
                        int sum = 0;
                        for (int j = 0; j < 2; j++)
                            for (int i = 0; i < 2; i++)
                                sum += source[(Math.Min(2 * y + j, (int)sh - 1) * sw + Math.Min(2 * x + i, (int)sw - 1)) * channels + c];
                        target[(y * dw + x) * channels + c] = (byte)((sum + 2) / 4);
                    }
            levels[level] = target;
        }
        return levels;
    }

    // Every level one solid colour (byte4).
    private static readonly byte[][] LevelColors = [[230, 40, 30, 255], [40, 200, 60, 255], [50, 70, 220, 255], [240, 220, 40, 255], [200, 60, 220, 255]];

    internal static byte[][] SolidLevels(uint width, uint height, int tileSize = TiledImage.TileSize)
    {
        int count = TiledImage.LevelCount(width, height, tileSize);
        return [.. Enumerable.Range(0, count).Select(level =>
        {
            (uint w, uint h) = TiledImage.LevelSize(width, height, level, tileSize);
            return Enumerable.Range(0, (int)(w * h)).SelectMany(_ => LevelColors[level]).ToArray();
        })];
    }

    private static LevelReader Reader(uint width, uint height, byte[][] levels) => new(width, height, 4, levels);

    // ---- Scenes --------------------------------------------------------------------------------------------------------

    internal static void Configure(CyclesScene scene)
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

    private static (ShaderNode Shader, ImageTextureNode Texture) Material(CyclesScene scene, ImageTextureInterpolation interpolation, ImageTextureExtension extension)
    {
        ShaderNode shader = scene.CreateShader();
        UvmapNode uv = shader.Add<UvmapNode>();
        ImageTextureNode texture = shader.Add<ImageTextureNode>();
        texture.Interpolation = interpolation;
        texture.Extension = extension;
        EmissionNode emission = shader.Add<EmissionNode>();
        shader.Connect(uv.UvOutput, texture.VectorInput);
        shader.Connect(texture.ColorOutput, emission.ColorInput);
        shader.Connect(emission.EmissionOutput, shader.Output.SurfaceInput);
        shader.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        return (shader, texture);
    }

    // One quad filling the orthographic view (-1..1 for a square film), with the UVs [u0, u1] x [v0, v1].
    internal static ImageTextureNode Quad(CyclesScene scene, float u0, float v0, float u1, float v1,
        ImageTextureInterpolation interpolation = ImageTextureInterpolation.Closest, ImageTextureExtension extension = ImageTextureExtension.Clamp) =>
        QuadObject(scene, u0, v0, u1, v1, interpolation, extension).Texture;

    internal static (ImageTextureNode Texture, ObjectNode Object) QuadObject(CyclesScene scene, float u0, float v0, float u1, float v1,
        ImageTextureInterpolation interpolation = ImageTextureInterpolation.Closest, ImageTextureExtension extension = ImageTextureExtension.Clamp)
    {
        (ShaderNode shader, ImageTextureNode texture) = Material(scene, interpolation, extension);
        MeshNode mesh = scene.CreateMesh([-1f, -1f, 0, 1f, -1f, 0, 1f, 1f, 0, -1f, 1f, 0], [0, 1, 2, 0, 2, 3], shader);
        mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, [u0, v0, u1, v0, u1, v1, u0, v0, u1, v1, u0, v1]);
        return (texture, scene.CreateObject(mesh));
    }

    // Four adjacent quads, each 64 x 64 pixels of a 256 x 64 film (the view spans -4..4 by -1..1), quad k with its own
    // texture node and the UVs [0, extents[k]]^2: an orthographic view of a 512-pixel image at 0.5, 2, 4 and 8 texels per
    // pixel selects mip levels 0, 1, 2 and 3 (kernel_image_tile_map: log2 of the footprint, plus 0.5 plus or minus 0.25).
    private static readonly float[] StripExtents = [.0625f, .25f, .5f, 1f];
    internal static readonly RenderSettings StripSettings = new(256, 64, 1);

    internal static ImageTextureNode[] Strip(CyclesScene scene, float[] extents = null)
    {
        extents ??= StripExtents;
        ImageTextureNode[] textures = new ImageTextureNode[4];
        float[] positions = new float[48];
        uint[] indices = new uint[24];
        float[] uvs = new float[48];
        ShaderNode[] shaders = new ShaderNode[4];
        int[] faces = new int[8];
        for (int k = 0; k < 4; k++)
        {
            (shaders[k], textures[k]) = Material(scene, ImageTextureInterpolation.Closest, ImageTextureExtension.Clamp);
            float x0 = 2f * k - 4f, x1 = x0 + 2f;
            new[] { x0, -1f, 0, x1, -1f, 0, x1, 1f, 0, x0, 1f, 0 }.CopyTo(positions, 12 * k);
            uint b = (uint)(4 * k);
            new[] { b, b + 1, b + 2, b, b + 2, b + 3 }.CopyTo(indices, 6 * k);
            float e = extents[k];
            new[] { 0f, 0f, e, 0f, e, e, 0f, 0f, e, e, 0f, e }.CopyTo(uvs, 12 * k);
            faces[2 * k] = faces[2 * k + 1] = k;
        }
        MeshNode mesh = scene.CreateMesh(positions, indices, shaders[0]);
        mesh.UsedShaders = shaders;
        mesh.Shader = faces;
        mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, uvs);
        scene.CreateObject(mesh);
        return textures;
    }

    // The strip with each quad bound to a plain 64 x 64 image of one level colour (on the CPU reference, or in scene).
    private static FilmBuffer StripOfColors(params int[] levels) => StripOfColors(null, levels);

    internal static FilmBuffer StripOfColors(CyclesScene target, params int[] levels)
    {
        using CyclesScene owned = target == null ? TestScenes.CreateCpuScene() : null;
        CyclesScene scene = target ?? owned;
        Configure(scene);
        ImageTextureNode[] textures = Strip(scene);
        for (int k = 0; k < 4; k++)
        {
            using CyclesImage solid = scene.CreateImage<byte>(64, 64, ImageFormat.Byte4, Enumerable.Range(0, 64 * 64).SelectMany(_ => LevelColors[levels[k]]).ToArray());
            textures[k].Bind(solid);
        }
        return scene.Render(StripSettings);
    }

    private static int TotalTiles(uint width, uint height) => Enumerable.Range(0, TiledImage.LevelCount(width, height)).Sum(level =>
    {
        (uint w, uint h) = TiledImage.LevelSize(width, height, level);
        return (int)(((w + 63) / 64) * ((h + 63) / 64));
    });

    // ---- Tests ---------------------------------------------------------------------------------------------------------

    [Fact]
    public void level_count_and_sizes_follow_cycles_texture_cache()
    {
        //Act / Assert
        TiledImage.LevelCount(512, 512).Should().Be(4);
        TiledImage.LevelCount(64, 64).Should().Be(1);
        TiledImage.LevelCount(48, 48).Should().Be(1);
        TiledImage.LevelCount(65, 1).Should().Be(2);
        TiledImage.LevelCount(8192, 64).Should().Be(8);
        TiledImage.LevelSize(8192, 64, 7).Should().Be((64u, 1u));
        TiledImage.LevelSize(1000, 3, 2).Should().Be((250u, 1u));
        ((Action)(() => TiledImage.LevelSize(512, 512, 4))).Should().Throw<ArgumentOutOfRangeException>();
        ((Action)(() => TiledImage.LevelCount(0, 4))).Should().Throw<ArgumentOutOfRangeException>();
    }

    // ---- Tile sizes (provisional) -------------------------------------------------------------------------------------

    // Every tile size a tiled image may choose on the CPU backends: the powers of two from 64 to 1024 (TiledImage.MaxTileSize;
    // Cycles allocates the tile pool of a format whole at its first tile load, 64 tiles of (size + 4)^2 pixels, 271 MB for
    // byte4 tiles of 1024). The images below are 8 rows high, so a tile is t x 8 texels at every size.
    public static TheoryData<int> CpuTileSizes => [.. Enumerable.Range(6, 5).Select(shift => 1 << shift)];

    // The strip's UV extents for an 8t x 8 image at tile size t: Cycles limits the footprint's anisotropy to 16
    // (kernel_image_tile_map), so with 8 rows the u footprint decides, e * 8t / 64 texels per pixel over 16, and
    // e_k = 2^(k + 7) / t makes it 2^k: mip level k of the image's four (8t, 4t, 2t and t wide).
    internal static float[] TileSizeStripExtents(int tileSize) => [.. Enumerable.Range(0, 4).Select(k => (float)((128.0 * (1 << k)) / tileSize))];

    // The strip at tile size t: an 8t x 8 image whose four levels are four solid colours, bound to the four quads.
    internal static LevelReader TileSizeStrip(CyclesScene scene, int tileSize)
    {
        uint width = (uint)(8 * tileSize), height = 8;
        Configure(scene);
        ImageTextureNode[] textures = Strip(scene, TileSizeStripExtents(tileSize));
        LevelReader reader = new(width, height, 4, SolidLevels(width, height, tileSize), tileSize);
        using CyclesImage image = scene.CreateTiledImage(width, height, ImageFormat.Byte4, reader, (uint)tileSize);
        foreach (ImageTextureNode texture in textures) texture.Bind(image);
        return reader;
    }

    // A (2t + 64) x 8 byte4 pattern magnified (half a texel per pixel: level 0) across the edge of tiles 0 and 1 at texel t,
    // as a tiled image of tile size t or bound plainly (reader null).
    internal static (FilmBuffer Film, LevelReader Reader) TileSizePattern(CyclesScene scene, int tileSize, bool tiled)
    {
        uint width = (uint)(2 * tileSize + 64), height = 8;
        byte[] pixels = Pattern(width, height);
        Configure(scene);
        ImageTextureNode texture = Quad(scene, (tileSize - 32f) / width, 0f, (tileSize + 32f) / width, 1f);
        LevelReader reader = tiled ? new(width, height, 4, BoxLevels(pixels, width, height, 4, tileSize), tileSize) : null;
        using (CyclesImage image = tiled ? scene.CreateTiledImage(width, height, ImageFormat.Byte4, reader, (uint)tileSize)
                                         : scene.CreateImage<byte>(width, height, ImageFormat.Byte4, pixels))
            texture.Bind(image);
        return (scene.Render(new RenderSettings(128, 128, 1)), reader);
    }

    // Provisional tile sizes, at every size the CPU backends take: Cycles selects the mip levels from the footprint and
    // reads them, in tiles of the chosen size, through the reader, and the film equals plain images of the level colours
    // bit for bit.
    [Theory]
    [MemberData(nameof(CpuTileSizes))]
    public void at_every_tile_size_mip_levels_are_selected_by_the_footprint_and_read_in_tiles_of_that_size(int tileSize)
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        LevelReader reader = TileSizeStrip(scene, tileSize);

        //Act
        FilmBuffer film = scene.Render(StripSettings);
        output.WriteLine($"tile size {tileSize}: {reader.Reads.Count} reads, widest {reader.Reads.Max(r => r.Width)}; per level " +
            string.Join(", ", reader.Reads.GroupBy(r => r.Level).OrderBy(g => g.Key).Select(g => $"{g.Key}: {g.Count()}")));

        //Assert
        TiledImage.LevelCount((uint)(8 * tileSize), 8, tileSize).Should().Be(4);
        film.Span.SequenceEqual(StripOfColors(0, 1, 2, 3).Span).Should().BeTrue();
        reader.Reads.Select(r => r.Level).Distinct().Order().Should().Equal(0, 1, 2, 3);
        // A tile and its 2-pixel border: wider than the tile size, at most 4 more.
        reader.Reads.Max(r => r.Width).Should().BeInRange(tileSize + 1, tileSize + 4);
        reader.Overlapped.Should().BeFalse();
    }

    // Provisional tile sizes: the same pixels as a tiled image of each tile size the CPU backends take and as a plain
    // image render bit for bit (Closest, across the edge between the first two tiles of level 0).
    [Theory]
    [MemberData(nameof(CpuTileSizes))]
    public void at_every_tile_size_a_tiled_image_renders_like_the_same_pixels_bound_plainly(int tileSize)
    {
        //Arrange
        using CyclesScene plainScene = TestScenes.CreateCpuScene();
        using CyclesScene tiledScene = TestScenes.CreateCpuScene();

        //Act
        (FilmBuffer plain, _) = TileSizePattern(plainScene, tileSize, false);
        (FilmBuffer tiled, LevelReader reader) = TileSizePattern(tiledScene, tileSize, true);

        //Assert
        plain.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(100);
        tiled.Span.SequenceEqual(plain.Span).Should().BeTrue();
        reader.Reads.All(r => r.Level == 0).Should().BeTrue();
        // Tiles 0 and 1 of level 0, each read from 2 pixels before it (its border).
        reader.Reads.Select(r => (r.X + 2) / tileSize).Distinct().Count().Should().BeGreaterThanOrEqualTo(2);
        reader.Reads.Max(r => r.Width).Should().BeInRange(tileSize + 1, tileSize + 4);
    }

    // Provisional tile sizes: 0 and 64 are the default; any size the CPU backends do not take (not a power of two, below 64,
    // or above 1024, the CPU's cap) is refused by name when the image is created, and the level helpers follow the tile size.
    [Fact]
    public void tile_sizes_the_cpu_backends_do_not_take_are_refused_by_name()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        uint[] refused = [1, 8, 32, 63, 65, 96, 100, 1000, 1025, 2048, 4096, 65535, 65536, 65537, 131072, 1u << 20, 1u << 31, uint.MaxValue];
        LevelReader reader = Reader(256, 256, SolidLevels(256, 256));

        //Act / Assert
        foreach (uint size in refused)
        {
            Action create = () => scene.CreateTiledImage(256, 256, ImageFormat.Byte4, reader, size);
            NativeFailure error = create.Should().Throw<NativeFailure>().Which;
            error.NativeStatus.Should().Be(NativeStatus.Invalid);
            error.Message.Should().Contain($"Invalid tiled image tile size {size}: 0 (the default, 64) or a power of two from 64 to 1024");
        }
        using (CyclesImage image = scene.CreateTiledImage(256, 256, ImageFormat.Byte4, reader)) image.IsTiled.Should().BeTrue();
        TiledImage.LevelCount(512, 512, 128).Should().Be(3);
        TiledImage.LevelCount(512, 512, 512).Should().Be(1);
        TiledImage.LevelCount(200, 100, 256).Should().Be(1);
        TiledImage.LevelCount(1u << 20, 8, 1024).Should().Be(11);
        using (CyclesImage largest = scene.CreateTiledImage(256, 256, ImageFormat.Byte4, reader, 1024)) largest.IsTiled.Should().BeTrue();
        TiledImage.LevelSize(1000, 600, 2, 128).Should().Be((250u, 150u));
        foreach (int size in new[] { 32, 63, 96, 2048, 65536, 131072 })
            ((Action)(() => TiledImage.LevelCount(512, 512, size))).Should().Throw<ArgumentOutOfRangeException>();
        ((Action)(() => TiledImage.LevelSize(512, 512, 3, 128))).Should().Throw<ArgumentOutOfRangeException>();
    }

    // The same pixels as a tiled image and as a plain image, magnified (half a texel per pixel: mip level 0) across tile
    // edges (texels 64 and 128) and the image's lower-left corner, for every extension and interpolation. Closest reads the
    // same texel: bit for bit. Linear and Cubic read the tile's 2-pixel padding across tile and image edges, where the
    // tiled path's pixel coordinates (tile offsets added, rows flipped) round differently: measured and held to the
    // practical criterion. Outside the image (UVs below 0, the film's lower and left 64 pixels) Cycles' tiled path wraps
    // the UV before it filters (kernel_image_tile_wrap: Clip returns zero beyond [0, 1], Extend clamps the UV into it),
    // where the plain path wraps each texel, so Linear with Clip and Cubic with Clip or Extend differ there by Cycles'
    // design, not the loader's: for those three the criterion holds inside the image, and the outside is reported.
    [Theory]
    [InlineData(ImageTextureInterpolation.Closest, ImageTextureExtension.Clamp)]
    [InlineData(ImageTextureInterpolation.Closest, ImageTextureExtension.Periodic)]
    [InlineData(ImageTextureInterpolation.Closest, ImageTextureExtension.Mirror)]
    [InlineData(ImageTextureInterpolation.Closest, ImageTextureExtension.Black)]
    [InlineData(ImageTextureInterpolation.Linear, ImageTextureExtension.Clamp)]
    [InlineData(ImageTextureInterpolation.Linear, ImageTextureExtension.Periodic)]
    [InlineData(ImageTextureInterpolation.Linear, ImageTextureExtension.Mirror)]
    [InlineData(ImageTextureInterpolation.Linear, ImageTextureExtension.Black)]
    [InlineData(ImageTextureInterpolation.Cubic, ImageTextureExtension.Clamp)]
    [InlineData(ImageTextureInterpolation.Cubic, ImageTextureExtension.Periodic)]
    [InlineData(ImageTextureInterpolation.Cubic, ImageTextureExtension.Mirror)]
    [InlineData(ImageTextureInterpolation.Cubic, ImageTextureExtension.Black)]
    public void a_tiled_image_renders_like_the_same_pixels_bound_as_a_plain_image(ImageTextureInterpolation interpolation, ImageTextureExtension extension)
    {
        //Arrange
        const uint size = 256;
        byte[] pixels = Pattern(size, size);
        RenderSettings settings = new(384, 384, 1);
        FilmBuffer Render(bool tiled, out LevelReader reader)
        {
            using CyclesScene scene = TestScenes.CreateCpuScene();
            Configure(scene);
            ImageTextureNode texture = Quad(scene, -.125f, -.125f, .625f, .625f, interpolation, extension);
            reader = tiled ? Reader(size, size, BoxLevels(pixels, size, size, 4)) : null;
            using CyclesImage image = tiled ? scene.CreateTiledImage(size, size, ImageFormat.Byte4, reader) : scene.CreateImage<byte>(size, size, ImageFormat.Byte4, pixels);
            texture.Bind(image);
            return scene.Render(settings);
        }

        //Act
        FilmBuffer plain = Render(false, out _);
        FilmBuffer tiled = Render(true, out LevelReader reader);
        FilmComparison comparison = FilmComparison.Compare(tiled.Span, plain.Span);
        output.WriteLine($"{interpolation}, {extension}: tiled vs plain image: {comparison}; {reader.Reads.Count} reads, levels {string.Join(",", reader.Reads.Select(r => r.Level).Distinct().Order())}");
        // The film's pixels whose sample footprints lie inside the image: right of and above pixel 64 (UV 0), one pixel spare.
        float[] Inside(FilmBuffer film) => [.. Enumerable.Range(65 * 384, (384 - 65) * 384).Where(pixel => pixel % 384 >= 65)
            .SelectMany(pixel => film.Span.Slice(pixel * 4, 4).ToArray())];
        FilmComparison inside = FilmComparison.Compare(Inside(tiled), Inside(plain));
        output.WriteLine($"{interpolation}, {extension}: inside the image: {inside}");
        bool wrappedBeforeFiltering = interpolation != ImageTextureInterpolation.Closest &&
            (extension == ImageTextureExtension.Black || (extension == ImageTextureExtension.Clamp && interpolation == ImageTextureInterpolation.Cubic));

        //Assert
        reader.Reads.Should().NotBeEmpty();
        reader.Reads.All(r => r.Level == 0).Should().BeTrue();
        reader.Overlapped.Should().BeFalse();
        plain.Span.ToArray().Where((value, index) => index % 4 == 0).Distinct().Count().Should().BeGreaterThan(100);
        if (interpolation == ImageTextureInterpolation.Closest) comparison.BitwiseEqual.Should().BeTrue();
        else if (wrappedBeforeFiltering) inside.PracticalFailures.Should().Be(0);
        else comparison.PracticalFailures.Should().Be(0);
    }

    // Mip levels: a 512 x 512 image whose four levels are four solid colours, seen at 0.5, 2, 4 and 8 texels per pixel,
    // renders exactly like the strip with each quad bound to a plain image of the colour of level 0, 1, 2 and 3: Cycles
    // selects the levels from the ray footprint and reads each through the reader.
    [Fact]
    public void mip_levels_are_requested_and_served_and_selected_by_the_footprint()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        ImageTextureNode[] textures = Strip(scene);
        LevelReader reader = Reader(512, 512, SolidLevels(512, 512));
        using (CyclesImage image = scene.CreateTiledImage(512, 512, ImageFormat.Byte4, reader))
            foreach (ImageTextureNode texture in textures) texture.Bind(image);

        //Act
        FilmBuffer film = scene.Render(StripSettings);
        int afterFirst = reader.Reads.Count;
        FilmBuffer again = scene.Render(StripSettings);
        Thread.Sleep(50);

        //Assert
        film.Span.SequenceEqual(StripOfColors(0, 1, 2, 3).Span).Should().BeTrue();
        again.Span.SequenceEqual(film.Span).Should().BeTrue();
        reader.Reads.Select(r => r.Level).Distinct().Order().Should().Equal(0, 1, 2, 3);
        // Only the sampled tiles (and the level-0 tile at the origin) are read, of the 85 the four levels have.
        afterFirst.Should().BeLessThan(TotalTiles(512, 512));
        output.WriteLine($"reads: {afterFirst} of {TotalTiles(512, 512)} tiles; per level {string.Join(", ", reader.Reads.GroupBy(r => r.Level).OrderBy(g => g.Key).Select(g => $"{g.Key}: {g.Count()}"))}");
        // The cache keeps the tiles: the second render reads nothing, and nothing is read after Render returns.
        reader.Reads.Count.Should().Be(afterFirst);
        reader.Overlapped.Should().BeFalse();
        foreach ((int level, int x, int y, int width, int height, _) in reader.Reads)
        {
            (uint w, uint h) = TiledImage.LevelSize(512, 512, level);
            (x + width <= w && y + height <= h && width > 0 && height > 0).Should().BeTrue();
        }
    }

    // Cycles' texture limit skips the top mip levels of a tiled image (miplevel_offset): under a limit of 256 the reader is
    // never asked for level 0, and each quad shows the level one below.
    [Fact]
    public void a_texture_limit_skips_the_top_levels_instead_of_reading_them()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        scene.TextureLimit = 256;
        ImageTextureNode[] textures = Strip(scene);
        LevelReader reader = Reader(512, 512, SolidLevels(512, 512));
        using (CyclesImage image = scene.CreateTiledImage(512, 512, ImageFormat.Byte4, reader))
            foreach (ImageTextureNode texture in textures) texture.Bind(image);

        //Act
        FilmBuffer film = scene.Render(StripSettings);

        //Assert
        reader.Reads.Should().NotBeEmpty();
        reader.Reads.Any(r => r.Level == 0).Should().BeFalse();
        film.Span.SequenceEqual(StripOfColors(1, 1, 2, 3).Span).Should().BeTrue();
    }

    // An image smaller than a 64-pixel block in both sides is a plain image to Cycles (ImageMetaData::detect_tiles): one
    // read of the whole of level 0, and the film of the same pixels bound plainly, bit for bit.
    [Fact]
    public void an_image_smaller_than_a_tile_is_read_once_whole()
    {
        //Arrange
        byte[] pixels = Pattern(48, 40);
        FilmBuffer Render(Func<CyclesScene, CyclesImage> create)
        {
            using CyclesScene scene = TestScenes.CreateCpuScene();
            Configure(scene);
            ImageTextureNode texture = Quad(scene, 0, 0, 1, 1, ImageTextureInterpolation.Linear);
            using CyclesImage image = create(scene);
            texture.Bind(image);
            return scene.Render(new RenderSettings(64, 64, 1));
        }
        LevelReader reader = new(48, 40, 4, [pixels]);

        //Act
        FilmBuffer tiled = Render(scene => scene.CreateTiledImage(48, 40, ImageFormat.Byte4, reader));
        FilmBuffer plain = Render(scene => scene.CreateImage<byte>(48, 40, ImageFormat.Byte4, pixels));

        //Assert
        reader.Reads.Select(r => (r.Level, r.X, r.Y, r.Width, r.Height)).Should().Equal((0, 0, 0, 48, 40));
        tiled.Span.SequenceEqual(plain.Span).Should().BeTrue();
    }

    // An Environment Texture node reads a tiled image too (Cycles asks for ray derivatives there as well): an
    // equirectangular view of a world at half a texel per pixel renders like the plain image bit for bit within 60
    // degrees of the horizon, where level 0 is selected; towards the poles the equirectangular footprint stretches and
    // Cycles selects smaller levels (mip mapping, which the plain image does not have), so those rows are reported.
    [Fact]
    public void an_environment_texture_reads_a_tiled_image()
    {
        //Arrange
        byte[] pixels = Pattern(256, 128);
        FilmBuffer Render(Func<CyclesScene, CyclesImage> create)
        {
            using CyclesScene scene = TestScenes.CreateCpuScene();
            Configure(scene);
            scene.Camera.CameraType = CameraCameraType.Panorama;
            scene.Camera.PanoramaType = CameraPanoramaType.Equirectangular;
            scene.Camera.Matrix = Transform3x4.Identity;
            ShaderNode world = scene.CreateShader();
            EnvironmentTextureNode environment = world.Add<EnvironmentTextureNode>();
            environment.Interpolation = EnvironmentTextureInterpolation.Closest;
            BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
            world.Connect(environment.ColorOutput, background.ColorInput);
            world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
            world.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
            scene.Background.Shader = world;
            using CyclesImage image = create(scene);
            environment.Bind(image);
            return scene.Render(new RenderSettings(512, 256, 1));
        }
        LevelReader reader = Reader(256, 128, BoxLevels(pixels, 256, 128, 4));

        //Act
        FilmBuffer tiled = Render(scene => scene.CreateTiledImage(256, 128, ImageFormat.Byte4, reader));
        FilmBuffer plain = Render(scene => scene.CreateImage<byte>(256, 128, ImageFormat.Byte4, pixels));
        output.WriteLine($"environment, tiled vs plain image: {FilmComparison.Compare(tiled.Span, plain.Span)}; {reader.Reads.Count} reads, levels {string.Join(",", reader.Reads.Select(r => r.Level).Distinct().Order())}");

        // Rows 43 to 212 of 256: latitudes within 60 degrees of the horizon.
        ReadOnlySpan<float> Band(FilmBuffer film) => film.Span.Slice(43 * 512 * 4, 170 * 512 * 4);
        FilmComparison band = FilmComparison.Compare(Band(tiled), Band(plain));
        output.WriteLine($"environment, within 60 degrees of the horizon: {band}");

        //Assert
        reader.Reads.Should().NotBeEmpty();
        band.BitwiseEqual.Should().BeTrue();
    }

    // A float4 HDR image and a one-channel byte image read through the reader in their own formats.
    [Fact]
    public void float_and_single_channel_tiled_images_render_like_plain_ones()
    {
        //Arrange
        const uint size = 128;
        float[] hdr = [.. Enumerable.Range(0, (int)(size * size)).SelectMany(i => new[] { .25f * (i % 17), 6f - .0625f * (i / size % 64), .5f + .125f * (i % 9), 1f })];
        byte[] hdrBytes = MemoryMarshal.AsBytes(hdr.AsSpan()).ToArray();
        byte[] gray = [.. Enumerable.Range(0, (int)(size * size)).Select(i => (byte)((i * 37) % 251))];
        FilmBuffer Render(Func<CyclesScene, CyclesImage> create)
        {
            using CyclesScene scene = TestScenes.CreateCpuScene();
            Configure(scene);
            ImageTextureNode texture = Quad(scene, .25f, .25f, .75f, .75f);
            using CyclesImage image = create(scene);
            texture.Bind(image);
            return scene.Render(new RenderSettings(128, 128, 1));
        }
        LevelReader floatReader = new(size, size, 16, [hdrBytes, .. Enumerable.Range(1, TiledImage.LevelCount(size, size) - 1).Select(level => new byte[16 * 64 * 64])]);
        LevelReader byteReader = new(size, size, 1, BoxLevels(gray, size, size, 1));

        //Act
        FilmBuffer tiledFloat = Render(scene => scene.CreateTiledImage(size, size, ImageFormat.Float4, floatReader));
        FilmBuffer plainFloat = Render(scene => scene.CreateImage<float>(size, size, ImageFormat.Float4, hdr));
        FilmBuffer tiledByte = Render(scene => scene.CreateTiledImage(size, size, ImageFormat.Byte, byteReader));
        FilmBuffer plainByte = Render(scene => scene.CreateImage<byte>(size, size, ImageFormat.Byte, gray));

        // A reader that writes a value that is not finite fails the render by name, as CreateImage refuses it (Cycles' conform
        // would set it to zero), and the next render reads the tile again.
        float[] poisoned = (float[])hdr.Clone();
        poisoned[4 * (70 * (int)size + 70) + 1] = float.NaN;
        floatReader = new(size, size, 16, [MemoryMarshal.AsBytes(poisoned.AsSpan()).ToArray(), .. floatReader.Levels()[1..]]);
        using CyclesScene nanScene = TestScenes.CreateCpuScene();
        Configure(nanScene);
        ImageTextureNode nanTexture = Quad(nanScene, .25f, .25f, .75f, .75f);
        using (CyclesImage nanImage = nanScene.CreateTiledImage(size, size, ImageFormat.Float4, floatReader)) nanTexture.Bind(nanImage);
        NativeFailure nonfinite = ((Action)(() => nanScene.Render(new RenderSettings(128, 128, 1)))).Should().Throw<NativeFailure>().Which;
        floatReader.Replace(0, hdrBytes);
        FilmBuffer afterNonfinite = nanScene.Render(new RenderSettings(128, 128, 1));

        //Assert
        nonfinite.NativeStatus.Should().Be(NativeStatus.Failed);
        nonfinite.Message.Should().StartWith("Tiled image ").And.Contain("the tile reader wrote a nonfinite value (level 0");
        nanScene.IsFaulted.Should().BeFalse();
        afterNonfinite.Span.SequenceEqual(plainFloat.Span).Should().BeTrue();
        tiledFloat.Span.SequenceEqual(plainFloat.Span).Should().BeTrue();
        tiledFloat.Span.ToArray().Max().Should().BeGreaterThan(1f);
        tiledByte.Span.SequenceEqual(plainByte.Span).Should().BeTrue();
        floatReader.Reads.All(r => r.Level == 0).Should().BeTrue();
    }

    // A reader that throws fails the render by name, with its exception as the cause, the destination untouched and the
    // texture's Missing colour never returned; the tile is read again by the next render, which then succeeds.
    [Fact]
    public void a_reader_that_throws_fails_the_render_by_name_and_the_next_render_reads_the_tile_again()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        ImageTextureNode[] textures = Strip(scene);
        LevelReader reader = Reader(512, 512, SolidLevels(512, 512));
        InvalidOperationException thrown = new("The level 2 file is unreadable.");
        reader.Fail = (level, _, _, _, _) => level == 2 ? thrown : null;
        CyclesImage image = scene.CreateTiledImage(512, 512, ImageFormat.Byte4, reader);
        foreach (ImageTextureNode texture in textures) texture.Bind(image);
        float[] destination = new float[StripSettings.FloatCount];
        Array.Fill(destination, -7f);

        //Act
        CyclesException failure = ((Action)(() => scene.Render(StripSettings, destination))).Should().Throw<CyclesException>().Which;
        reader.Fail = null;
        FilmBuffer film = scene.Render(StripSettings);

        //Assert
        failure.NativeStatus.Should().Be(NativeStatus.Failed);
        failure.InnerException.Should().BeSameAs(thrown);
        failure.Message.Should().Contain($"Tiled image {image.Handle.Value}: the tile reader failed");
        failure.Message.Should().Contain("level 2");
        failure.Message.Should().Contain("The level 2 file is unreadable.");
        destination.All(value => value == -7f).Should().BeTrue();
        scene.IsFaulted.Should().BeFalse();
        film.Span.SequenceEqual(StripOfColors(0, 1, 2, 3).Span).Should().BeTrue();
        image.Dispose();
    }

    // A bake reads tiled textures as a render does, and fails by name the same way.
    [Fact]
    public void a_bake_reads_a_tiled_texture_and_fails_by_name_when_the_reader_fails()
    {
        //Arrange
        const uint size = 256;
        byte[] pixels = Pattern(size, size);
        BakeSettings bake = new(size, size, 1, BakeType.Emission);
        FilmBuffer Bake(bool tiled, Func<int, int, int, int, int, Exception> fail = null)
        {
            using CyclesScene scene = TestScenes.CreateCpuScene();
            Configure(scene);
            (ImageTextureNode texture, ObjectNode target) = QuadObject(scene, 0, 0, 1, 1);
            texture.Bind(tiled
                ? scene.CreateTiledImage(size, size, ImageFormat.Byte4, new LevelReader(size, size, 4, BoxLevels(pixels, size, size, 4)) { Fail = fail })
                : scene.CreateImage<byte>(size, size, ImageFormat.Byte4, pixels));
            return scene.Bake(target, bake);
        }

        //Act
        FilmBuffer plain = Bake(false);
        FilmBuffer tiled = Bake(true);
        FilmBuffer again = Bake(true);
        CyclesException failure = ((Action)(() => Bake(true, (_, _, _, _, _) => new IOException("unreadable")))).Should().Throw<CyclesException>().Which;
        FilmComparison comparison = FilmComparison.Compare(tiled.Span, plain.Span);
        output.WriteLine($"bake, tiled vs plain image: {comparison}");

        //Assert
        tiled.Span.SequenceEqual(again.Span).Should().BeTrue();
        comparison.PracticalFailures.Should().Be(0);
        failure.InnerException.Should().BeOfType<IOException>();
        failure.Message.Should().Contain("the tile reader failed");
    }

    // A reader's call into the scene is refused by name (NotReady), and the render goes on.
    [Fact]
    public void a_call_into_the_library_from_a_reader_is_refused_by_name()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        ImageTextureNode texture = Quad(scene, 0, 0, .25f, .25f);
        LevelReader reader = Reader(256, 256, BoxLevels(Pattern(256, 256), 256, 256, 4));
        NativeFailure refused = null;
        reader.Inside = () =>
        {
            try
            {
                scene.Host.SceneNode("camera");
            }
            catch (NativeFailure failure)
            {
                refused ??= failure;
            }
        };
        using (CyclesImage image = scene.CreateTiledImage(256, 256, ImageFormat.Byte4, reader)) texture.Bind(image);

        //Act
        FilmBuffer film = scene.Render(new RenderSettings(64, 64, 1));

        //Assert
        refused.Should().NotBeNull();
        refused.NativeStatus.Should().Be(NativeStatus.NotReady);
        refused.Message.Should().Be("Host functions cannot be called from a tiled image's read callback");
        film.Span.ToArray().All(float.IsFinite).Should().BeTrue();
    }

    // A cancel reaches a render that waits on slow reads: the read in progress finishes, the threads waiting for the
    // reader give up, and the render stops long before the 64 reads of 150 ms it needs; the next render reads the tiles
    // again and matches the plain image bit for bit.
    [Fact]
    public async Task a_cancel_stops_a_render_that_waits_on_tile_reads()
    {
        //Arrange
        const uint size = 512;
        byte[] pixels = Pattern(size, size);
        TestScenes.RequireNativeHost();
        using CyclesScene scene = new(CyclesBackend.CpuReference, 4);
        Configure(scene);
        ImageTextureNode texture = Quad(scene, 0, 0, 1, 1);
        LevelReader reader = Reader(size, size, BoxLevels(pixels, size, size, 4));
        reader.DelayMilliseconds = 150;
        using (CyclesImage image = scene.CreateTiledImage(size, size, ImageFormat.Byte4, reader)) texture.Bind(image);
        RenderSettings settings = new(768, 768, 1);
        float[] destination = new float[settings.FloatCount];
        Array.Fill(destination, -3f);
        using CancellationTokenSource cancel = new();

        //Act
        Stopwatch timer = Stopwatch.StartNew();
        Task render = scene.RenderAsync(settings, destination, null, cancel.Token);
        while (reader.Reads.IsEmpty && timer.Elapsed < TimeSpan.FromSeconds(30)) await Task.Delay(5, TestContext.Current.CancellationToken);
        cancel.Cancel();
        Func<Task> wait = () => render;
        await wait.Should().ThrowAsync<OperationCanceledException>();
        TimeSpan stopped = timer.Elapsed;
        int readsWhenCancelled = reader.Reads.Count;
        reader.DelayMilliseconds = 0;
        FilmBuffer film = await scene.RenderAsync(settings, null, TestContext.Current.CancellationToken);
        using CyclesScene plainScene = TestScenes.CreateCpuScene();
        Configure(plainScene);
        using (CyclesImage plainImage = plainScene.CreateImage<byte>(size, size, ImageFormat.Byte4, pixels)) Quad(plainScene, 0, 0, 1, 1).Bind(plainImage);
        FilmBuffer plain = plainScene.Render(settings);
        output.WriteLine($"cancelled after {stopped.TotalMilliseconds:F0} ms with {readsWhenCancelled} reads of the 64 level-0 tiles");

        //Assert
        stopped.Should().BeLessThan(TimeSpan.FromSeconds(4));
        readsWhenCancelled.Should().BeLessThan(20);
        destination.All(value => value == -3f).Should().BeTrue();
        film.Span.SequenceEqual(plain.Span).Should().BeTrue();
        reader.Overlapped.Should().BeFalse();
    }

    // Many tiles on several threads: an 8-thread CPU host reads 64 level-0 tiles of a 512 image through one reader, one
    // call at a time, and two such scenes render at once on two threads; every film equals the plain image's bit for bit.
    [Fact]
    public async Task many_tiles_read_on_several_render_threads_are_serialized_per_scene()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        const uint size = 512;
        byte[] pixels = Pattern(size, size);
        RenderSettings settings = new(768, 768, 1);
        (FilmBuffer Film, LevelReader Reader) Render(bool tiled)
        {
            using CyclesScene scene = new(CyclesBackend.CpuReference, 8);
            Configure(scene);
            ImageTextureNode texture = Quad(scene, 0, 0, 1, 1);
            LevelReader reader = tiled ? Reader(size, size, BoxLevels(pixels, size, size, 4)) : null;
            using (CyclesImage image = tiled ? scene.CreateTiledImage(size, size, ImageFormat.Byte4, reader) : scene.CreateImage<byte>(size, size, ImageFormat.Byte4, pixels))
                texture.Bind(image);
            return (scene.Render(settings), reader);
        }

        //Act
        FilmBuffer plain = Render(false).Film;
        (FilmBuffer Film, LevelReader Reader)[] tiled = await Task.WhenAll(Task.Run(() => Render(true)), Task.Run(() => Render(true)));
        output.WriteLine($"reads per scene: {string.Join(", ", tiled.Select(t => t.Reader.Reads.Count))}; reading threads: {string.Join(", ", tiled.Select(t => t.Reader.Reads.Select(r => r.Thread).Distinct().Count()))}");

        //Assert
        foreach ((FilmBuffer film, LevelReader reader) in tiled)
        {
            film.Span.SequenceEqual(plain.Span).Should().BeTrue();
            reader.Overlapped.Should().BeFalse();
            reader.Reads.Count(r => r.Level == 0).Should().BeGreaterThanOrEqualTo(64);
        }
    }

    // UDIM tiles of tiled images render like the same UDIM tiles of plain images; a binding that mixes tiled and plain
    // images is refused by name.
    [Fact]
    public void udim_tiles_of_tiled_images_render_and_a_mix_with_plain_images_is_refused()
    {
        //Arrange
        const uint size = 128;
        byte[] left = Pattern(size, size), right = [.. Pattern(size, size).Select((value, index) => index % 4 == 3 ? value : (byte)(255 - value))];
        FilmBuffer Render(bool tiled)
        {
            using CyclesScene scene = TestScenes.CreateCpuScene();
            Configure(scene);
            ImageTextureNode texture = Quad(scene, .5f, .25f, 1.5f, .75f);
            CyclesImage Create(byte[] pixels) => tiled ? scene.CreateTiledImage(size, size, ImageFormat.Byte4, Reader(size, size, BoxLevels(pixels, size, size, 4)))
                : scene.CreateImage<byte>(size, size, ImageFormat.Byte4, pixels);
            using CyclesImage a = Create(left), b = Create(right);
            texture.BindTiles([new UdimTile(1001, a), new UdimTile(1002, b)]);
            return scene.Render(new RenderSettings(128, 64, 1));
        }

        //Act
        FilmBuffer tiled = Render(true);
        FilmBuffer plain = Render(false);
        using CyclesScene mixed = TestScenes.CreateCpuScene();
        Configure(mixed);
        ImageTextureNode node = Quad(mixed, 0, 0, 2, 1);
        using CyclesImage tiledImage = mixed.CreateTiledImage(size, size, ImageFormat.Byte4, Reader(size, size, BoxLevels(left, size, size, 4)));
        using CyclesImage plainImage = mixed.CreateImage<byte>(size, size, ImageFormat.Byte4, right);
        NativeFailure refused = ((Action)(() => node.BindTiles([new UdimTile(1001, tiledImage), new UdimTile(1002, plainImage)]))).Should().Throw<NativeFailure>().Which;

        //Assert
        tiled.Span.SequenceEqual(plain.Span).Should().BeTrue();
        refused.NativeStatus.Should().Be(NativeStatus.Invalid);
        refused.Message.Should().Contain("UDIM tile 1002 is not a tiled image");
    }

    // A tiled image cannot be updated (its reader supplies its pixels), through the facade or the host.
    [Fact]
    public void a_tiled_image_cannot_be_updated()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        using CyclesImage image = scene.CreateTiledImage(128, 128, ImageFormat.Byte4, Reader(128, 128, SolidLevels(128, 128)));
        byte[] pixels = new byte[128 * 128 * 4];

        //Act
        Action facade = () => image.Update<byte>(pixels);
        NativeFailure host = ((Action)(() => scene.Host.UpdateImage(image.Handle, 128, 128, ImageFormat.Byte4, pixels))).Should().Throw<NativeFailure>().Which;

        //Assert
        image.IsTiled.Should().BeTrue();
        facade.Should().Throw<InvalidOperationException>();
        host.NativeStatus.Should().Be(NativeStatus.Invalid);
        host.Message.Should().Contain("A tiled image cannot be updated");
        ((Action)(() => scene.CreateTiledImage(128, 128, ImageFormat.Byte4, null))).Should().Throw<ArgumentNullException>();
        ((Action)(() => scene.CreateTiledImage(0, 128, ImageFormat.Byte4, Reader(1, 1, [])))).Should().Throw<NativeFailure>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
    }

    // The scene keeps a reader alive while the image is in use (no other reference to it exists here), and lets it go once
    // the scene is disposed.
    [Fact]
    public void the_scene_keeps_the_reader_alive_while_it_is_used_and_lets_it_go_afterwards()
    {
        //Arrange
        CyclesScene scene = TestScenes.CreateCpuScene();
        Configure(scene);
        ImageTextureNode texture = Quad(scene, 0, 0, .5f, .5f);
        WeakReference reader = BindUnreferencedReader(scene, texture);

        //Act
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        FilmBuffer film = scene.Render(new RenderSettings(64, 64, 1));
        bool aliveWhileUsed = reader.IsAlive;
        scene.Dispose();
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();

        //Assert
        aliveWhileUsed.Should().BeTrue();
        film.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        reader.IsAlive.Should().BeFalse();
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference BindUnreferencedReader(CyclesScene scene, ImageTextureNode texture)
    {
        LevelReader reader = Reader(256, 256, BoxLevels(Pattern(256, 256), 256, 256, 4));
        using (CyclesImage image = scene.CreateTiledImage(256, 256, ImageFormat.Byte4, reader)) texture.Bind(image);
        return new WeakReference(reader);
    }

    // ---- The release callback, through the low-level tier --------------------------------------------------------------

    private static readonly ConcurrentDictionary<nint, int> Releases = new();
    private static readonly ConcurrentDictionary<nint, int> ReadDelays = new();
    private static readonly ConcurrentDictionary<nint, int> ReadCounts = new();
    private static long nextToken = 1000;

    private static nint Token() => (nint)Interlocked.Increment(ref nextToken);

    private static int ReleasesOf(nint token) => Releases.TryGetValue(token, out int count) ? count : 0;

    // Serves a solid byte4 colour; a token at or above 1 << 40 returns its low byte as a failure status.
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe int SolidRead(nint userData, uint level, uint x, uint y, uint width, uint height, void* pixels, ulong rowBytes)
    {
        ReadCounts.AddOrUpdate(userData, 1, (_, count) => count + 1);
        if (ReadDelays.TryGetValue(userData, out int delay)) Thread.Sleep(delay);
        if ((long)userData >= 1L << 40) return (int)(userData & 0xff);
        for (uint row = 0; row < height; row++)
        {
            byte* line = (byte*)pixels + row * rowBytes;
            for (uint i = 0; i < width; i++) { line[4 * i] = 90; line[4 * i + 1] = (byte)(30 * level); line[4 * i + 2] = 200; line[4 * i + 3] = 255; }
        }
        return 0;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void CountRelease(nint userData) => Releases.AddOrUpdate(userData, 1, (_, count) => count + 1);

    // A CPU host with a quad whose image texture shows UVs 0..0.5 of a bound image.
    private static (CyclesHost Host, Node Texture) LowLevelScene()
    {
        TestScenes.RequireNativeHost();
        CyclesHost host = new(Backend.CpuReference);
        Node camera = host.SceneNode("camera");
        host.SetString(camera, "camera_type", "orthograph");
        host.SetFloats(camera, "matrix", [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -4]);
        Node shader = host.CreateNode("shader"), uv = host.ShaderNode(shader, "uvmap"), texture = host.ShaderNode(shader, "image_texture");
        Node emission = host.ShaderNode(shader, "emission");
        host.Connect(uv, "UV", texture, "Vector");
        host.Connect(texture, "Color", emission, "Color");
        host.Connect(emission, "Emission", host.ShaderOutput(shader), "Surface");
        Node mesh = host.CreateNode("mesh");
        host.SetNodes(mesh, "used_shaders", [shader]);
        host.Mesh(mesh, [-1f, -1f, 0, 1f, -1f, 0, 1f, 1f, 0, -1f, 1f, 0], [0, 1, 2, 0, 2, 3]);
        host.SetAttribute(mesh, "uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, [0f, 0f, .5f, 0f, .5f, .5f, 0f, 0f, .5f, .5f, 0f, .5f]);
        Node instance = host.CreateNode("object");
        host.SetNodes(instance, "geometry", [mesh]);
        return (host, texture);
    }

    private static unsafe Image CreateTiled(CyclesHost host, nint token, uint size = 256, ImageFormat format = ImageFormat.Byte4) =>
        host.CreateTiledImage(size, size, format, &SolidRead, &CountRelease, token);

    private static void Render(CyclesHost host) => host.Render(32, 32, 1, new float[32 * 32 * 4]);

    [Fact]
    public async Task the_release_callback_runs_exactly_once_on_every_path()
    {
        (Task render, CyclesHost second, nint destroyed, nint cancelled) = ReleasePaths();
        (await ((Func<Task>)(() => render)).Should().ThrowAsync<NativeFailure>()).Which.NativeStatus.Should().Be(NativeStatus.Cancelled);
        ReleasesOf(cancelled).Should().Be(0);
        second.Dispose();
        ReleasesOf(destroyed).Should().Be(1);
        ReleasesOf(cancelled).Should().Be(1);
    }

    private static unsafe (Task Render, CyclesHost Second, nint Destroyed, nint Cancelled) ReleasePaths()
    {
        //Arrange
        nint invalidFormat = Token(), noReader = Token(), neverBound = Token(), unbound = Token(), destroyed = Token(),
            failed = (nint)((1L << 40) | ((long)Token() << 8) | 7), disposed = Token(), cancelled = Token();
        ReadDelays[cancelled] = 300;

        //Act / Assert
        (CyclesHost host, Node texture) = LowLevelScene();
        // A failed call releases at once.
        ((Action)(() => CreateTiled(host, invalidFormat, 256, (ImageFormat)99))).Should().Throw<NativeFailure>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
        ReleasesOf(invalidFormat).Should().Be(1);
        ((Action)(() => host.CreateTiledImage(256, 256, ImageFormat.Byte4, null, &CountRelease, noReader))).Should().Throw<NativeFailure>();
        ReleasesOf(noReader).Should().Be(1);
        // An image never bound releases with its handle.
        Image loose = CreateTiled(host, neverBound);
        ReleasesOf(neverBound).Should().Be(0);
        host.ReleaseImage(loose);
        ReleasesOf(neverBound).Should().Be(1);
        // A bound and rendered image: the handle, then the binding, then Cycles' image (freed at the next render).
        Image image = CreateTiled(host, unbound);
        host.BindImage(texture, image);
        Render(host);
        ReadCounts[unbound].Should().BeGreaterThan(0);
        host.ReleaseImage(image);
        host.BindImage(texture, default);
        ReleasesOf(unbound).Should().Be(0);
        Render(host);
        ReleasesOf(unbound).Should().Be(1);
        Render(host);
        ReleasesOf(unbound).Should().Be(1);
        // A failed render does not release; the destroy does, once.
        Image failing = CreateTiled(host, failed);
        host.BindImage(texture, failing);
        NativeFailure failure = ((Action)(() => Render(host))).Should().Throw<NativeFailure>().Which;
        failure.NativeStatus.Should().Be(NativeStatus.Failed);
        failure.Message.Should().StartWith($"Tiled image {failing.Value}: the tile reader failed (returned 7; level ");
        ReleasesOf(failed).Should().Be(0);
        host.Dispose();
        ReleasesOf(failed).Should().Be(1);
        // A disposed host: the native library is not called, and release still runs once.
        ((Action)(() => CreateTiled(host, disposed))).Should().Throw<ObjectDisposedException>();
        ReleasesOf(disposed).Should().Be(1);
        // A cancelled render and a destroy with the image bound.
        (CyclesHost second, Node secondTexture) = LowLevelScene();
        Image bound = CreateTiled(second, destroyed);
        second.BindImage(secondTexture, bound);
        Render(second);
        Image slow = CreateTiled(second, cancelled);
        second.BindImage(secondTexture, slow);
        Task render = Task.Run(() => second.Render(128, 128, 1, new float[128 * 128 * 4]));
        SpinWait.SpinUntil(() => ReadCounts.ContainsKey(cancelled), TimeSpan.FromSeconds(30)).Should().BeTrue();
        second.Cancel().Should().BeTrue();
        return (render, second, destroyed, cancelled);
    }

    // ---- The consumer path with CodeBrix.Imaging -----------------------------------------------------------------------

    // Serves the levels of an image file decoded with CodeBrix.Imaging, the reader's own work: the library never reads it.
    private sealed class DecodedFileReader : ITiledImageReader
    {
        public readonly byte[][] Levels;
        public readonly uint Width, Height;
        public int Reads;

        public DecodedFileReader(string path)
        {
            using var decoded = ImagingImage.Load<Rgba32>(File.ReadAllBytes(path));
            Width = (uint)decoded.Width;
            Height = (uint)decoded.Height;
            int row = decoded.Width * 4;
            byte[] topDown = new byte[row * decoded.Height], bottomUp = new byte[topDown.Length];
            decoded.CopyPixelDataTo(topDown);
            for (int y = 0; y < decoded.Height; y++) Buffer.BlockCopy(topDown, (decoded.Height - 1 - y) * row, bottomUp, y * row, row);
            Levels = BoxLevels(bottomUp, Width, Height, 4);
        }

        public void ReadTile(int level, int x, int y, int width, int height, Span<byte> destination, int rowBytes)
        {
            Interlocked.Increment(ref Reads);
            (uint levelWidth, _) = TiledImage.LevelSize(Width, Height, level);
            for (int r = 0; r < height; r++)
                Levels[level].AsSpan(((y + r) * (int)levelWidth + x) * 4, width * 4).CopyTo(destination[(r * rowBytes)..]);
        }
    }

    // A committed bench texture (CC0, native/oracle/scenes/assets/ForestVillage) decoded with CodeBrix.Imaging, its levels
    // made by the reader, served through ITiledImageReader: a magnified view renders bit for bit like the decoded pixels
    // bound plainly, and a minified one reads only the small levels.
    [Fact]
    public void a_reader_decoding_an_image_file_with_codebrix_imaging_feeds_the_texture_cache()
    {
        //Arrange
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        string path = Path.Combine(RepositoryPaths.Root, "native", "oracle", "scenes", "assets", "ForestVillage", "Textures", "rough_wood_diff_2k.jpg");
        DecodedFileReader reader = new(path);
        FilmBuffer Render(float u0, float u1, uint film, bool tiled)
        {
            using CyclesScene scene = TestScenes.CreateCpuScene();
            Configure(scene);
            ImageTextureNode texture = Quad(scene, u0, u0, u1, u1);
            using (CyclesImage image = tiled ? scene.CreateTiledImage(reader.Width, reader.Height, ImageFormat.Byte4, reader)
                       : scene.CreateImage<byte>(reader.Width, reader.Height, ImageFormat.Byte4, reader.Levels[0]))
                texture.Bind(image);
            return scene.Render(new RenderSettings(film, film, 1));
        }

        //Act
        FilmBuffer magnifiedTiled = Render(.48f, .52f, 160, true);
        int magnifiedReads = reader.Reads;
        FilmBuffer magnifiedPlain = Render(.48f, .52f, 160, false);
        FilmBuffer minifiedTiled = Render(0, 1, 64, true);
        int minifiedReads = reader.Reads - magnifiedReads;
        output.WriteLine($"{reader.Width} x {reader.Height}, {reader.Levels.Length} levels; magnified {magnifiedReads} reads, minified {minifiedReads} reads");

        //Assert
        reader.Width.Should().Be(2048u);
        magnifiedTiled.Span.SequenceEqual(magnifiedPlain.Span).Should().BeTrue();
        magnifiedReads.Should().BeLessThan(10);
        minifiedReads.Should().BeLessThan(TotalTiles(reader.Width, reader.Height) / 100);
        minifiedTiled.Span.ToArray().All(float.IsFinite).Should().BeTrue();
    }
}
