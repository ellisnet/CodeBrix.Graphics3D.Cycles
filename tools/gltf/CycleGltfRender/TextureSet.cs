// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using CodeBrix.Graphics3D.Gltf2.Schema2;
using CodeBrix.Imaging.PixelFormats;
using ImagingImage = CodeBrix.Imaging.Image;

namespace CodeBrix.Graphics3D.Cycles.GltfRender;

/// <summary>
/// Decoded glTF images as RGBA8, tightly packed rows, bottom row first, encoded values (no
/// colour conversion; Cycles applies the colourspace of the texture node that samples them).
/// <see cref="Decode"/> decodes a model's images with CodeBrix.Imaging; <see cref="Load"/> reads
/// pre-decoded raw files described by a <c>textures.json</c> manifest (each checked against its
/// recorded SHA-256), for example the synthetic textures of <see cref="SampleModel"/>.
/// </summary>
public sealed class TextureSet
{
    private readonly Dictionary<int, DecodedImage> images = new();

    private TextureSet(string directory) => Directory = directory == null ? null : directory;

    /// <summary>Gets the directory the set was loaded from, or null for a decoded model.</summary>
    public string Directory { get; }

    /// <summary>Gets the SHA-256 of the glTF file the images were decoded for, as the manifest records it.</summary>
    public string ModelSha256 { get; private set; }

    /// <summary>Gets the decoder identity (the manifest's record, or the CodeBrix.Imaging version that decoded the model).</summary>
    public string Decoder { get; private set; }

    /// <summary>Gets the decoded images by glTF image index.</summary>
    public IReadOnlyDictionary<int, DecodedImage> Images => images;

    /// <summary>
    /// Decodes every image of <paramref name="model"/> (PNG, JPEG and the other formats
    /// CodeBrix.Imaging reads) to RGBA8 with the rows flipped to bottom-first. When
    /// <paramref name="reviewPath"/> names a licence review (third_party/bench-texture-license-review.json
    /// layout), an image file listed there must match the reviewed SHA-256.
    /// </summary>
    /// <param name="model">The loaded glTF model.</param>
    /// <param name="reviewPath">An optional licence review whose pinned hashes are checked.</param>
    /// <returns>The decoded images.</returns>
    /// <exception cref="InvalidDataException">An image differs from its reviewed hash.</exception>
    public static TextureSet Decode(ModelRoot model, string reviewPath = null)
    {
        ArgumentNullException.ThrowIfNull(model);
        Dictionary<string, string> reviewed = new(StringComparer.Ordinal);
        if (reviewPath != null && File.Exists(reviewPath))
        {
            using JsonDocument review = JsonDocument.Parse(File.ReadAllText(reviewPath));
            foreach (JsonElement texture in review.RootElement.GetProperty("textures").EnumerateArray())
                foreach (JsonElement file in texture.GetProperty("files").EnumerateArray())
                    reviewed[Path.GetFileName(file.GetProperty("file").GetString())] = file.GetProperty("sha256").GetString();
        }
        Assembly imaging = typeof(ImagingImage).Assembly;
        TextureSet set = new(null)
        {
            Decoder = "CodeBrix.Imaging " + (imaging.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? imaging.GetName().Version?.ToString()),
        };
        foreach (Gltf2.Schema2.Image image in model.LogicalImages)
        {
            byte[] encoded = image.Content.Content.ToArray();
            if (encoded.Length == 0) continue;
            string source = image.Content.SourcePath;
            string sourceSha = Convert.ToHexStringLower(SHA256.HashData(encoded));
            bool listed = source != null && reviewed.ContainsKey(Path.GetFileName(source));
            if (listed && reviewed[Path.GetFileName(source)] != sourceSha)
                throw new InvalidDataException($"{source} has SHA-256 {sourceSha}; the licence review pins {reviewed[Path.GetFileName(source)]}.");
            using var decoded = ImagingImage.Load<Rgba32>(encoded);
            int width = decoded.Width, height = decoded.Height, row = width * 4;
            byte[] topDown = new byte[row * height];
            decoded.CopyPixelDataTo(topDown);
            byte[] pixels = new byte[topDown.Length];
            for (int y = 0; y < height; ++y) System.Buffer.BlockCopy(topDown, (height - 1 - y) * row, pixels, y * row, row);
            set.images[image.LogicalIndex] = new DecodedImage(image.LogicalIndex, $"image-{image.LogicalIndex}.byte4", (uint)width, (uint)height, pixels,
                Convert.ToHexStringLower(SHA256.HashData(pixels)), sourceSha, listed) { SourcePath = source };
        }
        return set;
    }

