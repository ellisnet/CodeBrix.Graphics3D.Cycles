// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Lights;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.GltfRender;
using CodeBrix.Graphics3D.Cycles.Interop;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Tiled rendering (provisional CyclesScene.TileSize, cbc_host_tile_size) on the CPU reference backend. Cycles renders the
// film tile by tile; the library keeps the finished tiles in memory (the overlay's in-memory tile store in
// integrator/path_trace.cpp, never a tile file) and, once the last tile is done, reads the full frame back, denoises it
// when denoising and returns it with every pass. A pixel's samples depend on its film coordinates only, and the sample
// count pass Cycles adds for tiles normalises by the same count, so the final film must equal the untiled film.
public class TiledRenderingTests
{
    private readonly ITestOutputHelper output;

    public TiledRenderingTests(ITestOutputHelper output) => this.output = output;

    private static CancellationToken Token => TestContext.Current.CancellationToken;

    private static readonly RenderSettings Small = new(32, 24, 8);

    // The named passes of the "passes" variant, read after every render.
    private static readonly string[] PassNames = ["Depth", "Normal", "DiffuseColor", "DiffuseDirect", "Emission", "SampleCount", "Group", "CryptoObject00", "CryptoMaterial00"];

    internal static void Build(CyclesScene scene, string variant)
    {
        (ShaderNode material, DiffuseBsdfNode _, MeshNode _) = TestScenes.BuildTyped(scene);
        IntegratorNode integrator = scene.Integrator;
        switch (variant)
        {
            case "diffuse":
                break;
            case "passes":
            {
                // A lamp and a second, emissive triangle; named passes, a light group and cryptomatte.
                AddLampAndGlow(scene);
                material.Name = "material";
                scene.AddPass(PassType.Depth, "Depth");
                scene.AddPass(PassType.Normal, "Normal");
                scene.AddPass(PassType.DiffuseColor, "DiffuseColor");
                scene.AddPass(PassType.DiffuseDirect, "DiffuseDirect");
                scene.AddPass(PassType.Emission, "Emission");
                scene.AddPass(PassType.SampleCount, "SampleCount");
                scene.AddPass(PassType.Combined, "Group").Lightgroup = "group";
                scene.Film.CryptomattePasses = FilmCryptomattePasses.Object | FilmCryptomattePasses.Material;
                scene.Film.CryptomatteDepth = 1;
                scene.AddPass(PassType.Cryptomatte, "CryptoObject00");
                scene.AddPass(PassType.Cryptomatte, "CryptoMaterial00");
                break;
            }
            case "camera-border":
                scene.Camera.BorderLeft = .25f;
                scene.Camera.BorderRight = .75f;
                scene.Camera.BorderBottom = .25f;
                scene.Camera.BorderTop = .625f;
                break;
            case "adaptive":
                integrator.UseAdaptiveSampling = true;
                integrator.AdaptiveThreshold = 0.02f;
                integrator.AdaptiveMinSamples = 8;
                scene.AddPass(PassType.SampleCount, "SampleCount");
                break;
            case "denoised":
            case "denoised-sample-count":
                AddLampAndGlow(scene);
                integrator.UseDenoise = true;
                scene.AddPass(PassType.Combined, "Noisy", PassMode.Noisy);
                if (variant == "denoised-sample-count") scene.AddPass(PassType.SampleCount, "SampleCount");
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(variant));
        }
    }

    // A point lamp in the light group "group" and a second, emissive triangle beside the lit one.
    private static void AddLampAndGlow(CyclesScene scene)
    {
        PointLightNode lamp = scene.CreateLight<PointLightNode>();
        lamp.Strength = new Vector3(60, 55, 50);
        lamp.Radius = .2f;
        ShaderNode emission = scene.CreateShader();
        EmissionNode emit = emission.Add<EmissionNode>();
        emission.Connect(emit.EmissionOutput, emission.Output.SurfaceInput);
        lamp.UsedShaders = [emission];
        ObjectNode lampObject = scene.CreateObject(lamp, Transform3x4.CreateTranslation(1.5f, 2f, -2.5f));
        lampObject.Lightgroup = "group";
        ShaderNode glow = scene.CreateShader();
        EmissionNode glowEmission = glow.Add<EmissionNode>();
        glowEmission.Color = new Vector3(.2f, .6f, .9f);
        glowEmission.Strength = 2f;
        glow.Connect(glowEmission.EmissionOutput, glow.Output.SurfaceInput);
        glow.Name = "glow";
        MeshNode second = scene.CreateMesh([-2.2f, -.4f, .6f, -.6f, -1.4f, .6f, -1.2f, 1.2f, .6f], [0, 1, 2], glow);
        ObjectNode secondObject = scene.CreateObject(second);
        secondObject.Name = "second";
        secondObject.AssetName = "asset";
    }

    internal static RenderSettings SettingsOf(string variant) => variant switch
    {
        "adaptive" => new RenderSettings(32, 24, 64),
        "denoised" or "denoised-sample-count" => new RenderSettings(32, 24, 16),
        _ => Small,
    };

    // The host's tiled-rendering facts from its device report: the tile size, the tiles the library kept in memory for
    // its renders and bakes, the full frames read back, and the tiles held now.
    internal static (uint TileSize, long TilesKept, long FramesRead, long TilesHeld) Report(CyclesScene scene)
    {
        using JsonDocument report = JsonDocument.Parse(scene.Host.DeviceReportJson());
        JsonElement tiled = report.RootElement.GetProperty("tiledRendering");
        return (tiled.GetProperty("tileSize").GetUInt32(), tiled.GetProperty("tilesKeptInMemory").GetInt64(),
                tiled.GetProperty("framesReadBack").GetInt64(), tiled.GetProperty("tilesHeld").GetInt64());
    }

    private static (FilmBuffer Film, Dictionary<string, float[]> Passes, List<RenderEvent> Events) Render(string variant, uint tileSize,
        CyclesScene reuse = null)
    {
        CyclesScene scene = reuse ?? TestScenes.CreateCpuScene();
        try
        {
            if (reuse == null) Build(scene, variant);
            scene.TileSize = tileSize;
            List<RenderEvent> events = [];
            void Record(object sender, RenderEvent e) => events.Add(e);
            scene.RenderEventReceived += Record;
            FilmBuffer film = scene.Render(SettingsOf(variant));
            scene.RenderEventReceived -= Record;
            string[] names = variant switch { "passes" => PassNames, "adaptive" => ["SampleCount"], "denoised" => ["Noisy"], "denoised-sample-count" => ["Noisy"], _ => [] };
            Dictionary<string, float[]> passes = names.ToDictionary(name => name, name => scene.ReadPass(name).Span.ToArray());
            return (film, passes, events);
        }
        finally
        {
            if (reuse == null) scene.Dispose();
        }
    }

    [Fact]
    public void the_tile_size_reads_back_and_sizes_cycles_would_change_are_refused_by_name()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        CyclesAbi.Create(Backend.CpuReference, 1, out ulong host).Should().Be(NativeStatus.Ok);
        uint[] accepted = [0, 8, 64, 128, 256, 1024, 8192, 0];
        uint[] refused = [1, 7, 129, 200, 1000, 8193, 8320, uint.MaxValue];

        //Act
        uint initial = scene.TileSize;
        Dictionary<uint, NativeStatus> native = [];
        string message = null;
        foreach (uint size in accepted.Concat(refused))
        {
            native[size] = (NativeStatus)Native.TileSize(host, size);
            if (native[size] != NativeStatus.Ok) message ??= CyclesAbi.LastError();
        }
        CyclesAbi.Destroy(host).Should().Be(NativeStatus.Ok);
        scene.TileSize = 16;
        uint set = scene.TileSize;
        Action invalid = () => scene.TileSize = 129;
        Action low = () => scene.Host.SetTileSize(7);

        //Assert
        initial.Should().Be(0u);
        set.Should().Be(16u);
        foreach (uint size in accepted) native[size].Should().Be(NativeStatus.Ok);
        foreach (uint size in refused) native[size].Should().Be(NativeStatus.Invalid);
        message.Should().Be("Tile size must be 0 (one tile), 8 to 128, or a multiple of 128 up to 8192");
        invalid.Should().Throw<ArgumentOutOfRangeException>().Which.Message.Should().Contain("Tile size must be 0 (one tile), 8 to 128, or a multiple of 128 up to 8192");
        scene.TileSize.Should().Be(16u);
        low.Should().Throw<NativeFailure>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
        Report(scene).TileSize.Should().Be(16u);
    }

    // The plain scene at tile sizes 8, 16 and 32 (12, 4 and 1 tiles of the 32 x 24 film: a tile whose area covers the
    // film renders it in one tile), named passes with a light group, cryptomatte and a sample count pass, and a camera
    // border (16 x 9 pixels, 4 tiles of 8): every film and pass bit for bit the untiled one, the tiles in memory.
    [Theory]
    [InlineData("diffuse", 8u, 12)]
    [InlineData("diffuse", 16u, 4)]
    [InlineData("diffuse", 32u, 0)]
    [InlineData("passes", 8u, 12)]
    [InlineData("camera-border", 8u, 4)]
    public void a_tiled_render_equals_the_untiled_render_bit_for_bit(string variant, uint tileSize, int tiles)
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Build(scene, variant);

        //Act
        (FilmBuffer untiled, Dictionary<string, float[]> untiledPasses, _) = Render(variant, 0);
        (FilmBuffer tiled, Dictionary<string, float[]> tiledPasses, List<RenderEvent> events) = Render(variant, tileSize, scene);
        (uint size, long kept, long frames, long held) = Report(scene);
        FilmComparison comparison = FilmComparison.Compare(tiled.Span, untiled.Span);
        output.WriteLine($"{variant}, tile {tileSize} ({tiles} tiles) vs untiled: {comparison}");

        //Assert
        tiled.Span.SequenceEqual(untiled.Span).Should().BeTrue();
        tiled.Span.ToArray().Any(value => value != 0f).Should().BeTrue();
        foreach ((string name, float[] pixels) in untiledPasses)
        {
            output.WriteLine($"  pass {name}: {FilmComparison.Compare(tiledPasses[name], pixels)}");
            tiledPasses[name].AsSpan().SequenceEqual(pixels).Should().BeTrue(name);
        }
        // The sample count pass reads the samples of each pixel divided by the render's samples.
        if (variant == "passes") untiledPasses["SampleCount"].All(value => value == 1f).Should().BeTrue();
        size.Should().Be(tileSize);
        kept.Should().Be(tiles);
        frames.Should().Be(tiles == 0 ? 0 : 1);
        held.Should().Be(0);
        string[] status = [.. events.Where(e => e.Kind == RenderEventKind.Status).Select(e => e.Message)];
        if (tiles == 0) status.Should().NotContain(m => m.Contains("Tiles"));
        else
        {
            status.Should().Contain(m => m.Contains($"/{tiles} Tiles"));
            status.Should().Contain("Reading full buffer from memory");
            status.Should().Contain("Finishing");
        }
        status.Should().NotContain(m => m.Contains("disk") || m.Contains("file"));
        if (variant == "camera-border")
            for (int y = 0; y < 24; y++)
                for (int x = 0; x < 32; x++)
                    if (x < 8 || x >= 24 || y < 6 || y >= 15)
                        tiled.Span.Slice((y * 32 + x) * 4, 4).ToArray().Should().Equal(0f, 0f, 0f, 0f);
    }

    // Adaptive sampling renders a 4-pixel overscan around each tile (TileManager::update), so each pixel's stopping decision
    // sees its neighbours: measured bit for bit, the sample counts included.
    [Theory]
    [InlineData(8u)]
    [InlineData(16u)]
    public void adaptive_sampling_in_tiles_equals_the_untiled_render_bit_for_bit(uint tileSize)
    {
        //Arrange / Act
        (FilmBuffer untiled, Dictionary<string, float[]> untiledPasses, _) = Render("adaptive", 0);
        (FilmBuffer tiled, Dictionary<string, float[]> tiledPasses, _) = Render("adaptive", tileSize);
        (FilmBuffer again, _, _) = Render("adaptive", tileSize);
        output.WriteLine($"adaptive, tile {tileSize} vs untiled: {FilmComparison.Compare(tiled.Span, untiled.Span)}");
        float[] counts = untiledPasses["SampleCount"];
        output.WriteLine($"  sample counts {counts.Min() * 64} to {counts.Max() * 64}");

        //Assert
        again.Span.SequenceEqual(tiled.Span).Should().BeTrue();
        tiled.Span.SequenceEqual(untiled.Span).Should().BeTrue();
        tiledPasses["SampleCount"].AsSpan().SequenceEqual(counts).Should().BeTrue();
        // Pixels stopped at different sample counts (the pass is the count divided by the render's 64 samples).
        counts.Distinct().Count().Should().BeGreaterThan(1);
        counts.Min().Should().BeLessThan(1f);
    }

    // Denoising in tiles runs once, over the full frame read back from memory (PathTrace::process_full_buffer_from_disk).
    // With tiles Cycles adds a sample count pass, and with one Open Image Denoise receives the noisy image divided by each
    // pixel's sample count and its output is multiplied back (integrator/denoiser_oidn.cpp), where an untiled render
    // without that pass hands it the sums: the tiled film is bit for bit the untiled film of the same scene with a
    // sample count pass, and meets the practical criterion against the plain untiled film (measured: bit for bit in this
    // scene, not in the committed scene render-settings/tiles-border-denoise, where the two inputs give Open Image Denoise
    // results that differ in the last bits). The noisy film is bit for bit in every case.
    [Theory]
    [InlineData(8u)]
    [InlineData(16u)]
    public void denoising_in_tiles_equals_the_untiled_render_with_a_sample_count_pass(uint tileSize)
    {
        //Arrange / Act
        (FilmBuffer untiled, Dictionary<string, float[]> untiledPasses, _) = Render("denoised", 0);
        (FilmBuffer counted, Dictionary<string, float[]> countedPasses, _) = Render("denoised-sample-count", 0);
        (FilmBuffer tiled, Dictionary<string, float[]> tiledPasses, _) = Render("denoised", tileSize);
        (FilmBuffer again, _, _) = Render("denoised", tileSize);
        FilmComparison versusPlain = FilmComparison.Compare(tiled.Span, untiled.Span);
        FilmComparison versusCounted = FilmComparison.Compare(tiled.Span, counted.Span);
        output.WriteLine($"denoised, tile {tileSize} vs untiled: {versusPlain}");
        output.WriteLine($"denoised, tile {tileSize} vs untiled with a sample count pass: {versusCounted}");

        //Assert
        again.Span.SequenceEqual(tiled.Span).Should().BeTrue();
        tiled.Span.SequenceEqual(counted.Span).Should().BeTrue();
        versusPlain.PracticalFailures.Should().Be(0);
        tiledPasses["Noisy"].AsSpan().SequenceEqual(untiledPasses["Noisy"]).Should().BeTrue();
        countedPasses["Noisy"].AsSpan().SequenceEqual(untiledPasses["Noisy"]).Should().BeTrue();
        // The denoised film is not the noisy one.
        untiled.Span.SequenceEqual(untiledPasses["Noisy"]).Should().BeFalse();
    }

    // A render that switches between tile sizes on one scene, with an edit between: each film equals a new scene's
    // untiled film (the sample count pass Cycles adds for tiles goes away again).
    [Fact]
    public void one_scene_renders_with_and_without_tiles_and_after_an_edit_like_a_new_scene()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (ShaderNode _, DiffuseBsdfNode diffuse, MeshNode _) = TestScenes.BuildTyped(scene);
        using CyclesScene fresh = TestScenes.CreateCpuScene();
        (ShaderNode _, DiffuseBsdfNode freshDiffuse, MeshNode _) = TestScenes.BuildTyped(fresh);
        FilmBuffer expected = fresh.Render(Small);
        freshDiffuse.Roughness = .8f;
        FilmBuffer expectedEdited = fresh.Render(Small);

        //Act
        scene.TileSize = 8;
        FilmBuffer tiled = scene.Render(Small);
        scene.TileSize = 0;
        FilmBuffer untiled = scene.Render(Small);
        scene.TileSize = 16;
        diffuse.Roughness = .8f;
        FilmBuffer edited = scene.Render(Small);
        scene.TileSize = 0;
        FilmBuffer editedUntiled = scene.Render(Small);

        //Assert
        tiled.Span.SequenceEqual(expected.Span).Should().BeTrue();
        untiled.Span.SequenceEqual(expected.Span).Should().BeTrue();
        edited.Span.SequenceEqual(expectedEdited.Span).Should().BeTrue();
        editedUntiled.Span.SequenceEqual(expectedEdited.Span).Should().BeTrue();
        Report(scene).TilesKept.Should().Be(12 + 4);
        Report(scene).FramesRead.Should().Be(2);
    }

    // Bakes tile too (no overscan when baking): each tile reads its region of the bake map, and the baked image equals the
    // untiled bake bit for bit, for a light bake and a colour pass bake.
    [Theory]
    [InlineData(BakeType.Combined, 16u, 16)]
    [InlineData(BakeType.Combined, 8u, 64)]
    [InlineData(BakeType.DiffuseColor, 16u, 16)]
    [InlineData(BakeType.Diffuse, 24u, 9)]
    public void a_tiled_bake_equals_the_untiled_bake_bit_for_bit(BakeType type, uint tileSize, int tiles)
    {
        //Arrange
        BakeSettings settings = new(64, 64, 8, type);
        FilmBuffer Bake(uint size, out (uint, long, long, long) report)
        {
            using CyclesScene scene = TestScenes.CreateCpuScene();
            (ShaderNode material, DiffuseBsdfNode _, MeshNode _) = TestScenes.BuildTyped(scene);
            MeshNode quad = scene.CreateMesh([-1f, -1f, 1f, 1f, -1f, 1f, 1f, 1f, 1f, -1f, 1f, 1f], [0, 1, 2, 0, 2, 3], material);
            quad.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, [0f, 0f, 1f, 0f, 1f, 1f, 0f, 0f, 1f, 1f, 0f, 1f]);
            ObjectNode target = scene.CreateObject(quad);
            PointLightNode lamp = scene.CreateLight<PointLightNode>();
            lamp.Strength = new Vector3(40, 40, 40);
            ShaderNode emission = scene.CreateShader();
            EmissionNode emit = emission.Add<EmissionNode>();
            emission.Connect(emit.EmissionOutput, emission.Output.SurfaceInput);
            lamp.UsedShaders = [emission];
            scene.CreateObject(lamp, Transform3x4.CreateTranslation(0, 1, -2));
            scene.TileSize = size;
            FilmBuffer film = scene.Bake(target, settings);
            report = Report(scene);
            return film;
        }

        //Act
        FilmBuffer untiled = Bake(0, out _);
        FilmBuffer tiled = Bake(tileSize, out (uint TileSize, long TilesKept, long FramesRead, long TilesHeld) report);
        output.WriteLine($"{type} bake, tile {tileSize} ({tiles} tiles) vs untiled: {FilmComparison.Compare(tiled.Span, untiled.Span)}");

        //Assert
        tiled.Span.SequenceEqual(untiled.Span).Should().BeTrue();
        tiled.Span.ToArray().Any(value => value != 0f).Should().BeTrue();
        report.TilesKept.Should().Be(tiles);
        report.FramesRead.Should().Be(1);
        report.TilesHeld.Should().Be(0);
    }

    // No tile file: the tiles stay in the library's memory. A tiled render writes no "cycles-tile-buffer" file (Cycles'
    // tile file name) into the process's working directory (where Cycles would write it with no temporary directory set)
    // or the temporary directory, and the device report's counters show the tiles kept in memory and the frame read back.
    [Fact]
    public void a_tiled_render_writes_no_file_and_keeps_its_tiles_in_memory()
    {
        //Arrange
        string[] folders = [Environment.CurrentDirectory, Path.GetTempPath(), AppContext.BaseDirectory];
        string[] TileFiles() => [.. folders.Where(Directory.Exists).SelectMany(folder =>
            Directory.EnumerateFiles(folder, "cycles-tile-buffer*").Concat(Directory.EnumerateFiles(folder, "*.exr")))];
        string[] before = TileFiles();
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Build(scene, "passes");
        scene.TileSize = 8;
        (uint, long, long, long) start = Report(scene);

        //Act
        scene.Render(Small);
        (uint TileSize, long TilesKept, long FramesRead, long TilesHeld) first = Report(scene);
        scene.Render(Small with { Width = 40, Height = 20 });
        (uint TileSize, long TilesKept, long FramesRead, long TilesHeld) second = Report(scene);
        string[] after = TileFiles();

        //Assert
        start.Should().Be((8u, 0L, 0L, 0L));
        first.Should().Be((8u, 12L, 1L, 0L));
        second.Should().Be((8u, 12L + 15L, 2L, 0L));
        after.Except(before).Should().BeEmpty();
    }

    // A cancel during a tiled render stops it in its first tile: the render throws OperationCanceledException, the
    // destination is untouched, nothing stays in the tile store, and the next render equals the untiled film.
    [Fact]
    public async Task a_cancelled_tiled_render_stops_mid_tile_and_leaves_the_destination_untouched()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Build(scene, "diffuse");
        scene.TileSize = 8;
        float[] destination = Enumerable.Repeat(-7f, (int)Small.FloatCount).ToArray();
        using CancellationTokenSource cancellation = CancellationTokenSource.CreateLinkedTokenSource(Token);
        ConcurrentQueue<string> statuses = new();
        scene.RenderEventReceived += (_, e) =>
        {
            if (e.Kind == RenderEventKind.Status) statuses.Enqueue(e.Message);
            if (e.Kind == RenderEventKind.Progress && e.SamplesDone > 0) cancellation.Cancel();
        };
        (FilmBuffer expected, _, _) = Render("diffuse", 0);
        Stopwatch timer = Stopwatch.StartNew();

        //Act
        Func<Task> render = () => scene.RenderAsync(Small with { Samples = 1_000_000 }, destination, null, cancellation.Token);
        await render.Should().ThrowAsync<OperationCanceledException>();
        TimeSpan stopped = timer.Elapsed;
        string[] cancelledStatuses = [.. statuses.Distinct()];
        (uint _, long keptAfterCancel, long framesAfterCancel, long heldAfterCancel) = Report(scene);
        FilmBuffer after = await scene.RenderAsync(Small, null, Token);
        output.WriteLine($"cancelled after {stopped.TotalMilliseconds:F0} ms; tiles kept {keptAfterCancel}; status: {string.Join(" | ", cancelledStatuses)}");

        //Assert
        destination.All(value => value == -7f).Should().BeTrue();
        framesAfterCancel.Should().Be(0);
        heldAfterCancel.Should().Be(0);
        cancelledStatuses.Should().Contain(m => m.Contains("Rendered 0/12 Tiles"));
        cancelledStatuses.Should().NotContain(m => m.Contains("Rendered 1/12 Tiles") || m.Contains("Reading full buffer"));
        stopped.Should().BeLessThan(TimeSpan.FromSeconds(30));
        after.Span.SequenceEqual(expected.Span).Should().BeTrue();
        Report(scene).TilesHeld.Should().Be(0);
    }

    // A progressive host (provisional HostFlags.ProgressiveResults) with tiles: each intermediate film places the tile
    // being rendered in the film, beside the tiles rendered before it (the rest is zero), and the last film is the
    // returned film, which equals a plain host's untiled film bit for bit.
    [Fact]
    public void a_progressive_tiled_render_shows_the_tiles_so_far_and_ends_with_the_returned_film()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        using CyclesScene scene = new(CyclesBackend.CpuReference, 1, HostFlags.ProgressiveResults);
        Build(scene, "diffuse");
        scene.TileSize = 16;
        ConcurrentQueue<IntermediateFilm> films = new();
        scene.IntermediateFilmReceived += (_, film) => films.Enqueue(film);
        (FilmBuffer plain, _, _) = Render("diffuse", 0);

        //Act
        FilmBuffer film = scene.Render(Small);

        //Assert
        film.Span.SequenceEqual(plain.Span).Should().BeTrue();
        IntermediateFilm[] received = [.. films];
        received.Length.Should().BeGreaterThanOrEqualTo(5);
        received[^1].Film.Span.SequenceEqual(film.Span).Should().BeTrue();
        // The first film holds only the first tile (pixels 0..15 by 0..15): the other tiles are still zero.
        float[] first = received[0].Film.Span.ToArray();
        first.AsSpan(((5 * 32) + 5) * 4, 4).ToArray().Any(value => value != 0f).Should().BeTrue();
        first.AsSpan(((20 * 32) + 24) * 4, 4).ToArray().Should().Equal(0f, 0f, 0f, 0f);
        received.All(f => f.Film.Width == 32 && f.Film.Height == 24 && f.SamplesTotal == Small.Samples).Should().BeTrue();
    }
}
