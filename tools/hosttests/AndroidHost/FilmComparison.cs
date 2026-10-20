// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
using System.Runtime.InteropServices;
using System.Security.Cryptography;

internal sealed record FilmComparison(string Film, string Reference, string Sha256, bool BitwiseEqual,
    double MaximumAbsoluteError, int PracticalFailures, int StrictDifferences, bool StrictRequired)
{
    public static FilmComparison Check(byte[] actual, string referencePath, string outputPath, bool strictRequired)
    {
        byte[] expected = File.ReadAllBytes(referencePath);
        if (actual.Length != 3072 * sizeof(float) || expected.Length != actual.Length)
            throw new Exception("Film dimensions differ: " + outputPath);
        ReadOnlySpan<float> a = MemoryMarshal.Cast<byte, float>(actual), b = MemoryMarshal.Cast<byte, float>(expected);
        double maximum = 0;
        int practical = 0, strict = 0;
        for (int i = 0; i < a.Length; ++i)
        {
            if (!float.IsFinite(a[i]) || !float.IsFinite(b[i])) throw new Exception("Nonfinite film: " + outputPath);
            double error = Math.Abs((double)a[i] - b[i]);
            maximum = Math.Max(maximum, error);
            if (error > 5e-5 + 2e-4 * Math.Abs(b[i])) ++practical;
            if (error > 1e-7 + 1e-7 * Math.Abs(b[i])) ++strict;
        }
        // Preserve the completed film even if an acceptance check fails.
        File.WriteAllBytes(outputPath, actual);
        FilmComparison result = new(outputPath, referencePath, Convert.ToHexStringLower(SHA256.HashData(actual)),
            actual.AsSpan().SequenceEqual(expected), maximum, practical, strict, strictRequired);
        if (practical != 0 || (strictRequired && strict != 0))
            throw new Exception("Film comparison failed: " + System.Text.Json.JsonSerializer.Serialize(result));
        return result;
    }
}
