// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using CodeBrix.Graphics3D.Cycles.Bundles;

namespace CodeBrix.Graphics3D.Cycles.GltfRender;

/// <summary>
/// Writes <c>bundle-manifest.json</c> beside a saved bundle: the source model and its SHA-256, the
/// decoder identity, every decoded image's source path, source SHA-256 and licence-review match,
/// and the SHA-256 of the scene document and of every raw file. A bundle copied to another
/// machine is verified against it.
/// </summary>
public static class BundleManifest
{
    /// <summary>The manifest's file name.</summary>
    public const string FileName = "bundle-manifest.json";

    /// <summary>Writes the manifest into <paramref name="directory"/>.</summary>
    /// <param name="directory">The directory the bundle was saved to.</param>
    /// <param name="bundle">The bundle.</param>
    /// <param name="model">The source glTF file.</param>
    /// <param name="textures">The decoded images, or null.</param>
    public static void Write(string directory, OracleSceneBundle bundle, string model, TextureSet textures)
    {
        static string Sha(byte[] data) => System.Convert.ToHexStringLower(SHA256.HashData(data));
        JsonObject files = new() { ["scene.json"] = Sha(File.ReadAllBytes(Path.Combine(directory, "scene.json"))) };
        foreach (string name in bundle.FileNames) files[name] = Sha(bundle.ReadFile(name));
        JsonArray images = new();
        if (textures != null)
            foreach (DecodedImage image in textures.Images.Values.OrderBy(i => i.Index))
                images.Add(new JsonObject
                {
                    ["index"] = image.Index, ["file"] = image.FileName, ["source"] = image.SourcePath == null ? null : Path.GetFileName(image.SourcePath),
                    ["sourceSha256"] = image.SourceSha256, ["licenceReviewMatch"] = image.ReviewMatch, ["width"] = image.Width, ["height"] = image.Height,
                    ["format"] = "byte4", ["origin"] = "lower-left", ["sha256"] = image.Sha256,
                });
        JsonObject manifest = new()
        {
            ["bundleManifest"] = 1, ["copyright"] = "Copyright (c) 2026 Jeremy Ellis and contributors",
            ["note"] = "Generated from external assets; copy the bundle with this manifest, do not commit the raw files.",
            ["sceneId"] = bundle.Id, ["model"] = Path.GetFileName(model), ["modelSha256"] = Sha(File.ReadAllBytes(model)),
            ["decoder"] = textures?.Decoder, ["images"] = images, ["files"] = files,
        };
        File.WriteAllText(Path.Combine(directory, FileName), manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = true, NewLine = "\n" }) + "\n");
    }
}
