// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Numerics;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

public class Transform3x4Tests
{
    [Fact]
    public void CreateTranslation_puts_the_offset_in_the_fourth_column()
    {
        //Arrange
        float[] values = new float[Transform3x4.FloatCount];

        //Act
        Transform3x4.CreateTranslation(1, 2, 3).CopyTo(values);

        //Assert
        values.Should().Equal(1, 0, 0, 1, 0, 1, 0, 2, 0, 0, 1, 3);
    }

    [Fact]
    public void FromMatrix_converts_the_numerics_row_vector_convention()
    {
        //Arrange
        Matrix4x4 matrix = Matrix4x4.CreateRotationZ(.5f) * Matrix4x4.CreateTranslation(4, 5, 6);
        Vector3 point = new(1, 2, 3);
        Vector3 expected = Vector3.Transform(point, matrix);

        //Act
        Transform3x4 transform = Transform3x4.FromMatrix(matrix);
        Vector3 actual = new(
            transform.M11 * point.X + transform.M12 * point.Y + transform.M13 * point.Z + transform.M14,
            transform.M21 * point.X + transform.M22 * point.Y + transform.M23 * point.Z + transform.M24,
            transform.M31 * point.X + transform.M32 * point.Y + transform.M33 * point.Z + transform.M34);

        //Assert
        Vector3.Distance(actual, expected).Should().BeLessThan(1e-5f);
        transform.ToMatrix().Should().Be(matrix);
    }

    [Fact]
    public void FromSpan_rejects_fewer_than_twelve_floats()
    {
        //Act
        Action read = () => Transform3x4.FromSpan(new float[11]);

        //Assert
        read.Should().Throw<ArgumentException>();
    }

    [Fact]
    public void Identity_round_trips_through_a_span()
    {
        //Arrange
        float[] values = new float[Transform3x4.FloatCount];

        //Act
        Transform3x4.Identity.CopyTo(values);

        //Assert
        Transform3x4.FromSpan(values).Should().Be(Transform3x4.Identity);
    }
}
