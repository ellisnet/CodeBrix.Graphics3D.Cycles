// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Numerics;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// A Cycles affine transform: three rows of four floats, row-major, mapping column vectors
/// (<c>p' = M * [x, y, z, 1]</c>). The translation is the fourth column (<see cref="M14"/>,
/// <see cref="M24"/>, <see cref="M34"/>). This is the layout every transform socket uses.
/// </summary>
/// <param name="M11">Row 1, column 1.</param>
/// <param name="M12">Row 1, column 2.</param>
/// <param name="M13">Row 1, column 3.</param>
/// <param name="M14">Row 1, column 4 (x translation).</param>
/// <param name="M21">Row 2, column 1.</param>
/// <param name="M22">Row 2, column 2.</param>
/// <param name="M23">Row 2, column 3.</param>
/// <param name="M24">Row 2, column 4 (y translation).</param>
/// <param name="M31">Row 3, column 1.</param>
/// <param name="M32">Row 3, column 2.</param>
/// <param name="M33">Row 3, column 3.</param>
/// <param name="M34">Row 3, column 4 (z translation).</param>
public readonly record struct Transform3x4(
    float M11, float M12, float M13, float M14,
    float M21, float M22, float M23, float M24,
    float M31, float M32, float M33, float M34)
{
    /// <summary>The number of floats a transform occupies in a socket (12).</summary>
    public const int FloatCount = 12;

    /// <summary>
    /// Gets the identity transform.
    /// </summary>
    public static Transform3x4 Identity { get; } = new(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0);

    /// <summary>
    /// Creates a pure translation.
    /// </summary>
    /// <param name="x">The x translation.</param>
    /// <param name="y">The y translation.</param>
    /// <param name="z">The z translation.</param>
    /// <returns>The transform.</returns>
    public static Transform3x4 CreateTranslation(float x, float y, float z) => new(1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z);

    /// <summary>
    /// Converts a <see cref="Matrix4x4"/> (System.Numerics row-vector convention, translation in
    /// <see cref="Matrix4x4.M41"/>..<see cref="Matrix4x4.M43"/>) into a Cycles transform. The
    /// matrix's fourth column is ignored.
    /// </summary>
    /// <param name="matrix">The matrix.</param>
    /// <returns>The equivalent Cycles transform.</returns>
    public static Transform3x4 FromMatrix(Matrix4x4 matrix) => new(
        matrix.M11, matrix.M21, matrix.M31, matrix.M41,
        matrix.M12, matrix.M22, matrix.M32, matrix.M42,
        matrix.M13, matrix.M23, matrix.M33, matrix.M43);

    /// <summary>
    /// Converts this transform to a <see cref="Matrix4x4"/> in the System.Numerics row-vector convention.
    /// </summary>
    /// <returns>The matrix.</returns>
    public Matrix4x4 ToMatrix() => new(
        M11, M21, M31, 0,
        M12, M22, M32, 0,
        M13, M23, M33, 0,
        M14, M24, M34, 1);

    /// <summary>
    /// Reads a transform from twelve row-major floats.
    /// </summary>
    /// <param name="values">At least twelve floats.</param>
    /// <returns>The transform.</returns>
    /// <exception cref="ArgumentException"><paramref name="values"/> holds fewer than twelve floats.</exception>
    public static Transform3x4 FromSpan(ReadOnlySpan<float> values)
    {
        if (values.Length < FloatCount) throw new ArgumentException("A transform needs twelve floats.", nameof(values));
        return new(values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7],
                   values[8], values[9], values[10], values[11]);
    }

    /// <summary>
    /// Writes the twelve row-major floats of this transform.
    /// </summary>
    /// <param name="destination">At least twelve floats.</param>
    /// <exception cref="ArgumentException"><paramref name="destination"/> holds fewer than twelve floats.</exception>
    public void CopyTo(Span<float> destination)
    {
        if (destination.Length < FloatCount) throw new ArgumentException("A transform needs twelve floats.", nameof(destination));
        destination[0] = M11; destination[1] = M12; destination[2] = M13; destination[3] = M14;
        destination[4] = M21; destination[5] = M22; destination[6] = M23; destination[7] = M24;
        destination[8] = M31; destination[9] = M32; destination[10] = M33; destination[11] = M34;
    }
}
