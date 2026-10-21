// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace CodeBrix.Graphics3D.Cycles.Interop;

/// <summary>
/// The thin string-socket layer over the native C ABI: one method per <c>cbc_host_*</c> function,
/// with raw 64-bit handles, native socket and type names, <see cref="NativeStatus"/> results and
/// spans instead of pointers. Nothing throws for a native failure and nothing is cached; read the
/// failure text with <see cref="LastError"/> on the same thread. Use it to address node types and
/// sockets directly, or as the model for a binding in another language.
/// </summary>
/// <remarks>
/// The caller owns every host handle and must call <see cref="Destroy"/> exactly once. Readers follow
/// the native protocol: pass an empty span to query the size; <see cref="NativeStatus.SmallBuffer"/>
/// means the destination was too small, was left unchanged, and <c>required</c> holds the size needed.
/// </remarks>
public static unsafe class CyclesAbi
{
    /// <summary>The native ABI version this layer was written against (<c>CBC_HOST_ABI</c>).</summary>
    public const uint ExpectedAbiVersion = 1;

    private const uint StructSize = 32;

    /// <summary><c>cbc_host_abi_version</c>.</summary>
    /// <returns>The ABI version the loaded native library implements.</returns>
    public static uint AbiVersion() => Native.AbiVersion();

    /// <summary>
    /// <c>cbc_host_error</c>: the calling thread's most recent native error text.
    /// </summary>
    /// <returns>The error text (empty when none was recorded).</returns>
    public static string LastError()
    {
        Native.Error(null, 0, out ulong required);
        for (;;)
        {
            byte[] text = new byte[checked((int)Math.Max(required, 1))];
            int status;
            fixed (byte* data = text) status = Native.Error(data, (ulong)text.Length, out required);
            if (status == (int)NativeStatus.SmallBuffer) continue;
            return status == 0 && required > 0 ? Encoding.UTF8.GetString(text, 0, checked((int)required - 1)) : string.Empty;
        }
    }

    /// <summary><c>cbc_host_color_config_path</c>: selects the process-wide OpenColorIO configuration.</summary>
    /// <param name="path">The configuration file path.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus ConfigureColor(string path) => (NativeStatus)Native.ConfigureColor(path);

    /// <summary><c>cbc_host_tier_config</c>: configures WebGPU pipeline-tier selection for hosts created afterwards.</summary>
    /// <param name="tier">The requested tier.</param>
    /// <param name="flags">Record and canary options.</param>
    /// <param name="recordDirectory">The app-writable record directory, or <see langword="null"/>.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus ConfigureTiers(PipelineTier tier, PipelineTierFlags flags, string recordDirectory)
    {
        Native.TierConfig config = new() { Size = StructSize, Abi = ExpectedAbiVersion, Tier = (uint)tier, Flags = (uint)flags };
        return (NativeStatus)Native.ConfigureTiers(ref config, recordDirectory);
    }

    /// <summary><c>cbc_host_create</c>: creates a scene host.</summary>
    /// <param name="backend">The backend.</param>
    /// <param name="threads">The native worker thread count.</param>
    /// <param name="host">Receives the host handle (zero on failure).</param>
    /// <returns>The native status.</returns>
    public static NativeStatus Create(Backend backend, uint threads, out ulong host)
    {
        Native.HostConfig config = new() { Size = StructSize, Abi = ExpectedAbiVersion, Backend = (uint)backend, Threads = threads };
        return (NativeStatus)Native.Create(ref config, out host);
    }

    /// <summary><c>cbc_host_destroy</c>: removes the handle, cancels active work and waits for cleanup.</summary>
    /// <param name="host">The host handle.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus Destroy(ulong host) => (NativeStatus)Native.Destroy(host);

    /// <summary><c>cbc_host_cancel</c>: cancels the active render.</summary>
    /// <param name="host">The host handle.</param>
    /// <returns>The native status (<see cref="NativeStatus.NotReady"/> when nothing was rendering).</returns>
    public static NativeStatus Cancel(ulong host) => (NativeStatus)Native.Cancel(host);

