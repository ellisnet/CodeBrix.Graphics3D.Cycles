// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Runtime.InteropServices;

namespace CodeBrix.Graphics3D.Cycles.Interop;

// P/Invoke declarations for the native scene host (native/host/cycles_host.h).
// Structure sizes and field order mirror the C ABI exactly.
internal static unsafe class Native
{
    private const string Library = "codebrix_cycles_host";
    [DllImport(Library, EntryPoint="cbc_host_color_config_path", CallingConvention=CallingConvention.Cdecl)] internal static extern int ConfigureColor([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
    [StructLayout(LayoutKind.Sequential)] internal struct HostConfig { public uint Size, Abi, Backend, Threads, R0, R1, R2, R3; }
    [StructLayout(LayoutKind.Sequential)] internal struct RenderConfig { public uint Size, Width, Height, Samples, R0, R1, R2, R3; }
    [StructLayout(LayoutKind.Sequential)] internal struct PropertyInfo { public uint Size, ValueType, Flags, Components; public ulong Count, Reserved; }
    [StructLayout(LayoutKind.Sequential)] internal struct AttributeConfig { public uint Size, Value, Domain, Flags, R0, R1, R2, R3; }
    [StructLayout(LayoutKind.Sequential)] internal struct ImageConfig { public uint Size, Width, Height, Format, R0, R1, R2, R3; }
    [StructLayout(LayoutKind.Sequential)] internal struct TierConfig { public uint Size, Abi, Tier, Flags, R0, R1, R2, R3; }
    [DllImport(Library, EntryPoint="cbc_host_tier_config", CallingConvention=CallingConvention.Cdecl)] internal static extern int ConfigureTiers(ref TierConfig config, [MarshalAs(UnmanagedType.LPUTF8Str)] string directory);
    [DllImport(Library, EntryPoint="cbc_host_device_report_json", CallingConvention=CallingConvention.Cdecl)] internal static extern int DeviceReport(ulong host, byte* json, ulong capacity, out ulong required);
    [DllImport(Library, EntryPoint="cbc_host_attribute_floats", CallingConvention=CallingConvention.Cdecl)] internal static extern int SetAttributeFloats(ulong host, ulong geometry, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, ref AttributeConfig config, float* values, ulong count);
    [DllImport(Library, EntryPoint="cbc_host_attribute_bytes", CallingConvention=CallingConvention.Cdecl)] internal static extern int SetAttributeBytes(ulong host, ulong geometry, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, ref AttributeConfig config, byte* values, ulong count);
    [DllImport(Library, EntryPoint="cbc_host_attribute_get_floats", CallingConvention=CallingConvention.Cdecl)] internal static extern int GetAttributeFloats(ulong host, ulong geometry, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, uint flags, float* values, ulong capacity, out ulong required);
    [DllImport(Library, EntryPoint="cbc_host_attribute_get_bytes", CallingConvention=CallingConvention.Cdecl)] internal static extern int GetAttributeBytes(ulong host, ulong geometry, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, uint flags, byte* values, ulong capacity, out ulong required);
    [DllImport(Library, EntryPoint="cbc_host_attribute_remove", CallingConvention=CallingConvention.Cdecl)] internal static extern int RemoveAttribute(ulong host, ulong geometry, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, uint flags);
    [DllImport(Library, EntryPoint="cbc_host_image_create", CallingConvention=CallingConvention.Cdecl)] internal static extern int CreateImage(ulong host, ref ImageConfig config, byte* pixels, ulong count, out ulong image);
    [DllImport(Library, EntryPoint="cbc_host_image_bind", CallingConvention=CallingConvention.Cdecl)] internal static extern int BindImage(ulong host, ulong texture, ulong image);
    [DllImport(Library, EntryPoint="cbc_host_image_release", CallingConvention=CallingConvention.Cdecl)] internal static extern int ReleaseImage(ulong host, ulong image);
    [DllImport(Library, EntryPoint="cbc_host_property_info", CallingConvention=CallingConvention.Cdecl)] internal static extern int GetProperty(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ref PropertyInfo info);
    [DllImport(Library, EntryPoint="cbc_host_get_floats", CallingConvention=CallingConvention.Cdecl)] internal static extern int GetFloats(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, float* values, ulong capacity, out ulong required);
    [DllImport(Library, EntryPoint="cbc_host_get_ints", CallingConvention=CallingConvention.Cdecl)] internal static extern int GetInts(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, long* values, ulong capacity, out ulong required);
    [DllImport(Library, EntryPoint="cbc_host_get_uints", CallingConvention=CallingConvention.Cdecl)] internal static extern int GetUInts(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ulong* values, ulong capacity, out ulong required);
    [DllImport(Library, EntryPoint="cbc_host_set_uints", CallingConvention=CallingConvention.Cdecl)] internal static extern int SetUInts(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ulong* values, ulong count);
    [DllImport(Library, EntryPoint="cbc_host_get_string", CallingConvention=CallingConvention.Cdecl)] internal static extern int GetString(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ulong index, byte* values, ulong capacity, out ulong required);
    [DllImport(Library, EntryPoint="cbc_host_get_nodes", CallingConvention=CallingConvention.Cdecl)] internal static extern int GetNodes(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ulong* values, ulong capacity, out ulong required);
    [DllImport(Library, EntryPoint="cbc_host_registry_json", CallingConvention=CallingConvention.Cdecl)] internal static extern int Registry(byte* values, ulong capacity, out ulong required);
    [DllImport(Library, EntryPoint="cbc_host_error", CallingConvention=CallingConvention.Cdecl)] internal static extern int Error(byte* data, ulong size, out ulong required);
    [DllImport(Library, EntryPoint="cbc_host_create", CallingConvention=CallingConvention.Cdecl)] internal static extern int Create(ref HostConfig config, out ulong host);
    [DllImport(Library, EntryPoint="cbc_host_destroy", CallingConvention=CallingConvention.Cdecl)] internal static extern int Destroy(ulong host);
    [DllImport(Library, EntryPoint="cbc_host_cancel", CallingConvention=CallingConvention.Cdecl)] internal static extern int Cancel(ulong host);
    [DllImport(Library, EntryPoint="cbc_host_is_rendering", CallingConvention=CallingConvention.Cdecl)] internal static extern int IsRendering(ulong host, out uint active);
    [DllImport(Library, EntryPoint="cbc_host_scene_node", CallingConvention=CallingConvention.Cdecl)] internal static extern int SceneNode(ulong host, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, out ulong node);
    [DllImport(Library, EntryPoint="cbc_host_create_node", CallingConvention=CallingConvention.Cdecl)] internal static extern int CreateNode(ulong host, [MarshalAs(UnmanagedType.LPUTF8Str)] string type, out ulong node);
    [DllImport(Library, EntryPoint="cbc_host_shader_output", CallingConvention=CallingConvention.Cdecl)] internal static extern int ShaderOutput(ulong host, ulong shader, out ulong node);
    [DllImport(Library, EntryPoint="cbc_host_shader_node", CallingConvention=CallingConvention.Cdecl)] internal static extern int ShaderNode(ulong host, ulong shader, [MarshalAs(UnmanagedType.LPUTF8Str)] string type, out ulong node);
    [DllImport(Library, EntryPoint="cbc_host_connect", CallingConvention=CallingConvention.Cdecl)] internal static extern int Connect(ulong host, ulong from, [MarshalAs(UnmanagedType.LPUTF8Str)] string output, ulong to, [MarshalAs(UnmanagedType.LPUTF8Str)] string input);
    [DllImport(Library, EntryPoint="cbc_host_set_floats", CallingConvention=CallingConvention.Cdecl)] internal static extern int SetFloats(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, float* values, ulong count);
    [DllImport(Library, EntryPoint="cbc_host_set_ints", CallingConvention=CallingConvention.Cdecl)] internal static extern int SetInts(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, long* values, ulong count);
    [DllImport(Library, EntryPoint="cbc_host_set_string", CallingConvention=CallingConvention.Cdecl)] internal static extern int SetString(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, [MarshalAs(UnmanagedType.LPUTF8Str)] string value);
    [DllImport(Library, EntryPoint="cbc_host_set_nodes", CallingConvention=CallingConvention.Cdecl)] internal static extern int SetNodes(ulong host, ulong node, [MarshalAs(UnmanagedType.LPUTF8Str)] string socket, ulong* values, ulong count);
    [DllImport(Library, EntryPoint="cbc_host_mesh", CallingConvention=CallingConvention.Cdecl)] internal static extern int Mesh(ulong host, ulong mesh, float* xyz, ulong vertices, uint* indices, ulong triangles);
    [DllImport(Library, EntryPoint="cbc_host_render", CallingConvention=CallingConvention.Cdecl)] internal static extern int Render(ulong host, ref RenderConfig config, float* pixels, ulong count);
}
