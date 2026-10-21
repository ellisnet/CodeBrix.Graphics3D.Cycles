// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.GltfRender;

/// <summary>
/// Compares two float films with the repository's criteria: practical
/// (|a - b| &gt; 5e-5 + 2e-4 |reference| fails) and strict (|a - b| &gt; 1e-7 + 1e-7 |reference|
/// counts a difference). GPU films are expected to pass practical and may show strict differences.
/// </summary>
/// <param name="Count">The number of compared floats.</param>
/// <param name="MaximumAbsoluteError">The largest absolute difference.</param>
/// <param name="PracticalFailures">Floats failing the practical criterion.</param>
/// <param name="StrictDifferences">Floats exceeding the strict criterion.</param>
/// <param name="BitwiseEqual">Whether both films are bit-identical.</param>
public sealed record FilmComparison(int Count, double MaximumAbsoluteError, int PracticalFailures, int StrictDifferences, bool BitwiseEqual)
{
    /// <summary>
    /// Gets a value indicating whether the practical criterion holds for every float.
    /// </summary>
    public bool PracticalPass => PracticalFailures == 0;

    /// <summary>
    /// Compares <paramref name="actual"/> with <paramref name="reference"/>.
    /// </summary>
    /// <param name="actual">The film under test (for example the GPU film).</param>
    /// <param name="reference">The reference film (the CPU reference backend's film).</param>
    /// <returns>The comparison.</returns>
    /// <exception cref="ArgumentException">The films differ in length or hold non-finite values.</exception>
    public static FilmComparison Compare(ReadOnlySpan<float> actual, ReadOnlySpan<float> reference)
    {
        if (actual.Length != reference.Length) throw new ArgumentException("The films differ in size.", nameof(actual));
        double maximum = 0;
        int practical = 0, strict = 0;
        bool bitwise = true;
        for (int i = 0; i < actual.Length; ++i)
        {
            if (!float.IsFinite(actual[i]) || !float.IsFinite(reference[i])) throw new ArgumentException("A film holds a non-finite value at float " + i + ".", nameof(actual));
            if (BitConverter.SingleToInt32Bits(actual[i]) != BitConverter.SingleToInt32Bits(reference[i])) bitwise = false;
            double error = Math.Abs((double)actual[i] - reference[i]);
            maximum = Math.Max(maximum, error);
            if (error > 5e-5 + 2e-4 * Math.Abs(reference[i])) ++practical;
            if (error > 1e-7 + 1e-7 * Math.Abs(reference[i])) ++strict;
        }
        return new FilmComparison(actual.Length, maximum, practical, strict, bitwise);
    }

    /// <inheritdoc/>
    public override string ToString() =>
        $"floats {Count}, max abs error {MaximumAbsoluteError:G6}, practical failures {PracticalFailures}, strict differences {StrictDifferences}, bitwise {(BitwiseEqual ? "equal" : "different")}, practical {(PracticalPass ? "PASS" : "FAIL")}";
}