    /// <summary><c>cbc_host_is_rendering</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="rendering">Receives whether a render is running.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus IsRendering(ulong host, out bool rendering)
    {
        NativeStatus status = (NativeStatus)Native.IsRendering(host, out uint active);
        rendering = active != 0;
        return status;
    }

    /// <summary><c>cbc_host_scene_node</c>: gets a scene singleton.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="name"><c>camera</c>, <c>background</c>, <c>film</c> or <c>integrator</c>.</param>
    /// <param name="node">Receives the node handle.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus SceneNode(ulong host, string name, out ulong node) => (NativeStatus)Native.SceneNode(host, name, out node);

    /// <summary><c>cbc_host_create_node</c>: creates a scene node.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="type">The native type name.</param>
    /// <param name="node">Receives the node handle.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus CreateNode(ulong host, string type, out ulong node) => (NativeStatus)Native.CreateNode(host, type, out node);

    /// <summary><c>cbc_host_shader_output</c>: gets a shader graph's output node.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="shader">The shader node handle.</param>
    /// <param name="node">Receives the output node handle.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus ShaderOutput(ulong host, ulong shader, out ulong node) => (NativeStatus)Native.ShaderOutput(host, shader, out node);

    /// <summary><c>cbc_host_shader_node</c>: adds a shader node to a shader graph.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="shader">The shader node handle.</param>
    /// <param name="type">The native shader node type name.</param>
    /// <param name="node">Receives the node handle.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus ShaderNode(ulong host, ulong shader, string type, out ulong node) => (NativeStatus)Native.ShaderNode(host, shader, type, out node);

    /// <summary><c>cbc_host_connect</c>: links two shader sockets by their link (UI) names.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="from">The source node handle.</param>
    /// <param name="output">The source output socket name.</param>
    /// <param name="to">The destination node handle.</param>
    /// <param name="input">The destination input socket name.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus Connect(ulong host, ulong from, string output, ulong to, string input) => (NativeStatus)Native.Connect(host, from, output, to, input);

    /// <summary><c>cbc_host_set_floats</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The flattened values.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus SetFloats(ulong host, ulong node, string socket, ReadOnlySpan<float> values)
    {
        fixed (float* data = values) return (NativeStatus)Native.SetFloats(host, node, socket, data, (ulong)values.Length);
    }

    /// <summary><c>cbc_host_set_ints</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The values.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus SetInts(ulong host, ulong node, string socket, ReadOnlySpan<long> values)
    {
        fixed (long* data = values) return (NativeStatus)Native.SetInts(host, node, socket, data, (ulong)values.Length);
    }

    /// <summary><c>cbc_host_set_uints</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The values (exactly one for the current native host).</param>
    /// <returns>The native status.</returns>
    public static NativeStatus SetUInts(ulong host, ulong node, string socket, ReadOnlySpan<ulong> values)
    {
        fixed (ulong* data = values) return (NativeStatus)Native.SetUInts(host, node, socket, data, (ulong)values.Length);
    }

    /// <summary><c>cbc_host_set_string</c>: sets a string socket, or an enum socket by item name.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus SetString(ulong host, ulong node, string socket, string value) => (NativeStatus)Native.SetString(host, node, socket, value);

    /// <summary><c>cbc_host_set_strings</c>: sets a string-array socket.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The strings; a <see langword="null"/> element is passed as a null pointer, which the native host rejects.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus SetStrings(ulong host, ulong node, string socket, ReadOnlySpan<string> values)
    {
        byte[][] utf8 = new byte[values.Length][];
        for (int i = 0; i < values.Length; ++i)
            if (values[i] != null) utf8[i] = Encoding.UTF8.GetBytes(values[i] + "\0");
        GCHandle[] pins = new GCHandle[utf8.Length];
        byte*[] pointers = new byte*[utf8.Length];
        try
        {
            for (int i = 0; i < utf8.Length; ++i)
            {
                if (utf8[i] == null) continue;
                pins[i] = GCHandle.Alloc(utf8[i], GCHandleType.Pinned);
                pointers[i] = (byte*)pins[i].AddrOfPinnedObject();
            }
            fixed (byte** list = pointers) return (NativeStatus)Native.SetStrings(host, node, socket, list, (ulong)pointers.Length);
        }
        finally
        {
            foreach (GCHandle pin in pins) if (pin.IsAllocated) pin.Free();
        }
    }

