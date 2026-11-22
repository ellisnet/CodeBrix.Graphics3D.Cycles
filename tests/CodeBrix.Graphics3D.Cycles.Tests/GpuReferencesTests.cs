// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json.Nodes;
using CodeBrix.Graphics3D.Cycles.GltfRender;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// The rules of the Metal reference recorder, in a scratch folder: they hold on every platform, the recording of real
// Metal films only ever runs on Apple Silicon (docs/TESTING-MACOS-ARM64.md).
public class GpuReferencesTests : IDisposable
{
    private readonly string folder = Path.Combine(Path.GetTempPath(), "codebrix-cycles-references-" + Guid.NewGuid().ToString("N"));

    private static readonly float[] First = [0.25f, 0.5f, 0.75f, 1f];
    private static readonly float[] Second = [0.125f, 0.5f, 0.875f, 1f];

    public void Dispose()
    {
        if (Directory.Exists(folder)) Directory.Delete(folder, true);
    }

    private FilmComparison Judge(GpuReferences.Recording recording, string name, float[] film)
        => GpuReferences.JudgeAgainstRecorded(folder, recording, name, film, FilmComparison.Compare(film, film), _ => { });

    private byte[] FilmBytes(string name) => File.ReadAllBytes(Path.Combine(folder, name + ".rgba.f32"));

    private string RecordedSha(string name)
        => (string)JsonNode.Parse(File.ReadAllText(Path.Combine(folder, "manifest.json")))["references"][name]["sha256"];

    [Theory]
    [InlineData(null, "Off")]
    [InlineData("", "Off")]
    [InlineData("0", "Off")]
    [InlineData("1", "MissingOnly")]
    [InlineData("all", "All")]
    public void ParseRecording_maps_the_documented_values(string value, string expected)
        => GpuReferences.ParseRecording(value).ToString().Should().Be(expected);

    [Theory]
    [InlineData("true")]
    [InlineData("ALL")]
    [InlineData("2")]
    public void ParseRecording_rejects_a_value_that_is_not_a_mode(string value)
    {
        //Act
        Action act = () => GpuReferences.ParseRecording(value);

        //Assert
        act.Should().Throw<ArgumentException>();
    }

    [Fact]
    public void JudgeAgainstRecorded_writes_a_name_that_has_no_reference_yet()
    {
        //Act
        FilmComparison comparison = Judge(GpuReferences.Recording.MissingOnly, "new-scene", First);

        //Assert
        comparison.BitwiseEqual.Should().BeTrue();
        FilmBytes("new-scene").Length.Should().Be(First.Length * sizeof(float));
        RecordedSha("new-scene").Should().NotBeNullOrEmpty();
    }

    [Fact]
    public void JudgeAgainstRecorded_leaves_an_existing_name_alone_and_judges_against_it()
    {
        //Arrange
        Judge(GpuReferences.Recording.MissingOnly, "kept-scene", First);
        byte[] before = FilmBytes("kept-scene");
        string shaBefore = RecordedSha("kept-scene");
        string manifestBefore = File.ReadAllText(Path.Combine(folder, "manifest.json"));

        //Act
        FilmComparison comparison = Judge(GpuReferences.Recording.MissingOnly, "kept-scene", Second);

        //Assert
        FilmBytes("kept-scene").Should().Equal(before);
        RecordedSha("kept-scene").Should().Be(shaBefore);
        File.ReadAllText(Path.Combine(folder, "manifest.json")).Should().Be(manifestBefore);
        comparison.BitwiseEqual.Should().BeFalse();
        comparison.StrictDifferences.Should().Be(2);
    }

    [Fact]
    public void JudgeAgainstRecorded_writes_only_the_missing_name_beside_an_existing_one()
    {
        //Arrange
        Judge(GpuReferences.Recording.MissingOnly, "old-scene", First);
        byte[] before = FilmBytes("old-scene");

        //Act
        Judge(GpuReferences.Recording.MissingOnly, "added-scene", Second);

        //Assert
        FilmBytes("old-scene").Should().Equal(before);
        FilmBytes("added-scene").Should().NotEqual(before);
        RecordedSha("old-scene").Should().NotBe(RecordedSha("added-scene"));
    }

    [Fact]
    public void JudgeAgainstRecorded_re_records_an_existing_name_when_every_name_is_asked_for()
    {
        //Arrange: the reference is written as a file a previous run left, so this run has not recorded the name yet
        Judge(GpuReferences.Recording.MissingOnly, "seed-scene", First);
        string manifest = File.ReadAllText(Path.Combine(folder, "manifest.json")).Replace("seed-scene", "pinned-scene");
        File.WriteAllText(Path.Combine(folder, "manifest.json"), manifest);
        File.Copy(Path.Combine(folder, "seed-scene.rgba.f32"), Path.Combine(folder, "pinned-scene.rgba.f32"));
        string shaBefore = RecordedSha("pinned-scene");

        //Act
        FilmComparison comparison = Judge(GpuReferences.Recording.All, "pinned-scene", Second);

        //Assert
        comparison.BitwiseEqual.Should().BeTrue();
        RecordedSha("pinned-scene").Should().NotBe(shaBefore);
    }

    [Fact]
    public void JudgeAgainstRecorded_keeps_the_first_film_of_a_name_within_one_re_recording_run()
    {
        //Arrange
        Judge(GpuReferences.Recording.All, "shared-scene", First);
        byte[] before = FilmBytes("shared-scene");

        //Act
        FilmComparison comparison = Judge(GpuReferences.Recording.All, "shared-scene", Second);

        //Assert
        FilmBytes("shared-scene").Should().Equal(before);
        comparison.BitwiseEqual.Should().BeFalse();
    }

    [Fact]
    public void JudgeAgainstRecorded_never_writes_when_recording_is_off()
    {
        //Arrange
        Judge(GpuReferences.Recording.MissingOnly, "judged-scene", First);
        List<string> names = [.. Directory.EnumerateFileSystemEntries(folder)];

        //Act
        FilmComparison comparison = Judge(GpuReferences.Recording.Off, "judged-scene", Second);

        //Assert
        comparison.BitwiseEqual.Should().BeFalse();
        Directory.EnumerateFileSystemEntries(folder).Should().BeEquivalentTo(names);
    }
}
