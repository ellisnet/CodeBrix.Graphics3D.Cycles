// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using CodeBrix.Graphics3D.Cycles.Rendering;

namespace CodeBrix.Graphics3D.Cycles.Bundles;

/// <summary>
/// A self-contained scene description in the <c>cbcOracleScene</c> version 1 format: one JSON
/// document (scene singletons, created nodes, shader graphs, meshes, attributes and images,
/// addressed by their registered Cycles names) plus the raw files it names (pixel files and,
/// optionally, geometry and attribute arrays). The same bundle drives the C oracle runner and
/// this managed loader, so one scene definition renders identically through both.
/// </summary>
/// <remarks>
/// <para>Load a bundle with <see cref="Load(string)"/> (a JSON file and the files beside it) or
/// build one in memory with <see cref="FromJson(string, IReadOnlyDictionary{string, byte[]})"/>;
/// then <see cref="Apply"/> builds it into a fresh <see cref="CyclesScene"/> through the
/// by-name host tier and <see cref="Render"/> gives the render settings it asks for.</para>
/// <para>Raw files are little-endian and tightly packed: pixel files are rows bottom to top in
/// the declared <c>format</c>; <c>positionsFile</c>, <c>indicesFile</c> and attribute <c>file</c>
/// hold float32 positions, uint32 indices and float32 (or, for byte attributes, raw byte)
/// values. Anything the format cannot express fails with the socket or entry named.</para>
/// </remarks>
public sealed class OracleSceneBundle
{
    private readonly Func<string, byte[]> readFile;
    private readonly IReadOnlyList<string> fileNames;

    private OracleSceneBundle(string json, Func<string, byte[]> readFile, IReadOnlyList<string> fileNames)
    {
        Json = json;
        this.readFile = readFile;
        this.fileNames = fileNames;
        using JsonDocument document = Parse(json);
        JsonElement root = document.RootElement;
        if (!root.TryGetProperty("cbcOracleScene", out JsonElement version) || version.ValueKind != JsonValueKind.Number || version.GetInt32() != 1)
            throw new FormatException("Not a cbcOracleScene version 1 document.");
        Id = root.TryGetProperty("id", out JsonElement id) ? id.GetString() : null;
        Description = root.TryGetProperty("description", out JsonElement description) && description.ValueKind == JsonValueKind.String ? description.GetString() : null;
        JsonElement render = Required(root, "render");
        uint width = Required(render, "width").GetUInt32(), height = Required(render, "height").GetUInt32(), samples = Required(render, "samples").GetUInt32();
        Render = new RenderSettings(width, height, samples);
        Repeats = render.TryGetProperty("repeats", out JsonElement repeats) ? repeats.GetUInt32() : 2u;
        ColorConfig = root.TryGetProperty("colorConfig", out JsonElement color) && color.ValueKind == JsonValueKind.String ? color.GetString() : null;
    }

    /// <summary>Gets the scene document text.</summary>
    public string Json { get; }

    /// <summary>Gets the scene id (the document's <c>id</c>).</summary>
    public string Id { get; }

    /// <summary>Gets the document's <c>description</c>, or null.</summary>
    public string Description { get; }

    /// <summary>Gets the film size and samples the document asks for (<c>render</c>).</summary>
    public RenderSettings Render { get; }

    /// <summary>Gets how many times the document asks the unchanged scene to be rendered (<c>render.repeats</c>, default 2).</summary>
    public uint Repeats { get; }

    /// <summary>
    /// Gets the document's <c>colorConfig</c> (an OCIO configuration path relative to the
    /// document), or null for scene-linear output without a configuration. Apply it with
    /// <see cref="CyclesHost.ConfigureColor"/> before creating the scene; <see cref="Apply"/> does not.
    /// </summary>
    public string ColorConfig { get; }

    /// <summary>Gets the names of the raw files the document references, in document order.</summary>
    public IReadOnlyList<string> FileNames => fileNames;

    /// <summary>
    /// Loads a bundle from its JSON file; referenced raw files are read from the same directory
    /// when <see cref="Apply"/> or <see cref="ReadFile"/> needs them.
    /// </summary>
    /// <param name="path">The scene JSON file.</param>
    /// <returns>The bundle.</returns>
    /// <exception cref="FormatException">The file is not a cbcOracleScene version 1 document.</exception>
    public static OracleSceneBundle Load(string path)
    {
        ArgumentException.ThrowIfNullOrEmpty(path);
        string full = Path.GetFullPath(path);
        string directory = Path.GetDirectoryName(full);
        string json = File.ReadAllText(full);
        return new OracleSceneBundle(json, name => File.ReadAllBytes(Path.Combine(directory, CheckName(name))), ReferencedFiles(json));
    }