    /// <summary><c>cbc_host_set_nodes</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The referenced node handles.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus SetNodes(ulong host, ulong node, string socket, ReadOnlySpan<ulong> values)
    {
        fixed (ulong* data = values) return (NativeStatus)Native.SetNodes(host, node, socket, data, (ulong)values.Length);
    }

    /// <summary><c>cbc_host_property_info</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="info">Receives the socket description.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus PropertyInfo(ulong host, ulong node, string socket, out PropertyInfo info)
    {
        Native.PropertyInfo value = new() { Size = StructSize };
        NativeStatus status = (NativeStatus)Native.GetProperty(host, node, socket, ref value);
        info = new PropertyInfo(value.ValueType, value.Flags, value.Components, value.Count);
        return status;
    }

    /// <summary><c>cbc_host_get_floats</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The destination (empty for a size query).</param>
    /// <param name="required">Receives the float count.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus GetFloats(ulong host, ulong node, string socket, Span<float> values, out ulong required)
    {
        fixed (float* data = values) return (NativeStatus)Native.GetFloats(host, node, socket, data, (ulong)values.Length, out required);
    }

    /// <summary><c>cbc_host_get_ints</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The destination (empty for a size query).</param>
    /// <param name="required">Receives the value count.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus GetInts(ulong host, ulong node, string socket, Span<long> values, out ulong required)
    {
        fixed (long* data = values) return (NativeStatus)Native.GetInts(host, node, socket, data, (ulong)values.Length, out required);
    }

    /// <summary><c>cbc_host_get_uints</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The destination (empty for a size query).</param>
    /// <param name="required">Receives the value count.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus GetUInts(ulong host, ulong node, string socket, Span<ulong> values, out ulong required)
    {
        fixed (ulong* data = values) return (NativeStatus)Native.GetUInts(host, node, socket, data, (ulong)values.Length, out required);
    }

    /// <summary><c>cbc_host_get_string</c>: one element as UTF-8 bytes including the trailing NUL.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="index">The element index (0 for scalar sockets).</param>
    /// <param name="utf8">The destination (empty for a size query).</param>
    /// <param name="required">Receives the byte count including the trailing NUL.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus GetString(ulong host, ulong node, string socket, ulong index, Span<byte> utf8, out ulong required)
    {
        fixed (byte* data = utf8) return (NativeStatus)Native.GetString(host, node, socket, index, data, (ulong)utf8.Length, out required);
    }

    /// <summary><c>cbc_host_get_nodes</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The destination (empty for a size query).</param>
    /// <param name="required">Receives the handle count.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus GetNodes(ulong host, ulong node, string socket, Span<ulong> values, out ulong required)
    {
        fixed (ulong* data = values) return (NativeStatus)Native.GetNodes(host, node, socket, data, (ulong)values.Length, out required);
    }

    /// <summary><c>cbc_host_node_type</c>: the node's native type name as UTF-8 bytes including the trailing NUL.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="utf8">The destination (empty for a size query).</param>
    /// <param name="required">Receives the byte count including the trailing NUL.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus NodeType(ulong host, ulong node, Span<byte> utf8, out ulong required)
    {
        fixed (byte* data = utf8) return (NativeStatus)Native.NodeType(host, node, data, (ulong)utf8.Length, out required);
    }

    /// <summary><c>cbc_host_registry_json</c>: the registry as UTF-8 bytes including the trailing NUL.</summary>
    /// <param name="utf8">The destination (empty for a size query).</param>
    /// <param name="required">Receives the byte count including the trailing NUL.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus RegistryJson(Span<byte> utf8, out ulong required)
    {
        fixed (byte* data = utf8) return (NativeStatus)Native.Registry(data, (ulong)utf8.Length, out required);
    }

