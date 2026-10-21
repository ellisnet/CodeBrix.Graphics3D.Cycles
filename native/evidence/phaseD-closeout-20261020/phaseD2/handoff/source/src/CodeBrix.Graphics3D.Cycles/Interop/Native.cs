// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Runtime.InteropServices;

namespace CodeBrix.Graphics3D.Cycles.Interop;

/// <summary>
/// The raw P/Invoke declarations of the native scene host C ABI (<c>native/host/cycles_host.h</c>),
/// one method per exported <c>cbc_host_*</c> function and one structure per ABI structure. This is
/// the lowest, stable managed tier: every call returns the native <see cref="NativeStatus"/> code as
/// an integer, handles are plain 64-bit integers, and nothing is validated, copied or converted
/// beyond UTF-8 string marshalling. Prefer <see cref="CyclesAbi"/> (spans and status enums) or the
/// typed <see cref="CyclesScene"/> unless you are writing your own binding.
/// </summary>
/// <remarks>
/// Structures carry their own <c>struct_size</c> (32 for every structure here) and, where present,
/// the <c>abi_version</c> the caller targets (<see cref="CyclesAbi.ExpectedAbiVersion"/>); reserved
/// fields must be zero. Getter functions follow one protocol: they set <c>required</c> even when the
/// destination is too small (status 6), leave the destination unchanged on any failure, and accept a
/// null pointer with capacity zero as a size query. The error text of the most recent failure on the
/// calling thread is read with <see cref="Error"/>.
/// </remarks>
public static unsafe class Native
{
    /// <summary>The native library name the declarations bind to (<c>libcodebrix_cycles_host.so</c> on Linux).</summary>
    public const string Library = "codebrix_cycles_host";

    /// <summary>The ABI structure <c>CBCHostConfig</c>, passed to <see cref="Create"/>.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct HostConfig
    {
        /// <summary>The structure size in bytes (32).</summary>
        public uint Size;
        /// <summary>The ABI version the caller targets (<c>CBC_HOST_ABI</c>).</summary>
        public uint Abi;
        /// <summary>The backend, a <see cref="Cycles.Backend"/> value.</summary>
        public uint Backend;
        /// <summary>The number of native worker threads (0 to 1024).</summary>
        public uint Threads;
        /// <summary>Reserved; must be zero.</summary>
        public uint R0;
        /// <summary>Reserved; must be zero.</summary>
        public uint R1;
        /// <summary>Reserved; must be zero.</summary>
        public uint R2;
        /// <summary>Reserved; must be zero.</summary>
        public uint R3;
    }

    /// <summary>The ABI structure <c>CBCRenderConfig</c>, passed to <see cref="Render"/>.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct RenderConfig
    {
        /// <summary>The structure size in bytes (32).</summary>
        public uint Size;
        /// <summary>The image width in pixels.</summary>
        public uint Width;
        /// <summary>The image height in pixels.</summary>
        public uint Height;
        /// <summary>The number of samples per pixel.</summary>
        public uint Samples;
        /// <summary>Reserved; must be zero.</summary>
        public uint R0;
        /// <summary>Reserved; must be zero.</summary>
        public uint R1;
        /// <summary>Reserved; must be zero.</summary>
        public uint R2;
        /// <summary>Reserved; must be zero.</summary>
        public uint R3;
    }

    /// <summary>The ABI structure <c>CBCPropertyInfo</c>, filled by <see cref="GetProperty"/>.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct PropertyInfo
    {
        /// <summary>The structure size in bytes (32); set it before the call.</summary>
        public uint Size;
        /// <summary>The value kind, a <see cref="ValueKind"/> value.</summary>
        public uint ValueType;
        /// <summary>The property flags, a <see cref="Cycles.PropertyFlags"/> combination.</summary>
        public uint Flags;
        /// <summary>The flattened component count per element.</summary>
        public uint Components;
        /// <summary>The current element count (zero when the socket is not readable).</summary>
        public ulong Count;
        /// <summary>Reserved.</summary>
        public ulong Reserved;
    }

