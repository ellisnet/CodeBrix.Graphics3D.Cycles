// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Threading;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Interop;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Progressive results (provisional HostFlags.ProgressiveResults, CBC_HOST_PROGRESSIVE_RESULTS) on the CPU reference
// backend. A progressive host renders with Cycles' display updates instead of its headless scheduling; its final film
// must equal a plain host's film bit for bit, and its intermediate films arrive through IntermediateFilmReceived.
public class ProgressiveResultTests
{
    private static CancellationToken Token => TestContext.Current.CancellationToken;

    private static readonly RenderSettings Small = new(32, 24, 8);

    private static void Build(CyclesScene scene, string variant)
    {
        TestScenes.BuildTyped(scene);
        IntegratorNode integrator = scene.Integrator;
        switch (variant)
        {
            case "diffuse":
                break;
            case "adaptive":
                integrator.UseAdaptiveSampling = true;
                integrator.AdaptiveThreshold = 0.02f;
                integrator.AdaptiveMinSamples = 8;
                break;
            case "volume":
            {
                // A scattering box in front of the triangle, lit by the world.
                ShaderNode fog = scene.CreateShader();
                ScatterVolumeNode scatter = fog.Add<ScatterVolumeNode>();
                scatter.Density = 1.5f;
                scatter.Color = new Vector3(.8f, .7f, .6f);
                fog.Connect(scatter.VolumeOutput, fog.Output.VolumeInput);
                float[] corners = [-1.2f, -.8f, -1.6f, 1.2f, -.8f, -1.6f, 1.2f, .8f, -1.6f, -1.2f, .8f, -1.6f,
                                   -1.2f, -.8f, -.4f, 1.2f, -.8f, -.4f, 1.2f, .8f, -.4f, -1.2f, .8f, -.4f];
                uint[] faces = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 2, 3, 7, 2, 7, 6, 1, 2, 6, 1, 6, 5, 0, 4, 7, 0, 7, 3];
                scene.CreateObject(scene.CreateMesh(corners, faces, fog));
                integrator.MaxVolumeBounce = 2;
                break;
            }
            case "denoised":
                integrator.UseDenoise = true;
                break;
            case "camera-border":
                scene.Camera.BorderLeft = .25f;
                scene.Camera.BorderRight = .75f;
                scene.Camera.BorderBottom = .25f;
                scene.Camera.BorderTop = .625f;
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(variant));
        }
    }

    private static RenderSettings SettingsOf(string variant) => variant switch
    {
        "adaptive" => new RenderSettings(32, 24, 64),
        "volume" or "denoised" => new RenderSettings(32, 24, 16),
        _ => Small,
    };

    private static CyclesScene CreateProgressiveScene()
    {
        TestScenes.RequireNativeHost();
        return new CyclesScene(CyclesBackend.CpuReference, 1, HostFlags.ProgressiveResults);
    }

    private static FilmBuffer RenderPlain(string variant)
    {
        using CyclesScene scene = TestScenes.CreateCpuScene();
        Build(scene, variant);
        List<RenderEvent> events = [];
        scene.RenderEventReceived += (_, e) => events.Add(e);
        FilmBuffer film = scene.Render(SettingsOf(variant));
        events.Should().NotContain(e => e.Kind == RenderEventKind.Film);
        return film;
    }

    [Theory]
    [InlineData("diffuse")]
    [InlineData("adaptive")]
    [InlineData("volume")]
    [InlineData("denoised")]
    [InlineData("camera-border")]
    public void the_final_film_of_a_progressive_host_equals_a_plain_hosts_bit_for_bit(string variant)
    {
        //Arrange
        using CyclesScene scene = CreateProgressiveScene();
        Build(scene, variant);
        ConcurrentQueue<IntermediateFilm> films = new();
        List<RenderEvent> events = [];
        scene.IntermediateFilmReceived += (_, film) => films.Enqueue(film);
        scene.RenderEventReceived += (_, e) => events.Add(e);
        RenderSettings settings = SettingsOf(variant);

        //Act
        FilmBuffer film = scene.Render(settings);
        FilmBuffer plain = RenderPlain(variant);
        int afterRender = films.Count;
        Thread.Sleep(50);

        //Assert
        film.Span.SequenceEqual(plain.Span).Should().BeTrue();
        IntermediateFilm[] received = [.. films];
        received.Length.Should().BeGreaterThanOrEqualTo(2);
        films.Count.Should().Be(afterRender);
        received.All(f => f.Film.Width == 32 && f.Film.Height == 24 && f.Film.Components == 4 && f.SamplesTotal == settings.Samples).Should().BeTrue();
        received.Select(f => f.SamplesDone).Should().BeInAscendingOrder();
        received[0].SamplesDone.Should().Be(1u);
        received[^1].Film.Span.SequenceEqual(film.Span).Should().BeTrue();
        received[0].Film.Span.SequenceEqual(film.Span).Should().BeFalse();
        events.Should().NotContain(e => e.Kind == RenderEventKind.Film);
        if (variant != "adaptive") received[^1].SamplesDone.Should().Be(settings.Samples);
        if (variant == "camera-border")
        {
            // The region (pixels 8..23 by 6..14) is placed in the whole film; every intermediate film is zero outside it.
            foreach (IntermediateFilm intermediate in received)
                for (int y = 0; y < 24; y++)
                    for (int x = 0; x < 32; x++)
                        if (x < 8 || x >= 24 || y < 6 || y >= 15)
                            intermediate.Film.Span.Slice((y * 32 + x) * 4, 4).ToArray().Should().Equal(0f, 0f, 0f, 0f);
            received[0].Film.Span.Slice((10 * 32 + 16) * 4, 4).ToArray().Any(value => value != 0f).Should().BeTrue();
        }
    }

    [Fact]
    public void every_render_of_a_progressive_host_sends_films_and_repeats_bit_for_bit()
    {
        //Arrange
        using CyclesScene scene = CreateProgressiveScene();
        (ShaderNode _, DiffuseBsdfNode diffuse, MeshNode _) = TestScenes.BuildTyped(scene);
        int count = 0;
        scene.IntermediateFilmReceived += (_, _) => Interlocked.Increment(ref count);

        //Act
        FilmBuffer first = scene.Render(Small);
        int firstCount = count;
        FilmBuffer second = scene.Render(Small);
        diffuse.Roughness = .8f;
        FilmBuffer edited = scene.Render(Small);
        using CyclesScene plain = TestScenes.CreateCpuScene();
        (ShaderNode _, DiffuseBsdfNode plainDiffuse, MeshNode _) = TestScenes.BuildTyped(plain);
        plainDiffuse.Roughness = .8f;
        FilmBuffer expected = plain.Render(Small);

        //Assert
        firstCount.Should().BeGreaterThanOrEqualTo(2);
        count.Should().BeGreaterThanOrEqualTo(3 * 2);
        second.Span.SequenceEqual(first.Span).Should().BeTrue();
        edited.Span.SequenceEqual(expected.Span).Should().BeTrue();
    }

    [Fact]
    public void a_plain_host_and_a_bake_send_no_film_and_a_bake_on_a_progressive_host_equals_a_plain_bake()
    {
        //Arrange
        using CyclesScene progressive = CreateProgressiveScene();
        using CyclesScene plain = TestScenes.CreateCpuScene();
        int films = 0;
        // The lit triangle scene and a quad behind it, the bake target, with a UV layout over the unit square.
        ObjectNode Target(CyclesScene scene)
        {
            (ShaderNode material, DiffuseBsdfNode _, MeshNode _) = TestScenes.BuildTyped(scene);
            MeshNode quad = scene.CreateMesh([-1f, -1f, 1f, 1f, -1f, 1f, 1f, 1f, 1f, -1f, 1f, 1f], [0, 1, 2, 0, 2, 3], material);
            quad.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, [0f, 0f, 1f, 0f, 1f, 1f, 0f, 0f, 1f, 1f, 0f, 1f]);
            scene.IntermediateFilmReceived += (_, _) => Interlocked.Increment(ref films);
            return scene.CreateObject(quad);
        }
        ObjectNode progressiveTarget = Target(progressive);
        ObjectNode plainTarget = Target(plain);
        BakeSettings bake = new(16, 16, 8, BakeType.Combined);

        //Act
        FilmBuffer plainRender = plain.Render(Small);
        int plainFilms = films;
        FilmBuffer progressiveBake = progressive.Bake(progressiveTarget, bake);
        FilmBuffer plainBake = plain.Bake(plainTarget, bake);

        //Assert
        plainRender.Span.ToArray().All(float.IsFinite).Should().BeTrue();
        plainFilms.Should().Be(0);
        films.Should().Be(0);
        progressiveBake.Span.SequenceEqual(plainBake.Span).Should().BeTrue();
    }

    [Fact]
    public void a_film_handler_cannot_call_the_host_and_one_that_throws_cancels_the_render()
    {
        //Arrange
        using CyclesScene scene = CreateProgressiveScene();
        TestScenes.BuildTyped(scene);
        NativeStatus? reentrant = null;
        void Reenter(object sender, IntermediateFilm film)
        {
            try
            {
                scene.Host.SceneNode("camera");
            }
            catch (NativeFailure failure)
            {
                reentrant ??= failure.NativeStatus;
            }
        }
        void Throw(object sender, IntermediateFilm film) => throw new InvalidOperationException("stop at the first film");
        float[] destination = Enumerable.Repeat(-7f, (int)Small.FloatCount).ToArray();

        //Act
        scene.IntermediateFilmReceived += Reenter;
        FilmBuffer reference = scene.Render(Small);
        scene.IntermediateFilmReceived -= Reenter;
        scene.IntermediateFilmReceived += Throw;
        Action render = () => scene.Render(Small, destination);
        render.Should().Throw<InvalidOperationException>().WithMessage("stop at the first film");
        scene.IntermediateFilmReceived -= Throw;
        FilmBuffer after = scene.Render(Small);

        //Assert
        reentrant.Should().Be(NativeStatus.NotReady);
        destination.All(value => value == -7f).Should().BeTrue();
        after.Span.SequenceEqual(reference.Span).Should().BeTrue();
    }

    [Fact]
    public async Task a_cancelled_progressive_render_stops_and_sends_no_film_afterwards()
    {
        //Arrange
        using CyclesScene scene = CreateProgressiveScene();
        TestScenes.BuildTyped(scene);
        using CancellationTokenSource cancellation = CancellationTokenSource.CreateLinkedTokenSource(Token);
        int films = 0, filmsAfterCancel = 0;
        scene.IntermediateFilmReceived += (_, _) =>
        {
            if (cancellation.IsCancellationRequested) Interlocked.Increment(ref filmsAfterCancel);
            if (Interlocked.Increment(ref films) == 1) cancellation.Cancel();
        };

        //Act
        Func<Task> render = () => scene.RenderAsync(Small with { Samples = 1_000_000 }, null, cancellation.Token);

        //Assert
        await render.Should().ThrowAsync<OperationCanceledException>();
        films.Should().Be(1);
        filmsAfterCancel.Should().Be(0);
        scene.Render(Small).Span.ToArray().All(float.IsFinite).Should().BeTrue();
    }

    [Fact]
    public void unknown_host_flags_are_refused_and_the_status_code_tier_creates_a_progressive_host()
    {
        //Arrange
        TestScenes.RequireNativeHost();

        //Act
        Action unknown = () => new CyclesHost(Backend.CpuReference, 1, (HostFlags)2).Dispose();
        NativeStatus refused = CyclesAbi.Create(Backend.CpuReference, 1, (HostFlags)6, out ulong none);
        string message = CyclesAbi.LastError();
        NativeStatus created = CyclesAbi.Create(Backend.CpuReference, 1, HostFlags.ProgressiveResults, out ulong host);
        CyclesAbi.Destroy(host);

        //Assert
        unknown.Should().Throw<NativeFailure>().Which.NativeStatus.Should().Be(NativeStatus.Invalid);
        refused.Should().Be(NativeStatus.Invalid);
        none.Should().Be(0ul);
        message.Should().Contain("Unknown configuration flags");
        created.Should().Be(NativeStatus.Ok);
        host.Should().NotBe(0ul);
    }
}
