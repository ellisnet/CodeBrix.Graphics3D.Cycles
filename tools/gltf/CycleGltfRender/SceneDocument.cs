// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using CodeBrix.Graphics3D.Cycles.Bundles;
using CodeBrix.Graphics3D.Cycles.Rendering;

namespace CodeBrix.Graphics3D.Cycles.GltfRender;

/// <summary>
/// Builds a <c>cbcOracleScene</c> version 1 document (and the raw files it references) entry by
/// entry, in the order the scene must be built. Socket, node-type and enum names are the
/// registered Cycles names.
/// </summary>
internal sealed class SceneDocument
{
    private readonly JsonArray nodes = new();
    private readonly JsonArray images = new();
    private readonly Dictionary<string, byte[]> files = new(StringComparer.Ordinal);

    /// <summary>Adds an entry that addresses a scene singleton (integrator, camera, film, background).</summary>
    internal JsonObject Scene(string handle, string singleton) => Add(new JsonObject { ["handle"] = handle, ["scene"] = singleton });

    /// <summary>Adds an entry that creates a node of a registered type.</summary>
    internal JsonObject Create(string handle, string type) => Add(new JsonObject { ["handle"] = handle, ["create"] = type });

    private JsonObject Add(JsonObject entry)
    {
        nodes.Add(entry);
        return entry;
    }

    /// <summary>Sets one socket of an entry (or of a shader graph node).</summary>
    internal static void Set(JsonObject entry, string socket, JsonNode value)
    {
        if (entry["set"] is not JsonObject set) entry["set"] = set = new JsonObject();
        set[socket] = value;
    }

    /// <summary>Starts (or returns) the shader graph of a shader entry.</summary>
    internal static Graph GraphOf(JsonObject shaderEntry)
    {
        if (shaderEntry["graph"] is not JsonObject graph)
            shaderEntry["graph"] = graph = new JsonObject { ["nodes"] = new JsonArray(), ["connect"] = new JsonArray() };
        return new Graph(graph);
    }

    /// <summary>Adds a raw file the document references.</summary>
    internal void AddFile(string name, byte[] data) => files.Add(name, data);

    /// <summary>Adds a raw float32 file the document references.</summary>
    internal void AddFloats(string name, ReadOnlySpan<float> values) => AddFile(name, ToLittleEndian(MemoryMarshal.AsBytes(values)));

    /// <summary>Adds a raw uint32 file the document references.</summary>
    internal void AddUInts(string name, ReadOnlySpan<uint> values) => AddFile(name, ToLittleEndian(MemoryMarshal.AsBytes(values)));

    private static byte[] ToLittleEndian(ReadOnlySpan<byte> bytes)
    {
        if (!BitConverter.IsLittleEndian) throw new PlatformNotSupportedException("Bundles are written on little-endian hosts only.");
        return bytes.ToArray();
    }

    /// <summary>Adds an image entry whose raw pixels are the file <paramref name="file"/>.</summary>
    internal void AddImage(string handle, uint width, uint height, string format, string file, string colorspace)
    {
        images.Add(new JsonObject
        {
            ["handle"] = handle, ["width"] = width, ["height"] = height, ["format"] = format, ["file"] = file, ["colorspace"] = colorspace,
        });
    }

    /// <summary>A JSON number array.</summary>
    internal static JsonArray Floats(params float[] values)
    {
        JsonArray array = new();
        foreach (float value in values) array.Add(value);
        return array;
    }

    /// <summary>The 12 floats of a transform (3 rows of 4).</summary>
    internal static JsonArray Transform(Transform3x4 transform)
    {
        float[] values = new float[12];
        transform.CopyTo(values);
        return Floats(values);
    }

    /// <summary>Finishes the document and wraps it with its files as a bundle.</summary>
    internal OracleSceneBundle ToBundle(string id, string description, string sourceFixture, RenderSettings render, uint repeats)
    {
        JsonObject root = new()
        {
            ["cbcOracleScene"] = 1,
            ["id"] = id,
            ["description"] = description,
            ["sourceFixture"] = sourceFixture,
            ["render"] = new JsonObject { ["width"] = render.Width, ["height"] = render.Height, ["samples"] = render.Samples, ["repeats"] = repeats },
            ["colorConfig"] = null,
            ["nodes"] = nodes.DeepClone(),
            ["images"] = images.DeepClone(),
        };
        // LF on every platform: the committed bench documents are compared byte for byte.
        string json = root.ToJsonString(new JsonSerializerOptions { WriteIndented = true, NewLine = "\n" }) + "\n";
        return OracleSceneBundle.FromJson(json, files);
    }

    /// <summary>A shader graph under construction.</summary>
    internal readonly struct Graph
    {
        private readonly JsonObject graph;

        internal Graph(JsonObject graph) => this.graph = graph;

        /// <summary>Adds a shader node and returns its entry (for <see cref="Set"/>).</summary>
        internal JsonObject Node(string handle, string type)
        {
            JsonObject node = new() { ["handle"] = handle, ["type"] = type };
            ((JsonArray)graph["nodes"]).Add(node);
            return node;
        }

        /// <summary>Links an output socket (UI name) to an input socket (UI name); "output" is the graph output.</summary>
        internal void Connect(string from, string output, string to, string input) =>
            ((JsonArray)graph["connect"]).Add(new JsonArray(from, output, to, input));
    }
}