    /// <summary>The ABI structure <c>CBCAttributeConfig</c>, passed to the attribute setters.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct AttributeConfig
    {
        /// <summary>The structure size in bytes (32).</summary>
        public uint Size;
        /// <summary>The element value type, an <see cref="AttributeValue"/> value.</summary>
        public uint Value;
        /// <summary>The element domain, an <see cref="AttributeDomain"/> value.</summary>
        public uint Domain;
        /// <summary>The attribute flags, an <see cref="AttributeFlags"/> combination.</summary>
        public uint Flags;
        /// <summary>Reserved; must be zero.</summary>
        public uint R0;
        /// <summary>Reserved; must be zero.</summary>
        public uint R1;
        /// <summary>Reserved; must be zero.</summary>
        public uint R2;
        /// <summary>Reserved; must be zero.</summary>
        public uint R3;
    }

    /// <summary>The ABI structure <c>CBCImageConfig</c>, passed to <see cref="CreateImage"/>.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ImageConfig
    {
        /// <summary>The structure size in bytes (32).</summary>
        public uint Size;
        /// <summary>The width in pixels.</summary>
        public uint Width;
        /// <summary>The height in pixels.</summary>
        public uint Height;
        /// <summary>The pixel layout, an <see cref="ImageFormat"/> value.</summary>
        public uint Format;
        /// <summary>Reserved; must be zero.</summary>
        public uint R0;
        /// <summary>Reserved; must be zero.</summary>
        public uint R1;
        /// <summary>Reserved; must be zero.</summary>
        public uint R2;
        /// <summary>Reserved; must be zero.</summary>
        public uint R3;
    }

    /// <summary>The ABI structure <c>CBCTierConfig</c>, passed to <see cref="ConfigureTiers"/>.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct TierConfig
    {
        /// <summary>The structure size in bytes (32).</summary>
        public uint Size;
        /// <summary>The ABI version the caller targets (<c>CBC_HOST_ABI</c>).</summary>
        public uint Abi;
        /// <summary>The requested tier, a <see cref="PipelineTier"/> value.</summary>
        public uint Tier;
        /// <summary>The record and canary options, a <see cref="PipelineTierFlags"/> combination.</summary>
        public uint Flags;
        /// <summary>Reserved; must be zero.</summary>
        public uint R0;
        /// <summary>Reserved; must be zero.</summary>
        public uint R1;
        /// <summary>Reserved; must be zero.</summary>
        public uint R2;
        /// <summary>Reserved; must be zero.</summary>
        public uint R3;
    }

    /// <summary><c>cbc_host_abi_version</c>: the ABI version the loaded native library implements.</summary>
    /// <returns>The native <c>CBC_HOST_ABI</c> value.</returns>
    [DllImport(Library, EntryPoint="cbc_host_abi_version", CallingConvention=CallingConvention.Cdecl)] public static extern uint AbiVersion();

    /// <summary><c>cbc_host_color_config_path</c>: selects the process-wide OpenColorIO configuration.</summary>
    /// <param name="path">The UTF-8 configuration file path.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_color_config_path", CallingConvention=CallingConvention.Cdecl)] public static extern int ConfigureColor([MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    /// <summary><c>cbc_host_tier_config</c>: configures WebGPU pipeline-tier selection for hosts created afterwards.</summary>
    /// <param name="config">The tier configuration.</param>
    /// <param name="directory">The app-writable record directory, or null to disable the record.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_tier_config", CallingConvention=CallingConvention.Cdecl)] public static extern int ConfigureTiers(ref TierConfig config, [MarshalAs(UnmanagedType.LPUTF8Str)] string directory);

    /// <summary><c>cbc_host_device_report_json</c>: copies the host's device capability report as UTF-8 JSON.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="json">The destination, or null with zero capacity for a size query.</param>
    /// <param name="capacity">The destination capacity in bytes.</param>
    /// <param name="required">Receives the byte count including the trailing NUL.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_device_report_json", CallingConvention=CallingConvention.Cdecl)] public static extern int DeviceReport(ulong host, byte* json, ulong capacity, out ulong required);

    /// <summary><c>cbc_host_attribute_floats</c>: creates or replaces a float-based geometry attribute.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="geometry">The geometry node handle.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="config">The attribute configuration.</param>
    /// <param name="values">The flattened values.</param>
    /// <param name="count">The number of floats.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_attribute_floats", CallingConvention=CallingConvention.Cdecl)] public static extern int SetAttributeFloats(ulong host, ulong geometry, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, ref AttributeConfig config, float* values, ulong count);

