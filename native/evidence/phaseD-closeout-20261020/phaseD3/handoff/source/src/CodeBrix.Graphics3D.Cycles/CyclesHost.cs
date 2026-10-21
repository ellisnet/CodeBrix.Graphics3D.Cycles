// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace CodeBrix.Graphics3D.Cycles;

// Metal is Cycles' own Metal device, available only in the osx-arm64 build.
// Gpu selects the binary's GPU backend: Metal in macOS builds, WebGPU elsewhere.
public enum Backend : uint { WebGpu = 1, CpuReference = 2, Metal = 3, Gpu = 4 }
public readonly record struct Node(ulong Value);
public readonly record struct Image(ulong Value);
public enum AttributeValue : uint { Float = 5, Color = 6, Vector = 7, Point = 8, Normal = 9, Point2 = 10, Transform = 13, Float4 = 16, Byte4 = 17 }
public enum AttributeDomain : uint { Mesh = 1, Face = 2, Vertex = 3, Corner = 4, Curve = 5, CurveKey = 6, VertexNormal = 7, CornerNormal = 8, CornerByte = 9, CurveKeyNormal = 10 }
[Flags] public enum AttributeFlags : uint { None = 0, Standard = 1, Subdivision = 2 }
public enum ImageFormat : uint { Float = 1, Float4 = 2, Half = 3, Half4 = 4, Byte = 5, Byte4 = 6, UShort = 7, UShort4 = 8 }
public readonly record struct PropertyInfo(uint ValueType, uint Flags, uint Components, ulong Count);
// WebGPU pipeline tier (graceful degradation; native/DEVICE.md "Device tiers").
public enum PipelineTier : uint { Auto = 0, Full = 1, Conservative = 2 }
[Flags] public enum PipelineTierFlags : uint { None = 0, RecordOff = 1, RecordReadOnly = 2, Canary = 4 }
public sealed class NativeFailure(int status, string message) : Exception(message)
{
    public int Status { get; } = status;
}

// Experimental thin ownership and marshalling layer. Scene updates and rendering
// stay in the native library. Dispose cancels and waits for active native work.
public sealed unsafe class CyclesHost : IDisposable
{
    private sealed class Lifetime : SafeHandleZeroOrMinusOneIsInvalid
    {
        public Lifetime(ulong value) : base(true)
        {
            SetHandle(unchecked((nint)value));
        }
        protected override bool ReleaseHandle() => Native.Destroy(unchecked((ulong)handle)) == 0;
    }

    private readonly Lifetime lifetime;
    private ulong Handle
    {
        get
        {
            ObjectDisposedException.ThrowIf(lifetime.IsClosed, this);
            return unchecked((ulong)lifetime.DangerousGetHandle());
        }
    }

    public CyclesHost(Backend backend, uint threads = 1)
    {
        if (IntPtr.Size != 8) throw new PlatformNotSupportedException("This experimental build requires a 64-bit process.");
        EnsureSupportedOperatingSystem();
        Native.HostConfig config = new() { Size = 32, Abi = 1, Backend = (uint)backend, Threads = threads };
        Check(Native.Create(ref config, out ulong value));
        lifetime = new Lifetime(value);
    }

    public void Dispose() => lifetime.Dispose();
    // Native Cycles shares OCIO process-wide. Configure only when no hosts or
    // in-flight calls remain; the native layer serializes against host creation.
    public static void ConfigureColor(string path)
    {
        EnsureSupportedOperatingSystem();
        Check(Native.ConfigureColor(path));
    }

    // The macOS loader does not enforce the native library's minimum OS, so
    // refuse before the first P/Invoke loads it. The native host checks again.
    private static void EnsureSupportedOperatingSystem()
    {
        if (OperatingSystem.IsMacOS() && !OperatingSystem.IsMacOSVersionAtLeast(15))
            throw new PlatformNotSupportedException("CodeBrix.Graphics3D.Cycles requires macOS 15.0 or later.");
    }

