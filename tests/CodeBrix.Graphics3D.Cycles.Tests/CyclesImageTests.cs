// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Image re-upload through the typed facade on the CPU reference backend.
public class CyclesImageTests
{
    private static readonly RenderSettings Small = new(32, 24, 8);

    [Fact]
    public void Update_changes_the_render_and_restoring_the_pixels_restores_it_bitwise()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        (ShaderNode material, DiffuseBsdfNode diffuse, _) = TestScenes.BuildTyped(scene);
        ImageTextureNode texture = material.Add<ImageTextureNode>();
        material.Connect(texture.ColorOutput, diffuse.ColorInput);
        using CyclesImage image = scene.CreateImage<float>(1, 1, ImageFormat.Float4, [.9f, .1f, .1f, 1]);
        texture.Bind(image);
        FilmBuffer red = scene.Render(Small);

        //Act
        image.Update<float>([.1f, .1f, .9f, 1]);
        FilmBuffer blue = scene.Render(Small);
        image.Update<byte>(2, 1, ImageFormat.Byte4, [10, 200, 10, 255, 10, 200, 10, 255]);
        FilmBuffer green = scene.Render(Small);
        image.Update<float>(1, 1, ImageFormat.Float4, [.9f, .1f, .1f, 1]);
        FilmBuffer restored = scene.Render(Small);

        //Assert
        blue.Span.SequenceEqual(red.Span).Should().BeFalse();
        green.Span.SequenceEqual(blue.Span).Should().BeFalse();
        restored.Span.SequenceEqual(red.Span).Should().BeTrue();
        image.Width.Should().Be(1u);
        image.Format.Should().Be(ImageFormat.Float4);
    }

    [Fact]
    public void Update_rejects_a_mismatched_component_type_and_a_released_image()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        CyclesImage image = scene.CreateImage<float>(1, 1, ImageFormat.Float4, [1, 1, 1, 1]);

        //Act
        Action wrongType = () => image.Update<ushort>([1, 2, 3, 4]);
        Action wrongCount = () => image.Update<float>([1, 1, 1]);
        Action released = () => image.Update<float>([1, 1, 1, 1]);

        //Assert
        wrongType.Should().Throw<ArgumentException>();
        wrongCount.Should().Throw<NativeFailure>();
        image.Dispose();
        released.Should().Throw<ObjectDisposedException>();
    }
}