    /// <summary><c>cbc_host_attribute_bytes</c>: creates or replaces a four-byte-per-element geometry attribute.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="geometry">The geometry node handle.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="config">The attribute configuration.</param>
    /// <param name="values">The bytes.</param>
    /// <param name="count">The number of bytes.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_attribute_bytes", CallingConvention=CallingConvention.Cdecl)] public static extern int SetAttributeBytes(ulong host, ulong geometry, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, ref AttributeConfig config, byte* values, ulong count);

    /// <summary><c>cbc_host_attribute_get_floats</c>: reads a float-based geometry attribute.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="geometry">The geometry node handle.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="flags">The attribute flags it was set with.</param>
    /// <param name="values">The destination, or null with zero capacity for a size query.</param>
    /// <param name="capacity">The destination capacity in floats.</param>
    /// <param name="required">Receives the float count.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_attribute_get_floats", CallingConvention=CallingConvention.Cdecl)] public static extern int GetAttributeFloats(ulong host, ulong geometry, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, uint flags, float* values, ulong capacity, out ulong required);

    /// <summary><c>cbc_host_attribute_get_bytes</c>: reads a four-byte-per-element geometry attribute.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="geometry">The geometry node handle.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="flags">The attribute flags it was set with.</param>
    /// <param name="values">The destination, or null with zero capacity for a size query.</param>
    /// <param name="capacity">The destination capacity in bytes.</param>
    /// <param name="required">Receives the byte count.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_attribute_get_bytes", CallingConvention=CallingConvention.Cdecl)] public static extern int GetAttributeBytes(ulong host, ulong geometry, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, uint flags, byte* values, ulong capacity, out ulong required);

    /// <summary><c>cbc_host_attribute_remove</c>: removes a geometry attribute.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="geometry">The geometry node handle.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="flags">The attribute flags it was set with.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_attribute_remove", CallingConvention=CallingConvention.Cdecl)] public static extern int RemoveAttribute(ulong host, ulong geometry, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, uint flags);

    /// <summary><c>cbc_host_image_create</c>: copies tightly packed raw pixels into a new immutable image.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="config">The image configuration.</param>
    /// <param name="pixels">The pixel bytes.</param>
    /// <param name="count">The pixel byte count.</param>
    /// <param name="image">Receives the image handle.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_image_create", CallingConvention=CallingConvention.Cdecl)] public static extern int CreateImage(ulong host, ref ImageConfig config, byte* pixels, ulong count, out ulong image);

    /// <summary><c>cbc_host_image_bind</c>: binds an image to an image or environment texture node (zero detaches).</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="texture">The texture node handle.</param>
    /// <param name="image">The image handle, or zero.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_image_bind", CallingConvention=CallingConvention.Cdecl)] public static extern int BindImage(ulong host, ulong texture, ulong image);

    /// <summary><c>cbc_host_image_release</c>: releases the caller's image handle; bindings keep the pixels.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="image">The image handle.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_image_release", CallingConvention=CallingConvention.Cdecl)] public static extern int ReleaseImage(ulong host, ulong image);

    /// <summary><c>cbc_host_property_info</c>: describes a socket.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="info">The description; set <see cref="PropertyInfo.Size"/> to 32 first.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_property_info", CallingConvention=CallingConvention.Cdecl)] public static extern int GetProperty(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ref PropertyInfo info);

    /// <summary><c>cbc_host_get_floats</c>: reads a float-based socket as flattened components.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The destination, or null with zero capacity for a size query.</param>
    /// <param name="capacity">The destination capacity in floats.</param>
    /// <param name="required">Receives the float count.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_get_floats", CallingConvention=CallingConvention.Cdecl)] public static extern int GetFloats(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, float* values, ulong capacity, out ulong required);

    /// <summary><c>cbc_host_get_ints</c>: reads an integer, boolean, enum or unsigned socket as 64-bit integers.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The destination, or null with zero capacity for a size query.</param>
    /// <param name="capacity">The destination capacity in values.</param>
    /// <param name="required">Receives the value count.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_get_ints", CallingConvention=CallingConvention.Cdecl)] public static extern int GetInts(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, long* values, ulong capacity, out ulong required);

    /// <summary><c>cbc_host_get_uints</c>: reads a uint or uint64 scalar socket.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The destination, or null with zero capacity for a size query.</param>
    /// <param name="capacity">The destination capacity in values.</param>
    /// <param name="required">Receives the value count.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_get_uints", CallingConvention=CallingConvention.Cdecl)] public static extern int GetUInts(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ulong* values, ulong capacity, out ulong required);