    /// <summary><c>cbc_host_device_report_json</c>: the device report as UTF-8 bytes including the trailing NUL.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="utf8">The destination (empty for a size query).</param>
    /// <param name="required">Receives the byte count including the trailing NUL.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus DeviceReportJson(ulong host, Span<byte> utf8, out ulong required)
    {
        fixed (byte* data = utf8) return (NativeStatus)Native.DeviceReport(host, data, (ulong)utf8.Length, out required);
    }

    /// <summary><c>cbc_host_attribute_floats</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="geometry">The geometry node handle.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="value">The element type.</param>
    /// <param name="domain">The element domain.</param>
    /// <param name="flags">Standard and subdivision options.</param>
    /// <param name="values">The flattened values.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus SetAttributeFloats(ulong host, ulong geometry, string name, AttributeValue value, AttributeDomain domain, AttributeFlags flags, ReadOnlySpan<float> values)
    {
        Native.AttributeConfig config = new() { Size = StructSize, Value = (uint)value, Domain = (uint)domain, Flags = (uint)flags };
        fixed (float* data = values) return (NativeStatus)Native.SetAttributeFloats(host, geometry, name, ref config, data, (ulong)values.Length);
    }

    /// <summary><c>cbc_host_attribute_bytes</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="geometry">The geometry node handle.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="domain">The element domain.</param>
    /// <param name="flags">Standard and subdivision options.</param>
    /// <param name="values">The bytes, four per element.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus SetAttributeBytes(ulong host, ulong geometry, string name, AttributeDomain domain, AttributeFlags flags, ReadOnlySpan<byte> values)
    {
        Native.AttributeConfig config = new() { Size = StructSize, Value = (uint)AttributeValue.Byte4, Domain = (uint)domain, Flags = (uint)flags };
        fixed (byte* data = values) return (NativeStatus)Native.SetAttributeBytes(host, geometry, name, ref config, data, (ulong)values.Length);
    }

    /// <summary><c>cbc_host_attribute_get_floats</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="geometry">The geometry node handle.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="flags">The flags the attribute was set with.</param>
    /// <param name="values">The destination (empty for a size query).</param>
    /// <param name="required">Receives the float count.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus GetAttributeFloats(ulong host, ulong geometry, string name, AttributeFlags flags, Span<float> values, out ulong required)
    {
        fixed (float* data = values) return (NativeStatus)Native.GetAttributeFloats(host, geometry, name, (uint)flags, data, (ulong)values.Length, out required);
    }

    /// <summary><c>cbc_host_attribute_get_bytes</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="geometry">The geometry node handle.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="flags">The flags the attribute was set with.</param>
    /// <param name="values">The destination (empty for a size query).</param>
    /// <param name="required">Receives the byte count.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus GetAttributeBytes(ulong host, ulong geometry, string name, AttributeFlags flags, Span<byte> values, out ulong required)
    {
        fixed (byte* data = values) return (NativeStatus)Native.GetAttributeBytes(host, geometry, name, (uint)flags, data, (ulong)values.Length, out required);
    }

    /// <summary><c>cbc_host_attribute_remove</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="geometry">The geometry node handle.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="flags">The flags the attribute was set with.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus RemoveAttribute(ulong host, ulong geometry, string name, AttributeFlags flags) => (NativeStatus)Native.RemoveAttribute(host, geometry, name, (uint)flags);

    /// <summary><c>cbc_host_image_create</c>: copies raw pixels into a new immutable image.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="width">The width in pixels.</param>
    /// <param name="height">The height in pixels.</param>
    /// <param name="format">The pixel layout.</param>
    /// <param name="pixels">The tightly packed pixel bytes, row zero at v = 0.</param>
    /// <param name="image">Receives the image handle.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus CreateImage(ulong host, uint width, uint height, ImageFormat format, ReadOnlySpan<byte> pixels, out ulong image)
    {
        Native.ImageConfig config = new() { Size = StructSize, Width = width, Height = height, Format = (uint)format };
        fixed (byte* data = pixels) return (NativeStatus)Native.CreateImage(host, ref config, data, (ulong)pixels.Length, out image);
    }

    /// <summary><c>cbc_host_image_bind</c> (image zero detaches).</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="texture">The texture node handle.</param>
    /// <param name="image">The image handle, or zero.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus BindImage(ulong host, ulong texture, ulong image) => (NativeStatus)Native.BindImage(host, texture, image);