    /// <summary>
    /// Creates a bundle from a document and the raw files it references, held in memory.
    /// </summary>
    /// <param name="json">The cbcOracleScene version 1 document.</param>
    /// <param name="files">The referenced raw files by name (the names used in the document).</param>
    /// <returns>The bundle.</returns>
    /// <exception cref="FormatException">The text is not a cbcOracleScene version 1 document.</exception>
    public static OracleSceneBundle FromJson(string json, IReadOnlyDictionary<string, byte[]> files)
    {
        ArgumentNullException.ThrowIfNull(json);
        ArgumentNullException.ThrowIfNull(files);
        Dictionary<string, byte[]> copy = new(files, StringComparer.Ordinal);
        return new OracleSceneBundle(json, name => copy.TryGetValue(name, out byte[] data) ? data : throw new FileNotFoundException("The bundle has no file named " + name), ReferencedFiles(json));
    }

    /// <summary>Reads one referenced raw file.</summary>
    /// <param name="name">The file name used in the document.</param>
    /// <returns>The file's bytes.</returns>
    public byte[] ReadFile(string name) => readFile(name);

    /// <summary>
    /// Writes the bundle (the document as <paramref name="sceneFileName"/> and every referenced
    /// file beside it) into a new or empty directory. Existing files are never overwritten.
    /// </summary>
    /// <param name="directory">The destination directory (created when missing; must be empty).</param>
    /// <param name="sceneFileName">The document's file name.</param>
    /// <returns>The path of the written document.</returns>
    /// <exception cref="IOException">The directory is not empty.</exception>
    public string Save(string directory, string sceneFileName = "scene.json")
    {
        ArgumentException.ThrowIfNullOrEmpty(directory);
        ArgumentException.ThrowIfNullOrEmpty(sceneFileName);
        if (Directory.Exists(directory) && Directory.EnumerateFileSystemEntries(directory).Any())
            throw new IOException("The bundle directory is not empty; use a new directory: " + directory);
        Directory.CreateDirectory(directory);
        foreach (string name in fileNames) File.WriteAllBytes(Path.Combine(directory, CheckName(name)), readFile(name));
        string scenePath = Path.Combine(directory, CheckName(sceneFileName));
        File.WriteAllText(scenePath, Json);
        return scenePath;
    }

    /// <summary>
    /// Builds the scene described by the document into <paramref name="scene"/> through the
    /// by-name host tier: images first, then every node entry in order. Call it on a fresh scene.
    /// </summary>
    /// <param name="scene">The scene to fill.</param>
    /// <returns>The native nodes created or addressed, by document handle.</returns>
    /// <exception cref="FormatException">The document is malformed (the entry is named).</exception>
    /// <exception cref="InvalidOperationException">The native host refused a call; the message names the entry and
    /// socket, and <see cref="Exception.InnerException"/> is the <see cref="NativeFailure"/>.</exception>
    public IReadOnlyDictionary<string, Node> Apply(CyclesScene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        using JsonDocument document = Parse(Json);
        Builder builder = new(scene.Host, readFile);
        try
        {
            if (document.RootElement.TryGetProperty("images", out JsonElement images))
                foreach (JsonElement image in images.EnumerateArray()) builder.Image(image);
            foreach (JsonElement entry in Required(document.RootElement, "nodes").EnumerateArray()) builder.Entry(entry);
        }
        finally
        {
            builder.ReleaseImages();
        }
        return builder.Nodes;
    }