    /// <summary><c>cbc_host_set_uints</c>: sets a uint or uint64 scalar socket (exactly one value).</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The value.</param>
    /// <param name="count">The value count (1).</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_set_uints", CallingConvention=CallingConvention.Cdecl)] public static extern int SetUInts(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ulong* values, ulong count);

    /// <summary><c>cbc_host_set_strings</c>: sets a string-array socket from NUL-terminated UTF-8 strings.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">Pointers to NUL-terminated UTF-8 strings (none null).</param>
    /// <param name="count">The string count.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_set_strings", CallingConvention=CallingConvention.Cdecl)] public static extern int SetStrings(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, byte** values, ulong count);

    /// <summary><c>cbc_host_get_string</c>: reads one element of a string, enum or string-array socket as UTF-8.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="index">The element index (0 for scalar sockets).</param>
    /// <param name="values">The destination, or null with zero capacity for a size query.</param>
    /// <param name="capacity">The destination capacity in bytes.</param>
    /// <param name="required">Receives the byte count including the trailing NUL.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_get_string", CallingConvention=CallingConvention.Cdecl)] public static extern int GetString(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ulong index, byte* values, ulong capacity, out ulong required);

    /// <summary><c>cbc_host_get_nodes</c>: reads a node or node-array socket as node handles (zero for an empty reference).</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The destination, or null with zero capacity for a size query.</param>
    /// <param name="capacity">The destination capacity in handles.</param>
    /// <param name="required">Receives the handle count.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_get_nodes", CallingConvention=CallingConvention.Cdecl)] public static extern int GetNodes(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ulong* values, ulong capacity, out ulong required);

    /// <summary><c>cbc_host_node_type</c>: copies a node's native registry type name as UTF-8.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="name">The destination, or null with zero capacity for a size query.</param>
    /// <param name="capacity">The destination capacity in bytes.</param>
    /// <param name="required">Receives the byte count including the trailing NUL.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_node_type", CallingConvention=CallingConvention.Cdecl)] public static extern int NodeType(ulong host, ulong node, byte* name, ulong capacity, out ulong required);

    /// <summary><c>cbc_host_registry_json</c>: copies the native node and property registry as UTF-8 JSON.</summary>
    /// <param name="values">The destination, or null with zero capacity for a size query.</param>
    /// <param name="capacity">The destination capacity in bytes.</param>
    /// <param name="required">Receives the byte count including the trailing NUL.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_registry_json", CallingConvention=CallingConvention.Cdecl)] public static extern int Registry(byte* values, ulong capacity, out ulong required);

    /// <summary><c>cbc_host_error</c>: copies the calling thread's most recent native error text as UTF-8.</summary>
    /// <param name="data">The destination, or null with zero capacity for a size query.</param>
    /// <param name="size">The destination capacity in bytes.</param>
    /// <param name="required">Receives the byte count including the trailing NUL.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_error", CallingConvention=CallingConvention.Cdecl)] public static extern int Error(byte* data, ulong size, out ulong required);

    /// <summary><c>cbc_host_create</c>: creates a scene host.</summary>
    /// <param name="config">The host configuration.</param>
    /// <param name="host">Receives the host handle.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_create", CallingConvention=CallingConvention.Cdecl)] public static extern int Create(ref HostConfig config, out ulong host);

    /// <summary><c>cbc_host_destroy</c>: removes the handle, cancels active work and waits for cleanup.</summary>
    /// <param name="host">The host handle.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_destroy", CallingConvention=CallingConvention.Cdecl)] public static extern int Destroy(ulong host);

    /// <summary><c>cbc_host_cancel</c>: cancels the active render (status 5 when nothing is rendering).</summary>
    /// <param name="host">The host handle.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_cancel", CallingConvention=CallingConvention.Cdecl)] public static extern int Cancel(ulong host);

    /// <summary><c>cbc_host_is_rendering</c>: reports whether a render is running.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="active">Receives 1 while a render runs, otherwise 0.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_is_rendering", CallingConvention=CallingConvention.Cdecl)] public static extern int IsRendering(ulong host, out uint active);