    /// <summary><c>cbc_host_image_release</c>.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="image">The image handle.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus ReleaseImage(ulong host, ulong image) => (NativeStatus)Native.ReleaseImage(host, image);

    /// <summary><c>cbc_host_mesh</c>: replaces a mesh's triangles.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="mesh">The mesh node handle.</param>
    /// <param name="positions">Vertex positions as x, y, z triples (the vertex count is the length divided by three).</param>
    /// <param name="indices">Triangle vertex indices (the triangle count is the length divided by three).</param>
    /// <returns>The native status.</returns>
    public static NativeStatus Mesh(ulong host, ulong mesh, ReadOnlySpan<float> positions, ReadOnlySpan<uint> indices)
    {
        fixed (float* xyz = positions)
        fixed (uint* triangles = indices)
            return (NativeStatus)Native.Mesh(host, mesh, xyz, (ulong)(positions.Length / 3), triangles, (ulong)(indices.Length / 3));
    }

    /// <summary><c>cbc_host_render</c>: renders synchronously into scene-linear RGBA, lower-left origin.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="width">The width in pixels.</param>
    /// <param name="height">The height in pixels.</param>
    /// <param name="samples">The samples per pixel.</param>
    /// <param name="rgba">The destination, at least width * height * 4 floats; changed only on success.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus Render(ulong host, uint width, uint height, uint samples, Span<float> rgba)
    {
        Native.RenderConfig config = new() { Size = StructSize, Width = width, Height = height, Samples = samples };
        fixed (float* data = rgba) return (NativeStatus)Native.Render(host, ref config, data, (ulong)rgba.Length);
    }

    /// <summary><c>cbc_host_version</c>: the native library version, ABI and pinned Cycles revision. Callable at any time, including from an event callback.</summary>
    /// <param name="abi">Receives the ABI version.</param>
    /// <param name="libraryVersion">Receives the library version (<c>major.minor.yyyyMMdd.build</c>), or an empty string on failure.</param>
    /// <param name="cyclesRevision">Receives the pinned upstream Cycles revision, or an empty string on failure.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus Version(out uint abi, out string libraryVersion, out string cyclesRevision)
    {
        Native.VersionInfo info = new() { Size = (uint)sizeof(Native.VersionInfo) };
        NativeStatus status = (NativeStatus)Native.Version(ref info);
        abi = status == NativeStatus.Ok ? info.Abi : 0;
        libraryVersion = status == NativeStatus.Ok ? FixedAscii(info.LibraryVersion, 32) : string.Empty;
        cyclesRevision = status == NativeStatus.Ok ? FixedAscii(info.CyclesRevision, 48) : string.Empty;
        return status;
    }

    private static string FixedAscii(byte* text, int capacity)
    {
        int length = 0;
        while (length < capacity && text[length] != 0) ++length;
        return Encoding.ASCII.GetString(text, length);
    }

    /// <summary><c>cbc_host_resolve_backend</c>: the concrete backend this binary creates for a request (including <see cref="Cycles.Backend.Gpu"/>), without probing hardware.</summary>
    /// <param name="requested">The requested backend.</param>
    /// <param name="resolved">Receives the concrete backend.</param>
    /// <returns>The native status (<see cref="NativeStatus.Unsupported"/> when the backend is not compiled in).</returns>
    public static NativeStatus ResolveBackend(Backend requested, out Backend resolved)
    {
        NativeStatus status = (NativeStatus)Native.ResolveBackend((uint)requested, out uint value);
        resolved = (Backend)value;
        return status;
    }

    /// <summary><c>cbc_host_backend</c>: the concrete backend of a host (never <see cref="Cycles.Backend.Gpu"/>).</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="backend">Receives the concrete backend.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus GetBackend(ulong host, out Backend backend)
    {
        NativeStatus status = (NativeStatus)Native.GetBackend(host, out uint value);
        backend = (Backend)value;
        return status;
    }