    private static void Check(int status)
    {
        if (status == 0) return;
        byte[] bytes = new byte[2048];
        fixed (byte* data = bytes)
        {
            int copied = Native.Error(data, (ulong)bytes.Length, out ulong required);
            string error = copied == 0 ? Encoding.UTF8.GetString(bytes, 0, checked((int)required - 1)) : "Native operation failed";
            throw new NativeFailure(status, error);
        }
    }

    // IDs are validated by the native registry, which retains the context while
    // calls run. KeepAlive prevents finalization; explicit concurrent Dispose is
    // deliberately allowed to enter native destruction and cancel that work.
    private void Finish(int status) { GC.KeepAlive(lifetime); Check(status); }
    public Node SceneNode(string name) { int s = Native.SceneNode(Handle, name, out ulong n); Finish(s); return new(n); }
    public Node CreateNode(string type) { int s = Native.CreateNode(Handle, type, out ulong n); Finish(s); return new(n); }
    public Node ShaderOutput(Node shader) { int s = Native.ShaderOutput(Handle, shader.Value, out ulong n); Finish(s); return new(n); }
    public Node ShaderNode(Node shader, string type) { int s = Native.ShaderNode(Handle, shader.Value, type, out ulong n); Finish(s); return new(n); }
    public void Connect(Node from, string output, Node to, string input) => Finish(Native.Connect(Handle, from.Value, output, to.Value, input));
    public void SetString(Node node, string socket, string value) => Finish(Native.SetString(Handle, node.Value, socket, value));
    public void SetFloats(Node node, string socket, ReadOnlySpan<float> values)
    {
        fixed (float* data = values) Finish(Native.SetFloats(Handle, node.Value, socket, data, (ulong)values.Length));
    }
    public void SetInts(Node node, string socket, ReadOnlySpan<long> values)
    {
        fixed (long* data = values) Finish(Native.SetInts(Handle, node.Value, socket, data, (ulong)values.Length));
    }
    public PropertyInfo Property(Node node, string socket)
    {
        Native.PropertyInfo value = new() { Size = 32 };
        Finish(Native.GetProperty(Handle, node.Value, socket, ref value));
        return new(value.ValueType, value.Flags, value.Components, value.Count);
    }
    // Span overloads let callers manage query/copy races explicitly. A return of
    // false means the destination was left unchanged and required is its new size.
    public bool GetFloats(Node node, string socket, Span<float> values, out ulong required)
    {
        int status;
        fixed (float* data = values) status = Native.GetFloats(Handle, node.Value, socket, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }
    public bool GetInts(Node node, string socket, Span<long> values, out ulong required)
    {
        int status;
        fixed (long* data = values) status = Native.GetInts(Handle, node.Value, socket, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }
    public void SetUInt(Node node, string socket, ulong value) => Finish(Native.SetUInts(Handle, node.Value, socket, &value, 1));
    public ulong GetUInt(Node node, string socket)
    {
        ulong value = 0;
        Finish(Native.GetUInts(Handle, node.Value, socket, &value, 1, out ulong required));
        if (required != 1) throw new InvalidOperationException("Unexpected unsigned property size");
        return value;
    }
    public bool GetUtf8(Node node, string socket, ulong index, Span<byte> values, out ulong required)
    {
        int status;
        fixed (byte* data = values) status = Native.GetString(Handle, node.Value, socket, index, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }
    public bool GetNodes(Node node, string socket, Span<ulong> values, out ulong required)
    {
        int status;
        fixed (ulong* data = values) status = Native.GetNodes(Handle, node.Value, socket, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }
    // Process-wide; applies to WebGPU hosts created afterwards. recordDirectory
    // is an app-writable folder for the persisted device-identity record (null
    // disables it).
    public static void ConfigureTiers(PipelineTier tier, string? recordDirectory, PipelineTierFlags flags = PipelineTierFlags.None)
    {
        EnsureSupportedOperatingSystem();
        Native.TierConfig config = new() { Size = 32, Abi = 1, Tier = (uint)tier, Flags = (uint)flags };
        Check(Native.ConfigureTiers(ref config, recordDirectory));
    }
    // Capability report JSON: backend, adapter, chosen tier and reason, record,
    // canary, compile budget, warnings and compiled pipelines.
    public string DeviceReportJson()
    {
        int status = Native.DeviceReport(Handle, null, 0, out ulong required);
        GC.KeepAlive(lifetime);
        if (status != 6) Check(status);
        for (;;)
        {
            byte[] result = new byte[checked((int)required)];
            fixed (byte* data = result) status = Native.DeviceReport(Handle, data, (ulong)result.Length, out required);
            GC.KeepAlive(lifetime);
            if (status == 6) continue;
            Check(status);
            return Encoding.UTF8.GetString(result, 0, checked((int)required - 1));
        }
    }
    public static string RegistryJson()
    {
        EnsureSupportedOperatingSystem();
        int status = Native.Registry(null, 0, out ulong required);
        if (status != 6) Check(status);
        byte[] result = new byte[checked((int)required)];
        fixed (byte* data = result) Check(Native.Registry(data, (ulong)result.Length, out required));
        return Encoding.UTF8.GetString(result, 0, checked((int)required - 1));
    }
    public void SetNodes(Node node, string socket, ReadOnlySpan<Node> values)
    {
        // Marshal explicit integer IDs without relying on the managed Node layout.
        ulong[] ids = new ulong[values.Length];
        for (int i = 0; i < ids.Length; ++i) ids[i] = values[i].Value;
        fixed (ulong* data = ids) Finish(Native.SetNodes(Handle, node.Value, socket, data, (ulong)ids.Length));
    }
    public void Mesh(Node node, ReadOnlySpan<float> positions, ReadOnlySpan<uint> indices)
    {
        if (positions.Length % 3 != 0 || indices.Length % 3 != 0) throw new ArgumentException("Complete vertices and triangles are required.");
        fixed (float* xyz = positions)
        fixed (uint* triangles = indices)
            Finish(Native.Mesh(Handle, node.Value, xyz, (ulong)(positions.Length / 3), triangles, (ulong)(indices.Length / 3)));
    }
    public void SetAttribute(Node geometry, string name, AttributeValue value, AttributeDomain domain, AttributeFlags flags, ReadOnlySpan<float> values)
    {
        Native.AttributeConfig config = new() { Size = 32, Value = (uint)value, Domain = (uint)domain, Flags = (uint)flags };
        fixed (float* data = values) Finish(Native.SetAttributeFloats(Handle, geometry.Value, name, ref config, data, (ulong)values.Length));
    }
    public void SetAttributeBytes(Node geometry, string name, AttributeDomain domain, AttributeFlags flags, ReadOnlySpan<byte> values)
    {
        Native.AttributeConfig config = new() { Size = 32, Value = (uint)AttributeValue.Byte4, Domain = (uint)domain, Flags = (uint)flags };
        fixed (byte* data = values) Finish(Native.SetAttributeBytes(Handle, geometry.Value, name, ref config, data, (ulong)values.Length));
    }
    public bool GetAttribute(Node geometry, string name, AttributeFlags flags, Span<float> values, out ulong required)
    {
        int status;
        fixed (float* data = values) status = Native.GetAttributeFloats(Handle, geometry.Value, name, (uint)flags, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }
    public bool GetAttributeBytes(Node geometry, string name, AttributeFlags flags, Span<byte> values, out ulong required)
    {
        int status;
        fixed (byte* data = values) status = Native.GetAttributeBytes(Handle, geometry.Value, name, (uint)flags, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }
    public void RemoveAttribute(Node geometry, string name, AttributeFlags flags = AttributeFlags.None) => Finish(Native.RemoveAttribute(Handle, geometry.Value, name, (uint)flags));
    // Image bytes are copied by the native call. ReleaseImage drops the caller's
    // ID; graph bindings retain their own native ownership until replaced/destroyed.
    public Image CreateImage(uint width, uint height, ImageFormat format, ReadOnlySpan<byte> pixels)
    {
        Native.ImageConfig config = new() { Size = 32, Width = width, Height = height, Format = (uint)format };
        ulong image;
        fixed (byte* data = pixels) Finish(Native.CreateImage(Handle, ref config, data, (ulong)pixels.Length, out image));
        return new(image);
    }
    public void BindImage(Node texture, Image image) => Finish(Native.BindImage(Handle, texture.Value, image.Value));
    public void ReleaseImage(Image image) => Finish(Native.ReleaseImage(Handle, image.Value));
    public void Render(uint width, uint height, uint samples, Span<float> pixels)
    {
        Native.RenderConfig config = new() { Size = 32, Width = width, Height = height, Samples = samples };
        fixed (float* data = pixels) Finish(Native.Render(Handle, ref config, data, (ulong)pixels.Length));
    }
    public bool IsRendering { get { int s = Native.IsRendering(Handle, out uint active); Finish(s); return active != 0; } }
    public bool Cancel()
    {
        int status = Native.Cancel(Handle); GC.KeepAlive(lifetime);
        if (status == 5) return false;
        Check(status); return true;
    }
}

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
    [DllImport(Library, EntryPoint="cbc_host_tier_config", CallingConvention=CallingConvention.Cdecl)] internal static extern int ConfigureTiers(ref TierConfig config, [MarshalAs(UnmanagedType.LPUTF8Str)] string? directory);
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
    // Phase D additions to ABI 1 (native/host/ABI.md). Thin declarations only;
    // the typed managed surface wraps them.
    [StructLayout(LayoutKind.Sequential)] internal struct VersionInfo { public uint Size, Abi, R0, R1; public fixed byte LibraryVersion[32]; public fixed byte CyclesRevision[48]; }
    [StructLayout(LayoutKind.Sequential)] internal struct Event { public uint Size, Kind; public ulong SamplesDone, SamplesTotal; public double ElapsedSeconds; public byte* Message; }
    [DllImport(Library, EntryPoint="cbc_host_version", CallingConvention=CallingConvention.Cdecl)] internal static extern int Version(ref VersionInfo info);
    [DllImport(Library, EntryPoint="cbc_host_resolve_backend", CallingConvention=CallingConvention.Cdecl)] internal static extern int ResolveBackend(uint requested, out uint resolved);
    [DllImport(Library, EntryPoint="cbc_host_backend", CallingConvention=CallingConvention.Cdecl)] internal static extern int GetBackend(ulong host, out uint backend);
    [DllImport(Library, EntryPoint="cbc_host_capabilities_json", CallingConvention=CallingConvention.Cdecl)] internal static extern int Capabilities(ulong host, byte* json, ulong capacity, out ulong required);
    [DllImport(Library, EntryPoint="cbc_host_delete_node", CallingConvention=CallingConvention.Cdecl)] internal static extern int DeleteNode(ulong host, ulong node);
    [DllImport(Library, EntryPoint="cbc_host_image_update", CallingConvention=CallingConvention.Cdecl)] internal static extern int UpdateImage(ulong host, ulong image, ref ImageConfig config, byte* pixels, ulong count);
    [DllImport(Library, EntryPoint="cbc_host_motion_positions", CallingConvention=CallingConvention.Cdecl)] internal static extern int MotionPositions(ulong host, ulong mesh, uint steps, float* xyz, ulong count);
    [DllImport(Library, EntryPoint="cbc_host_set_event_callback", CallingConvention=CallingConvention.Cdecl)] internal static extern int SetEventCallback(ulong host, delegate* unmanaged[Cdecl]<nint, Event*, int> callback, nint userData);
    [DllImport(Library, EntryPoint="cbc_host_pass_floats", CallingConvention=CallingConvention.Cdecl)] internal static extern int PassFloats(ulong host, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, float* values, ulong capacity, out ulong required, out uint components);
    [DllImport(Library, EntryPoint="cbc_host_render_passes_json", CallingConvention=CallingConvention.Cdecl)] internal static extern int RenderPasses(ulong host, byte* json, ulong capacity, out ulong required);
}