    /// <summary><c>cbc_host_scene_node</c>: gets a scene singleton (<c>camera</c>, <c>background</c>, <c>film</c>, <c>integrator</c>).</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="name">The singleton name.</param>
    /// <param name="node">Receives the node handle.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_scene_node", CallingConvention=CallingConvention.Cdecl)] public static extern int SceneNode(ulong host, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, out ulong node);

    /// <summary><c>cbc_host_create_node</c>: creates a scene node of a native type.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="type">The native type name.</param>
    /// <param name="node">Receives the node handle.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_create_node", CallingConvention=CallingConvention.Cdecl)] public static extern int CreateNode(ulong host, [MarshalAs(UnmanagedType.LPUTF8Str)] string type, out ulong node);

    /// <summary><c>cbc_host_shader_output</c>: gets a shader graph's output node.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="shader">The shader node handle.</param>
    /// <param name="node">Receives the output node handle.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_shader_output", CallingConvention=CallingConvention.Cdecl)] public static extern int ShaderOutput(ulong host, ulong shader, out ulong node);

    /// <summary><c>cbc_host_shader_node</c>: adds a shader node of a native type to a shader graph.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="shader">The shader node handle.</param>
    /// <param name="type">The native shader node type name.</param>
    /// <param name="node">Receives the node handle.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_shader_node", CallingConvention=CallingConvention.Cdecl)] public static extern int ShaderNode(ulong host, ulong shader, [MarshalAs(UnmanagedType.LPUTF8Str)] string type, out ulong node);

    /// <summary><c>cbc_host_connect</c>: links an output socket to an input socket in one shader graph (socket UI names).</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="from">The source node handle.</param>
    /// <param name="output">The source output socket name.</param>
    /// <param name="to">The destination node handle.</param>
    /// <param name="input">The destination input socket name.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_connect", CallingConvention=CallingConvention.Cdecl)] public static extern int Connect(ulong host, ulong from, [MarshalAs(UnmanagedType.LPUTF8Str)] string output, ulong to, [MarshalAs(UnmanagedType.LPUTF8Str)] string input);

    /// <summary><c>cbc_host_set_floats</c>: sets a float-based socket from flattened components.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The flattened values.</param>
    /// <param name="count">The float count.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_set_floats", CallingConvention=CallingConvention.Cdecl)] public static extern int SetFloats(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, float* values, ulong count);

    /// <summary><c>cbc_host_set_ints</c>: sets an integer, boolean, enum or unsigned socket from 64-bit integers.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The values.</param>
    /// <param name="count">The value count.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_set_ints", CallingConvention=CallingConvention.Cdecl)] public static extern int SetInts(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, long* values, ulong count);

    /// <summary><c>cbc_host_set_string</c>: sets a string socket, or an enum socket by item name.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The UTF-8 value.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_set_string", CallingConvention=CallingConvention.Cdecl)] public static extern int SetString(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, [MarshalAs(UnmanagedType.LPUTF8Str)] string value);

    /// <summary><c>cbc_host_set_nodes</c>: sets a node or node-array socket from node handles.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="node">The node handle.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The referenced node handles (zero allowed for a single node socket).</param>
    /// <param name="count">The handle count.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_set_nodes", CallingConvention=CallingConvention.Cdecl)] public static extern int SetNodes(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ulong* values, ulong count);

    /// <summary><c>cbc_host_mesh</c>: replaces a mesh's triangle geometry.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="mesh">The mesh node handle.</param>
    /// <param name="xyz">The vertex positions as x, y, z triples.</param>
    /// <param name="vertices">The vertex count.</param>
    /// <param name="indices">The triangle vertex indices.</param>
    /// <param name="triangles">The triangle count.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_mesh", CallingConvention=CallingConvention.Cdecl)] public static extern int Mesh(ulong host, ulong mesh, float* xyz, ulong vertices, uint* indices, ulong triangles);

    /// <summary><c>cbc_host_render</c>: renders synchronously into scene-linear RGBA floats, lower-left origin.</summary>
    /// <param name="host">The host handle.</param>
    /// <param name="config">The render configuration.</param>
    /// <param name="pixels">The destination, at least width * height * 4 floats.</param>
    /// <param name="count">The destination capacity in floats.</param>
    /// <returns>The native status code.</returns>
    [DllImport(Library, EntryPoint="cbc_host_render", CallingConvention=CallingConvention.Cdecl)] public static extern int Render(ulong host, ref RenderConfig config, float* pixels, ulong count);
}