    /// <summary>
    /// Loads a decoded texture directory and verifies every pixel file against the manifest.
    /// </summary>
    /// <param name="directory">The directory holding <c>textures.json</c>.</param>
    /// <returns>The texture set.</returns>
    /// <exception cref="InvalidDataException">A file is missing, has the wrong size or does not match its recorded SHA-256.</exception>
    public static TextureSet Load(string directory)
    {
        ArgumentException.ThrowIfNullOrEmpty(directory);
        string manifestPath = Path.Combine(directory, "textures.json");
        using JsonDocument manifest = JsonDocument.Parse(File.ReadAllText(manifestPath));
        JsonElement root = manifest.RootElement;
        if (!root.TryGetProperty("textures", out JsonElement version) || version.GetInt32() != 1)
            throw new InvalidDataException(manifestPath + " is not a version 1 textures.json manifest.");
        TextureSet set = new(Path.GetFullPath(directory))
        {
            ModelSha256 = root.GetProperty("modelSha256").GetString(),
            Decoder = root.GetProperty("decoder").ToString(),
        };
        foreach (JsonElement entry in root.GetProperty("images").EnumerateArray())
        {
            int index = entry.GetProperty("index").GetInt32();
            string file = entry.GetProperty("file").GetString();
            string format = entry.GetProperty("format").GetString();
            if (format != "byte4") throw new InvalidDataException($"Image {index}: format {format} is not supported (byte4 expected).");
            uint width = entry.GetProperty("width").GetUInt32(), height = entry.GetProperty("height").GetUInt32();
            byte[] pixels = File.ReadAllBytes(Path.Combine(directory, Path.GetFileName(file)));
            if ((ulong)pixels.LongLength != (ulong)width * height * 4) throw new InvalidDataException($"Image {index}: {file} has the wrong size.");
            string sha256 = Convert.ToHexStringLower(SHA256.HashData(pixels));
            if (sha256 != entry.GetProperty("sha256").GetString()) throw new InvalidDataException($"Image {index}: {file} does not match its recorded SHA-256.");
            set.images[index] = new DecodedImage(index, Path.GetFileName(file), width, height, pixels, sha256,
                entry.GetProperty("sourceSha256").GetString(), entry.TryGetProperty("reviewMatch", out JsonElement review) && review.ValueKind == JsonValueKind.True);
        }
        return set;
    }
}

/// <summary>One decoded glTF image (RGBA8, bottom row first).</summary>
/// <param name="Index">The glTF image index.</param>
/// <param name="FileName">The raw pixel file name.</param>
/// <param name="Width">The width in pixels.</param>
/// <param name="Height">The height in pixels.</param>
/// <param name="Pixels">The pixels: width * height * 4 bytes.</param>
/// <param name="Sha256">The SHA-256 of <paramref name="Pixels"/>.</param>
/// <param name="SourceSha256">The SHA-256 of the encoded source image.</param>
/// <param name="ReviewMatch">Whether the source matched a licence review's pinned hash.</param>
public sealed record DecodedImage(int Index, string FileName, uint Width, uint Height, byte[] Pixels, string Sha256, string SourceSha256, bool ReviewMatch)
{
    /// <summary>Gets the encoded source file's path, when the image came from a file.</summary>
    public string SourcePath { get; init; }
}