    /// <summary><c>cbc_host_capabilities_json</c>: the capability report as UTF-8 JSON, including the trailing NUL.</summary>
    /// <param name="host">The host handle, or zero for the binary-level report.</param>
    /// <param name="utf8">The destination; empty for a size query.</param>
    /// <param name="required">Receives the bytes needed.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus CapabilitiesJson(ulong host, Span<byte> utf8, out ulong required)
    {
        fixed (byte* data = utf8) return (NativeStatus)Native.Capabilities(host, data, (ulong)utf8.Length, out required);
    }

    /// <summary><c>cbc_host_delete_node</c>: deletes a node and invalidates its handle.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <returns>The native status (<see cref="NativeStatus.Invalid"/> when another node still references it, or it cannot be deleted).</returns>
    public static NativeStatus DeleteNode(ulong host, ulong node) => (NativeStatus)Native.DeleteNode(host, node);

    /// <summary><c>cbc_host_image_update</c>: replaces the pixels of a live image handle.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="image">The image handle.</param>
    /// <param name="width">The width in pixels.</param>
    /// <param name="height">The height in pixels.</param>
    /// <param name="format">The pixel layout.</param>
    /// <param name="pixels">The tightly packed pixel bytes.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus UpdateImage(ulong host, ulong image, uint width, uint height, ImageFormat format, ReadOnlySpan<byte> pixels)
    {
        Native.ImageConfig config = new() { Size = StructSize, Width = width, Height = height, Format = (uint)format };
        fixed (byte* data = pixels) return (NativeStatus)Native.UpdateImage(host, image, ref config, data, (ulong)pixels.Length);
    }

    /// <summary><c>cbc_host_motion_positions</c>: sets or (steps 1, no values) removes a mesh's deformation motion.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="mesh">The mesh node handle.</param>
    /// <param name="steps">The odd number of time steps (3 to 129), or 1 to remove.</param>
    /// <param name="positions">The time-ordered flattened positions.</param>
    /// <returns>The native status (<see cref="NativeStatus.NotReady"/> when the host refuses the edit, for example on a baked volume).</returns>
    public static NativeStatus MotionPositions(ulong host, ulong mesh, uint steps, ReadOnlySpan<float> positions)
    {
        fixed (float* xyz = positions) return (NativeStatus)Native.MotionPositions(host, mesh, steps, xyz, (ulong)positions.Length);
    }

    /// <summary><c>cbc_host_set_event_callback</c>: registers (or with a null callback clears) the host's render event callback.</summary>
    /// <remarks>
    /// The callback must stay callable and <paramref name="userData"/> valid until a later call or
    /// <see cref="Destroy"/> returns. It must not call the library except <see cref="LastError"/> and
    /// <see cref="Version"/>; returning nonzero requests cancellation.
    /// </remarks>
    /// <param name="host">The host handle.</param>
    /// <param name="callback">The unmanaged callback, or null.</param>
    /// <param name="userData">The value passed back to the callback.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus SetEventCallback(ulong host, delegate* unmanaged[Cdecl]<nint, Native.Event*, int> callback, nint userData)
        => (NativeStatus)Native.SetEventCallback(host, callback, userData);

    /// <summary><c>cbc_host_pass_floats</c>: copies one pass of the most recent successful render.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="name">The pass name.</param>
    /// <param name="values">The destination; empty for a size query.</param>
    /// <param name="required">Receives the float count needed.</param>
    /// <param name="components">Receives the floats per pixel.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus PassFloats(ulong host, string name, Span<float> values, out ulong required, out uint components)
    {
        fixed (float* data = values) return (NativeStatus)Native.PassFloats(host, name, data, (ulong)values.Length, out required, out components);
    }

    /// <summary><c>cbc_host_render_passes_json</c>: the passes captured by the most recent successful render, as UTF-8 JSON.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="utf8">The destination; empty for a size query.</param>
    /// <param name="required">Receives the bytes needed, including the trailing NUL.</param>
    /// <returns>The native status.</returns>
    public static NativeStatus RenderPassesJson(ulong host, Span<byte> utf8, out ulong required)
    {
        fixed (byte* data = utf8) return (NativeStatus)Native.RenderPasses(host, data, (ulong)utf8.Length, out required);
    }
}
