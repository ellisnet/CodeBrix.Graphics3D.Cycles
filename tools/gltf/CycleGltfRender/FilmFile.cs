// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using CodeBrix.Graphics3D.Cycles.Rendering;

namespace CodeBrix.Graphics3D.Cycles.GltfRender;

/// <summary>
/// Writes a raw film: a <c>.bin</c> of little-endian float32 RGBA (scene-linear, Cycles film
/// order, row 0 at the bottom) plus a small JSON header beside it (<c>.bin.json</c>).
/// </summary>
public static class FilmFile
{
    /// <summary>
    /// Writes the film and its header.
    /// </summary>
    /// <param name="path">The <c>.bin</c> path; the header goes to <c>path + ".json"</c>.</param>
    /// <param name="film">The rendered film.</param>
    /// <param name="backend">The backend the film was rendered on.</param>
    /// <param name="samples">The samples per pixel.</param>
    /// <param name="source">The glTF file the scene came from.</param>
    /// <returns>The SHA-256 of the written <c>.bin</c>.</returns>
    public static string Write(string path, FilmBuffer film, Backend backend, uint samples, string source)
    {
        ArgumentNullException.ThrowIfNull(film);
        byte[] bytes = MemoryMarshal.AsBytes(film.Span).ToArray();
        if (!BitConverter.IsLittleEndian) throw new PlatformNotSupportedException("Films are written little-endian.");
        string directory = Path.GetDirectoryName(Path.GetFullPath(path));
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
        File.WriteAllBytes(path, bytes);
        string sha256 = Convert.ToHexStringLower(SHA256.HashData(bytes));
        var header = new
        {
            format = "float32-le",
            channels = 4,
            layout = "RGBA, scene-linear, Cycles film order (row 0 is the bottom row)",
            width = film.Width,
            height = film.Height,
            samples,
            backend = backend.ToString(),
            source,
            sha256
        };
        File.WriteAllText(path + ".json", JsonSerializer.Serialize(header, new JsonSerializerOptions { WriteIndented = true }));
        return sha256;
    }
}