    private static JsonDocument Parse(string json) => JsonDocument.Parse(json, new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Disallow });

    private static JsonElement Required(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) ? value : throw new FormatException($"Missing '{name}'.");

    private static string CheckName(string name)
    {
        if (string.IsNullOrEmpty(name) || name != Path.GetFileName(name) || name is "." or "..")
            throw new FormatException("Bundle file names must be plain names beside the document: " + name);
        return name;
    }

    private static IReadOnlyList<string> ReferencedFiles(string json)
    {
        List<string> names = new();
        void Add(JsonElement owner, string property)
        {
            if (owner.TryGetProperty(property, out JsonElement value) && value.ValueKind == JsonValueKind.String && !names.Contains(value.GetString()))
                names.Add(CheckName(value.GetString()));
        }
        using JsonDocument document = Parse(json);
        JsonElement root = document.RootElement;
        if (root.TryGetProperty("images", out JsonElement images))
            foreach (JsonElement image in images.EnumerateArray()) Add(image, "file");
        if (root.TryGetProperty("nodes", out JsonElement nodes))
            foreach (JsonElement entry in nodes.EnumerateArray())
            {
                if (!entry.TryGetProperty("mesh", out JsonElement mesh)) continue;
                Add(mesh, "positionsFile");
                Add(mesh, "indicesFile");
                if (mesh.TryGetProperty("attributes", out JsonElement attributes))
                    foreach (JsonElement attribute in attributes.EnumerateArray()) Add(attribute, "file");
            }
        return names;
    }

    private sealed class Builder
    {
        private static readonly Dictionary<string, (ImageFormat Format, int Bytes)> Formats = new(StringComparer.Ordinal)
        {
            ["float"] = (ImageFormat.Float, 4), ["float4"] = (ImageFormat.Float4, 16), ["half"] = (ImageFormat.Half, 2),
            ["half4"] = (ImageFormat.Half4, 8), ["byte"] = (ImageFormat.Byte, 1), ["byte4"] = (ImageFormat.Byte4, 4),
            ["ushort"] = (ImageFormat.UShort, 2), ["ushort4"] = (ImageFormat.UShort4, 8),
        };
        private static readonly Dictionary<string, AttributeValue> Values = new(StringComparer.Ordinal)
        {
            ["float"] = AttributeValue.Float, ["color"] = AttributeValue.Color, ["vector"] = AttributeValue.Vector,
            ["point"] = AttributeValue.Point, ["normal"] = AttributeValue.Normal, ["point2"] = AttributeValue.Point2,
            ["transform"] = AttributeValue.Transform, ["float4"] = AttributeValue.Float4, ["byte4"] = AttributeValue.Byte4,
        };
        private static readonly Dictionary<string, AttributeDomain> Domains = new(StringComparer.Ordinal)
        {
            ["mesh"] = AttributeDomain.Mesh, ["face"] = AttributeDomain.Face, ["vertex"] = AttributeDomain.Vertex,
            ["corner"] = AttributeDomain.Corner, ["vertex_normal"] = AttributeDomain.VertexNormal,
            ["corner_normal"] = AttributeDomain.CornerNormal, ["corner_byte"] = AttributeDomain.CornerByte,
        };

        private readonly CyclesHost host;
        private readonly Func<string, byte[]> readFile;
        private readonly Dictionary<string, Image> images = new(StringComparer.Ordinal);

        internal Builder(CyclesHost host, Func<string, byte[]> readFile)
        {
            this.host = host;
            this.readFile = readFile;
        }

        internal Dictionary<string, Node> Nodes { get; } = new(StringComparer.Ordinal);

        internal void ReleaseImages()
        {
            // Bindings keep their own reference to the pixels.
            foreach (Image image in images.Values) host.ReleaseImage(image);
            images.Clear();
        }

        internal void Image(JsonElement entry)
        {
            string handle = Required(entry, "handle").GetString();
            if (!Formats.TryGetValue(Required(entry, "format").GetString(), out var format)) throw new FormatException("Unknown image format for " + handle);
            uint width = Required(entry, "width").GetUInt32(), height = Required(entry, "height").GetUInt32();
            byte[] pixels = readFile(Required(entry, "file").GetString());
            if ((ulong)pixels.LongLength != (ulong)width * height * (ulong)format.Bytes) throw new FormatException("Raw pixel file size does not match " + handle);
            if (images.ContainsKey(handle)) throw new FormatException("Duplicate image handle: " + handle);
            images.Add(handle, host.CreateImage(width, height, format.Format, pixels));
        }

        internal void Entry(JsonElement entry)
        {
            string handle = Required(entry, "handle").GetString();
            try
            {
                Build(entry, handle);
            }
            catch (CyclesException error)
            {
                throw new InvalidOperationException($"cbcOracleScene entry '{handle}': {error.Message}", error);
            }
            catch (InvalidOperationException error)
            {
                throw new InvalidOperationException($"cbcOracleScene entry '{handle}': {error.Message}", error.InnerException ?? error);
            }
        }

        private void Build(JsonElement entry, string handle)
        {
            Node target = entry.TryGetProperty("scene", out JsonElement singleton)
                ? host.SceneNode(singleton.GetString())
                : host.CreateNode(Required(entry, "create").GetString());
            if (!Nodes.TryAdd(handle, target)) throw new FormatException("Duplicate node handle: " + handle);
            if (entry.TryGetProperty("graph", out JsonElement graph)) Graph(target, graph);
            if (entry.TryGetProperty("mesh", out JsonElement mesh)) Mesh(target, mesh);
            if (entry.TryGetProperty("set", out JsonElement values)) Apply(target, values, null);
        }

        private Node Find(string handle, Dictionary<string, Node> local)
        {
            if (local != null && local.TryGetValue(handle, out Node node)) return node;
            return Nodes.TryGetValue(handle, out node) ? node : throw new FormatException("Unknown node handle: " + handle);
        }

        private void Graph(Node shader, JsonElement graph)
        {
            Dictionary<string, Node> local = new(StringComparer.Ordinal) { ["output"] = host.ShaderOutput(shader) };
            List<(Node Node, JsonElement Values)> pending = new();
            foreach (JsonElement node in Required(graph, "nodes").EnumerateArray())
            {
                string handle = Required(node, "handle").GetString();
                Node created = host.ShaderNode(shader, Required(node, "type").GetString());
                if (!local.TryAdd(handle, created)) throw new FormatException("Duplicate graph handle: " + handle);
                if (node.TryGetProperty("set", out JsonElement values)) pending.Add((created, values));
            }
            foreach ((Node node, JsonElement values) in pending) Apply(node, values, local);
            if (!graph.TryGetProperty("connect", out JsonElement links)) return;
            foreach (JsonElement link in links.EnumerateArray())
            {
                if (link.GetArrayLength() != 4) throw new FormatException("Each connection is [from, output, to, input].");
                host.Connect(Find(link[0].GetString(), local), link[1].GetString(), Find(link[2].GetString(), local), link[3].GetString());
            }
        }

        private void Mesh(Node target, JsonElement mesh)
        {
            float[] positions = mesh.TryGetProperty("positionsFile", out JsonElement positionsFile)
                ? Floats(readFile(positionsFile.GetString()))
                : Required(mesh, "positions").EnumerateArray().Select(v => v.GetSingle()).ToArray();
            uint[] indices = mesh.TryGetProperty("indicesFile", out JsonElement indicesFile)
                ? UInts(readFile(indicesFile.GetString()))
                : Required(mesh, "indices").EnumerateArray().Select(v => v.GetUInt32()).ToArray();
            if (positions.Length % 3 != 0 || indices.Length % 3 != 0) throw new FormatException("Mesh positions and indices must be complete triples.");
            host.Mesh(target, positions, indices);
            if (mesh.TryGetProperty("attributes", out JsonElement attributes))
                foreach (JsonElement attribute in attributes.EnumerateArray()) Attribute(target, attribute);
            if (mesh.TryGetProperty("motion", out JsonElement motion))
                host.MotionPositions(target, Required(motion, "steps").GetUInt32(), Required(motion, "positions").EnumerateArray().Select(v => v.GetSingle()).ToArray());
        }

        private void Attribute(Node target, JsonElement attribute)
        {
            string name = Required(attribute, "name").GetString();
            if (!Values.TryGetValue(Required(attribute, "value").GetString(), out AttributeValue value) ||
                !Domains.TryGetValue(Required(attribute, "domain").GetString(), out AttributeDomain domain))
                throw new FormatException("Unknown attribute value or domain for " + name);
            AttributeFlags flags = AttributeFlags.None;
            if (attribute.TryGetProperty("flags", out JsonElement flagList))
                foreach (JsonElement flag in flagList.EnumerateArray())
                    flags |= flag.GetString() switch
                    {
                        "standard" => AttributeFlags.Standard,
                        "subdivision" => AttributeFlags.Subdivision,
                        _ => throw new FormatException("Unknown attribute flag for " + name),
                    };
            if (attribute.TryGetProperty("bytes", out JsonElement bytes))
                host.SetAttributeBytes(target, name, domain, flags, bytes.EnumerateArray().Select(v => v.GetByte()).ToArray());
            else if (attribute.TryGetProperty("data", out JsonElement data))
                host.SetAttribute(target, name, value, domain, flags, data.EnumerateArray().Select(v => v.GetSingle()).ToArray());
            else
            {
                byte[] raw = readFile(Required(attribute, "file").GetString());
                if (value == AttributeValue.Byte4) host.SetAttributeBytes(target, name, domain, flags, raw);
                else host.SetAttribute(target, name, value, domain, flags, Floats(raw));
            }
        }

        private void Apply(Node target, JsonElement values, Dictionary<string, Node> local)
        {
            if (values.ValueKind != JsonValueKind.Object) throw new FormatException("'set' must be an object.");
            foreach (JsonProperty property in values.EnumerateObject()) Set(target, property.Name, property.Value, local);
        }

        // Each value is written with the setter that matches the native socket kind (as the C runner does).
        private void Set(Node target, string socket, JsonElement value, Dictionary<string, Node> local)
        {
            try
            {
                SetValue(target, socket, value, local);
            }
            catch (CyclesException error)
            {
                throw new InvalidOperationException($"socket '{socket}': {error.Message}", error);
            }
        }

        private void SetValue(Node target, string socket, JsonElement value, Dictionary<string, Node> local)
        {
            if (socket == "image")
            {
                if (!images.TryGetValue(value.GetString(), out Image image)) throw new FormatException($"Unknown image handle for socket '{socket}'.");
                host.BindImage(target, image);
                return;
            }
            PropertyInfo info = host.Property(target, socket);
            bool array = (info.PropertyFlags & PropertyFlags.Array) != 0;
            List<JsonElement> items = value.ValueKind == JsonValueKind.Array ? value.EnumerateArray().ToList() : [value];
            switch (info.Kind)
            {
                case ValueKind.Boolean or ValueKind.Int or ValueKind.UInt or ValueKind.UInt64:
                {
                    long[] ints = new long[items.Count];
                    for (int i = 0; i < ints.Length; ++i)
                    {
                        JsonElement item = items[i];
                        if (info.Kind == ValueKind.Boolean)
                            ints[i] = item.ValueKind switch
                            {
                                JsonValueKind.True => 1, JsonValueKind.False => 0,
                                _ => throw new FormatException($"Socket '{socket}' expects booleans."),
                            };
                        else ints[i] = item.ValueKind == JsonValueKind.True ? 1 : item.ValueKind == JsonValueKind.False ? 0 : item.GetInt64();
                    }
                    if (!array && ints.Length != 1) throw new FormatException($"Socket '{socket}' expects one value.");
                    host.SetInts(target, socket, ints);
                    return;
                }
                case ValueKind.Enum:
                    if (value.ValueKind == JsonValueKind.String) host.SetString(target, socket, value.GetString());
                    else host.SetInts(target, socket, [value.GetInt64()]);
                    return;
                case ValueKind.Float or ValueKind.Color or ValueKind.Vector or ValueKind.Point or ValueKind.Normal
                    or ValueKind.Point2 or ValueKind.Transform or ValueKind.Float4:
                {
                    List<float> floats = new();
                    Flatten(value, floats);
                    host.SetFloats(target, socket, CollectionsMarshal.AsSpan(floats));
                    return;
                }
                case ValueKind.String:
                    if (array) host.SetStrings(target, socket, items.Select(item => item.GetString()).ToArray());
                    else host.SetString(target, socket, value.GetString());
                    return;
                case ValueKind.Node:
                    host.SetNodes(target, socket, items.Select(item => item.ValueKind == JsonValueKind.Null ? new Node(0) : Find(item.GetString(), local)).ToArray());
                    return;
                default:
                    throw new FormatException($"Socket '{socket}' has a value kind this loader cannot express ({info.ValueType.ToString(CultureInfo.InvariantCulture)}).");
            }
        }

        private static void Flatten(JsonElement value, List<float> output)
        {
            if (value.ValueKind == JsonValueKind.Array) foreach (JsonElement item in value.EnumerateArray()) Flatten(item, output);
            else output.Add(value.GetSingle());
        }

        private static float[] Floats(byte[] raw)
        {
            if (raw.Length % 4 != 0) throw new FormatException("A float32 file's length is not a multiple of 4.");
            float[] values = new float[raw.Length / 4];
            for (int i = 0; i < values.Length; ++i) values[i] = BinaryPrimitives.ReadSingleLittleEndian(raw.AsSpan(i * 4));
            return values;
        }

        private static uint[] UInts(byte[] raw)
        {
            if (raw.Length % 4 != 0) throw new FormatException("A uint32 file's length is not a multiple of 4.");
            uint[] values = new uint[raw.Length / 4];
            for (int i = 0; i < values.Length; ++i) values[i] = BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(i * 4));
            return values;
        }
    }
}
