// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;
using System.Text;
using CodeBrix.Graphics3D.Cycles.Interop;
using Microsoft.Win32.SafeHandles;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Owns one native Cycles scene host: its scene graph, images and render session. This is an
/// experimental thin ownership and marshalling layer; scene compilation, BVH construction, image
/// handling, color management, scheduling and rendering all stay in the native library.
/// </summary>
/// <remarks>
/// Inputs (arrays, strings, image pixels) are copied by the native call before it returns. All
/// edits serialize with rendering. <see cref="Cancel"/> and <see cref="Dispose"/> may run
/// concurrently with a render; <see cref="Dispose"/> cancels and waits for active native work.
/// Every failing native call throws <see cref="NativeFailure"/>.
/// </remarks>
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

    /// <summary>
    /// Creates a native scene host that renders with the given backend.
    /// </summary>
    /// <param name="backend">The device the host renders with.</param>
    /// <param name="threads">The number of native worker threads (CPU reference backend).</param>
    /// <exception cref="PlatformNotSupportedException">The process is not 64-bit, or the operating system is older than the native library supports.</exception>
    /// <exception cref="NativeFailure">The native host could not be created (for example an unsupported backend for this build).</exception>
    public CyclesHost(Backend backend, uint threads = 1)
    {
        if (IntPtr.Size != 8) throw new PlatformNotSupportedException("This experimental build requires a 64-bit process.");
        EnsureSupportedOperatingSystem();
        Native.HostConfig config = new() { Size = 32, Abi = 1, Backend = (uint)backend, Threads = threads };
        Check(Native.Create(ref config, out ulong value));
        lifetime = new Lifetime(value);
    }

    /// <summary>
    /// Destroys the native host: removes its handle, cancels active work and waits for cleanup.
    /// Calling it more than once has no further effect.
    /// </summary>
    public void Dispose() => lifetime.Dispose();

    // Native Cycles shares OCIO process-wide. Configure only when no hosts or
    // in-flight calls remain; the native layer serializes against host creation.
    /// <summary>
    /// Selects the process-wide OpenColorIO configuration file. Call it before creating any host or
    /// after every host and its in-flight calls have finished; a failed load keeps the previous
    /// configuration.
    /// </summary>
    /// <param name="path">The path of the color configuration file.</param>
    /// <exception cref="NativeFailure">The configuration could not be loaded, or a host still exists (status 5).</exception>
    public static void ConfigureColor(string path)
    {
        EnsureSupportedOperatingSystem();
        Check(Native.ConfigureColor(path));
    }

    // The macOS loader does not enforce the native library's minimum OS, so
    // refuse before the first P/Invoke loads it. The native host checks again.
    internal static void EnsureSupportedOperatingSystem()
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

    /// <summary>
    /// Gets one of the scene's existing singleton nodes.
    /// </summary>
    /// <param name="name">The singleton name: <c>camera</c>, <c>background</c>, <c>film</c> or <c>integrator</c>.</param>
    /// <returns>The singleton node.</returns>
    public Node SceneNode(string name) { int s = Native.SceneNode(Handle, name, out ulong n); Finish(s); return new(n); }

    /// <summary>
    /// Creates a scene node of a native registered type (for example <c>object</c>, <c>mesh</c>, <c>light</c> or <c>shader</c>).
    /// </summary>
    /// <param name="type">The native node type name.</param>
    /// <returns>The new node.</returns>
    public Node CreateNode(string type) { int s = Native.CreateNode(Handle, type, out ulong n); Finish(s); return new(n); }

    /// <summary>
    /// Gets the output node of a shader graph, whose <c>Surface</c>, <c>Volume</c> and <c>Displacement</c> inputs receive the graph's closures.
    /// </summary>
    /// <param name="shader">A node created with type <c>shader</c>.</param>
    /// <returns>The graph's output node.</returns>
    public Node ShaderOutput(Node shader) { int s = Native.ShaderOutput(Handle, shader.Value, out ulong n); Finish(s); return new(n); }

    /// <summary>
    /// Adds a shader node of a native registered type (for example <c>diffuse_bsdf</c> or <c>image_texture</c>) to a shader graph.
    /// </summary>
    /// <param name="shader">A node created with type <c>shader</c>.</param>
    /// <param name="type">The native shader node type name.</param>
    /// <returns>The new shader node.</returns>
    public Node ShaderNode(Node shader, string type) { int s = Native.ShaderNode(Handle, shader.Value, type, out ulong n); Finish(s); return new(n); }

    /// <summary>
    /// Links an output socket of one shader node to an input socket of another in the same graph.
    /// </summary>
    /// <param name="from">The source shader node.</param>
    /// <param name="output">The source output socket name (for example <c>BSDF</c> or <c>Color</c>).</param>
    /// <param name="to">The destination shader node.</param>
    /// <param name="input">The destination input socket name (for example <c>Surface</c>).</param>
    public void Connect(Node from, string output, Node to, string input) => Finish(Native.Connect(Handle, from.Value, output, to.Value, input));

    /// <summary>
    /// Sets a string or enum socket.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The UTF-8 value (for enum sockets, the enum item name).</param>
    public void SetString(Node node, string socket, string value) => Finish(Native.SetString(Handle, node.Value, socket, value));

    /// <summary>
    /// Sets a float-based socket from flattened components (float2 = 2, float3 = 3, row-major transform = 12 per element).
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The flattened values; copied before the call returns.</param>
    public void SetFloats(Node node, string socket, ReadOnlySpan<float> values)
    {
        fixed (float* data = values) Finish(Native.SetFloats(Handle, node.Value, socket, data, (ulong)values.Length));
    }

    /// <summary>
    /// Sets an integer or boolean socket (booleans are 0 or 1).
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The values; copied before the call returns.</param>
    public void SetInts(Node node, string socket, ReadOnlySpan<long> values)
    {
        fixed (long* data = values) Finish(Native.SetInts(Handle, node.Value, socket, data, (ulong)values.Length));
    }

    /// <summary>
    /// Describes a socket: its value kind, flags, component count and current element count.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The socket description.</returns>
    public PropertyInfo Property(Node node, string socket)
    {
        Native.PropertyInfo value = new() { Size = 32 };
        Finish(Native.GetProperty(Handle, node.Value, socket, ref value));
        return new(value.ValueType, value.Flags, value.Components, value.Count);
    }

    // Span overloads let callers manage query/copy races explicitly. A return of
    // false means the destination was left unchanged and required is its new size.
    /// <summary>
    /// Reads a float-based socket as flattened components. Pass an empty span to query the size.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The destination; left unchanged when it is too small.</param>
    /// <param name="required">Receives the number of floats the socket holds.</param>
    /// <returns><see langword="true"/> when the values were copied; <see langword="false"/> when <paramref name="values"/> is too small.</returns>
    public bool GetFloats(Node node, string socket, Span<float> values, out ulong required)
    {
        int status;
        fixed (float* data = values) status = Native.GetFloats(Handle, node.Value, socket, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }

    /// <summary>
    /// Reads an integer, boolean or enum socket. Pass an empty span to query the size.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The destination; left unchanged when it is too small.</param>
    /// <param name="required">Receives the number of values the socket holds.</param>
    /// <returns><see langword="true"/> when the values were copied; <see langword="false"/> when <paramref name="values"/> is too small.</returns>
    public bool GetInts(Node node, string socket, Span<long> values, out ulong required)
    {
        int status;
        fixed (long* data = values) status = Native.GetInts(Handle, node.Value, socket, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }

    /// <summary>
    /// Sets an unsigned (uint or uint64) scalar socket.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value.</param>
    public void SetUInt(Node node, string socket, ulong value) => Finish(Native.SetUInts(Handle, node.Value, socket, &value, 1));

    /// <summary>
    /// Reads an unsigned (uint or uint64) scalar socket.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The value.</returns>
    /// <exception cref="InvalidOperationException">The socket does not hold exactly one value.</exception>
    public ulong GetUInt(Node node, string socket)
    {
        ulong value = 0;
        Finish(Native.GetUInts(Handle, node.Value, socket, &value, 1, out ulong required));
        if (required != 1) throw new InvalidOperationException("Unexpected unsigned property size");
        return value;
    }

    /// <summary>
    /// Reads one string element of a string or enum socket as UTF-8 bytes including the trailing NUL.
    /// Pass an empty span to query the size.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="index">The element index (0 for a scalar string socket).</param>
    /// <param name="values">The destination; left unchanged when it is too small.</param>
    /// <param name="required">Receives the byte count including the trailing NUL.</param>
    /// <returns><see langword="true"/> when the bytes were copied; <see langword="false"/> when <paramref name="values"/> is too small.</returns>
    public bool GetUtf8(Node node, string socket, ulong index, Span<byte> values, out ulong required)
    {
        int status;
        fixed (byte* data = values) status = Native.GetString(Handle, node.Value, socket, index, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }

    /// <summary>
    /// Reads a node or node-array socket as native node identifiers. Pass an empty span to query the size.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The destination; left unchanged when it is too small.</param>
    /// <param name="required">Receives the number of node identifiers.</param>
    /// <returns><see langword="true"/> when the identifiers were copied; <see langword="false"/> when <paramref name="values"/> is too small.</returns>
    public bool GetNodes(Node node, string socket, Span<ulong> values, out ulong required)
    {
        int status;
        fixed (ulong* data = values) status = Native.GetNodes(Handle, node.Value, socket, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }

    // Process-wide; applies to WebGPU hosts created afterwards. recordDirectory
    // is an app-writable folder for the persisted device-identity record (null
    // disables it).
    /// <summary>
    /// Configures WebGPU pipeline-tier selection for the whole process. It applies to WebGPU hosts
    /// created after the call.
    /// </summary>
    /// <param name="tier">The requested tier.</param>
    /// <param name="recordDirectory">An app-writable directory for the persisted device-identity record (created if missing); <see langword="null"/> or empty disables the record.</param>
    /// <param name="flags">Record and canary options.</param>
    public static void ConfigureTiers(PipelineTier tier, string recordDirectory, PipelineTierFlags flags = PipelineTierFlags.None)
    {
        EnsureSupportedOperatingSystem();
        Native.TierConfig config = new() { Size = 32, Abi = 1, Tier = (uint)tier, Flags = (uint)flags };
        Check(Native.ConfigureTiers(ref config, recordDirectory));
    }

    // Capability report JSON: backend, adapter, chosen tier and reason, record,
    // canary, compile budget, warnings and compiled pipelines.
    /// <summary>
    /// Returns the host's device capability report as JSON: backend, adapter, chosen tier and the
    /// reason for it, record path and status, canary result, compile budget, warnings and compiled
    /// pipelines.
    /// </summary>
    /// <returns>The JSON report.</returns>
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

    /// <summary>
    /// Returns the native node and property registration metadata as JSON. It describes native
    /// properties only; it does not state which features a GPU backend implements.
    /// </summary>
    /// <returns>The JSON registry.</returns>
    public static string RegistryJson()
    {
        EnsureSupportedOperatingSystem();
        int status = Native.Registry(null, 0, out ulong required);
        if (status != 6) Check(status);
        byte[] result = new byte[checked((int)required)];
        fixed (byte* data = result) Check(Native.Registry(data, (ulong)result.Length, out required));
        return Encoding.UTF8.GetString(result, 0, checked((int)required - 1));
    }

    /// <summary>
    /// Sets a string-array socket. Every element is copied before the call returns.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The strings; none may be <see langword="null"/>.</param>
    /// <exception cref="ArgumentNullException">An element is <see langword="null"/>.</exception>
    public void SetStrings(Node node, string socket, ReadOnlySpan<string> values)
    {
        byte[][] utf8 = new byte[values.Length][];
        int total = 0;
        for (int i = 0; i < values.Length; ++i)
        {
            if (values[i] == null) throw new ArgumentNullException(nameof(values), "String array elements cannot be null.");
            utf8[i] = Encoding.UTF8.GetBytes(values[i]);
            total += utf8[i].Length + 1;
        }
        byte[] storage = new byte[Math.Max(total, 1)];
        int[] offsets = new int[values.Length];
        for (int i = 0, offset = 0; i < utf8.Length; ++i)
        {
            offsets[i] = offset;
            utf8[i].CopyTo(storage, offset);
            offset += utf8[i].Length + 1;
        }
        byte*[] pointers = new byte*[values.Length];
        fixed (byte* data = storage)
        {
            for (int i = 0; i < pointers.Length; ++i) pointers[i] = data + offsets[i];
            fixed (byte** list = pointers) Finish(Native.SetStrings(Handle, node.Value, socket, list, (ulong)pointers.Length));
        }
    }

    /// <summary>
    /// Returns a node's native registry type name (for example <c>mesh</c> or <c>diffuse_bsdf</c>).
    /// </summary>
    /// <param name="node">The node.</param>
    /// <returns>The native type name.</returns>
    public string NodeTypeName(Node node)
    {
        int status = Native.NodeType(Handle, node.Value, null, 0, out ulong required);
        GC.KeepAlive(lifetime);
        if (status != 6) Check(status);
        for (;;)
        {
            byte[] result = new byte[checked((int)required)];
            fixed (byte* data = result) status = Native.NodeType(Handle, node.Value, data, (ulong)result.Length, out required);
            GC.KeepAlive(lifetime);
            if (status == 6) continue;
            Check(status);
            return Encoding.UTF8.GetString(result, 0, checked((int)required - 1));
        }
    }

    /// <summary>
    /// Sets a node or node-array socket (for example an object's <c>geometry</c> or a mesh's <c>used_shaders</c>).
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The referenced nodes.</param>
    public void SetNodes(Node node, string socket, ReadOnlySpan<Node> values)
    {
        // Marshal explicit integer IDs without relying on the managed Node layout.
        ulong[] ids = new ulong[values.Length];
        for (int i = 0; i < ids.Length; ++i) ids[i] = values[i].Value;
        fixed (ulong* data = ids) Finish(Native.SetNodes(Handle, node.Value, socket, data, (ulong)ids.Length));
    }

    /// <summary>
    /// Replaces a mesh's triangle geometry. Every face initially uses material 0.
    /// </summary>
    /// <param name="node">A node created with type <c>mesh</c>.</param>
    /// <param name="positions">Vertex positions as x, y, z triples.</param>
    /// <param name="indices">Triangle vertex indices, three per triangle.</param>
    /// <exception cref="ArgumentException">Either span length is not a multiple of three.</exception>
    public void Mesh(Node node, ReadOnlySpan<float> positions, ReadOnlySpan<uint> indices)
    {
        if (positions.Length % 3 != 0 || indices.Length % 3 != 0) throw new ArgumentException("Complete vertices and triangles are required.");
        fixed (float* xyz = positions)
        fixed (uint* triangles = indices)
            Finish(Native.Mesh(Handle, node.Value, xyz, (ulong)(positions.Length / 3), triangles, (ulong)(indices.Length / 3)));
    }

    /// <summary>
    /// Creates or replaces a float-based geometry attribute. The element count derives from the
    /// current geometry, so establish the topology first.
    /// </summary>
    /// <param name="geometry">The geometry node.</param>
    /// <param name="name">The attribute name (a standard name with <see cref="AttributeFlags.Standard"/>, otherwise a custom name).</param>
    /// <param name="value">The element type.</param>
    /// <param name="domain">The element domain.</param>
    /// <param name="flags">Standard and subdivision options.</param>
    /// <param name="values">The flattened values; copied before the call returns.</param>
    public void SetAttribute(Node geometry, string name, AttributeValue value, AttributeDomain domain, AttributeFlags flags, ReadOnlySpan<float> values)
    {
        Native.AttributeConfig config = new() { Size = 32, Value = (uint)value, Domain = (uint)domain, Flags = (uint)flags };
        fixed (float* data = values) Finish(Native.SetAttributeFloats(Handle, geometry.Value, name, ref config, data, (ulong)values.Length));
    }

    /// <summary>
    /// Creates or replaces a four-byte-per-element geometry attribute.
    /// </summary>
    /// <param name="geometry">The geometry node.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="domain">The element domain.</param>
    /// <param name="flags">Standard and subdivision options.</param>
    /// <param name="values">The bytes, four per element; copied before the call returns.</param>
    public void SetAttributeBytes(Node geometry, string name, AttributeDomain domain, AttributeFlags flags, ReadOnlySpan<byte> values)
    {
        Native.AttributeConfig config = new() { Size = 32, Value = (uint)AttributeValue.Byte4, Domain = (uint)domain, Flags = (uint)flags };
        fixed (byte* data = values) Finish(Native.SetAttributeBytes(Handle, geometry.Value, name, ref config, data, (ulong)values.Length));
    }

    /// <summary>
    /// Reads a float-based geometry attribute. Normal domains return decoded directions. Pass an empty span to query the size.
    /// </summary>
    /// <param name="geometry">The geometry node.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="flags">Standard and subdivision options used when the attribute was set.</param>
    /// <param name="values">The destination; left unchanged when it is too small.</param>
    /// <param name="required">Receives the number of floats.</param>
    /// <returns><see langword="true"/> when the values were copied; <see langword="false"/> when <paramref name="values"/> is too small.</returns>
    public bool GetAttribute(Node geometry, string name, AttributeFlags flags, Span<float> values, out ulong required)
    {
        int status;
        fixed (float* data = values) status = Native.GetAttributeFloats(Handle, geometry.Value, name, (uint)flags, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }

    /// <summary>
    /// Reads a four-byte-per-element geometry attribute. Pass an empty span to query the size.
    /// </summary>
    /// <param name="geometry">The geometry node.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="flags">Standard and subdivision options used when the attribute was set.</param>
    /// <param name="values">The destination; left unchanged when it is too small.</param>
    /// <param name="required">Receives the number of bytes.</param>
    /// <returns><see langword="true"/> when the bytes were copied; <see langword="false"/> when <paramref name="values"/> is too small.</returns>
    public bool GetAttributeBytes(Node geometry, string name, AttributeFlags flags, Span<byte> values, out ulong required)
    {
        int status;
        fixed (byte* data = values) status = Native.GetAttributeBytes(Handle, geometry.Value, name, (uint)flags, data, (ulong)values.Length, out required);
        GC.KeepAlive(lifetime); if (status == 6) return false; Check(status); return true;
    }

    /// <summary>
    /// Removes a geometry attribute.
    /// </summary>
    /// <param name="geometry">The geometry node.</param>
    /// <param name="name">The attribute name.</param>
    /// <param name="flags">Standard and subdivision options used when the attribute was set.</param>
    public void RemoveAttribute(Node geometry, string name, AttributeFlags flags = AttributeFlags.None) => Finish(Native.RemoveAttribute(Handle, geometry.Value, name, (uint)flags));

    // Image bytes are copied by the native call. ReleaseImage drops the caller's
    // ID; graph bindings retain their own native ownership until replaced/destroyed.
    /// <summary>
    /// Creates an immutable in-memory image from tightly packed raw pixels, row zero at v = 0. The
    /// bytes are copied. Sampling, alpha and colorspace come from the node the image is bound to.
    /// </summary>
    /// <param name="width">The width in pixels.</param>
    /// <param name="height">The height in pixels.</param>
    /// <param name="format">The pixel layout.</param>
    /// <param name="pixels">The raw pixel bytes.</param>
    /// <returns>The image identifier.</returns>
    public Image CreateImage(uint width, uint height, ImageFormat format, ReadOnlySpan<byte> pixels)
    {
        Native.ImageConfig config = new() { Size = 32, Width = width, Height = height, Format = (uint)format };
        ulong image;
        fixed (byte* data = pixels) Finish(Native.CreateImage(Handle, ref config, data, (ulong)pixels.Length, out image));
        return new(image);
    }

    /// <summary>
    /// Binds an image to an image or environment texture node. Binding <c>new Image(0)</c> detaches
    /// the in-memory input.
    /// </summary>
    /// <param name="texture">The texture node.</param>
    /// <param name="image">The image to bind.</param>
    public void BindImage(Node texture, Image image) => Finish(Native.BindImage(Handle, texture.Value, image.Value));

    /// <summary>
    /// Releases the caller's image identifier. Existing graph bindings keep their own ownership until
    /// they are replaced or the host is destroyed.
    /// </summary>
    /// <param name="image">The image to release.</param>
    public void ReleaseImage(Image image) => Finish(Native.ReleaseImage(Handle, image.Value));

    /// <summary>
    /// Renders the scene synchronously into scene-linear RGBA floats with a lower-left origin. The
    /// output changes only on success; a failed or cancelled render leaves it untouched.
    /// </summary>
    /// <param name="width">The image width in pixels.</param>
    /// <param name="height">The image height in pixels.</param>
    /// <param name="samples">The number of samples per pixel.</param>
    /// <param name="pixels">The destination, at least <c>width * height * 4</c> floats.</param>
    /// <exception cref="NativeFailure">The render failed or was cancelled (status 4).</exception>
    public void Render(uint width, uint height, uint samples, Span<float> pixels)
    {
        Native.RenderConfig config = new() { Size = 32, Width = width, Height = height, Samples = samples };
        fixed (float* data = pixels) Finish(Native.Render(Handle, ref config, data, (ulong)pixels.Length));
    }

    /// <summary>
    /// Gets a value indicating whether a render is currently running on this host.
    /// </summary>
    public bool IsRendering { get { int s = Native.IsRendering(Handle, out uint active); Finish(s); return active != 0; } }

    /// <summary>
    /// Requests cancellation of the active render. It may be called from another thread.
    /// </summary>
    /// <returns><see langword="true"/> when a render was cancelled; <see langword="false"/> when no render was running.</returns>
    public bool Cancel()
    {
        int status = Native.Cancel(Handle); GC.KeepAlive(lifetime);
        if (status == 5) return false;
        Check(status); return true;
    }

    /// <summary>
    /// Maps a requested backend (including <see cref="Backend.Gpu"/>) to the concrete backend this
    /// native binary creates for it, without probing hardware: Metal for <see cref="Backend.Gpu"/> in
    /// the osx-arm64 (Apple Silicon) build, WebGPU in every other build, osx-x64 (Intel macOS) included.
    /// </summary>
    /// <param name="requested">The requested backend.</param>
    /// <returns>The concrete backend.</returns>
    /// <exception cref="NativeFailure">The backend is not compiled into this binary (status 2).</exception>
    public static Backend ResolveBackend(Backend requested)
    {
        EnsureSupportedOperatingSystem();
        Check(Native.ResolveBackend((uint)requested, out uint resolved));
        return (Backend)resolved;
    }

    /// <summary>
    /// Returns the concrete backend this host renders with (never <see cref="Backend.Gpu"/>). It may be
    /// called while a render runs.
    /// </summary>
    /// <returns>The concrete backend.</returns>
    public Backend GetBackend()
    {
        int status = Native.GetBackend(Handle, out uint backend);
        Finish(status);
        return (Backend)backend;
    }

    /// <summary>
    /// Returns the binary-level capability report as JSON: version, compiled backends, image formats,
    /// pass types, limits and the determinism reference (<c>native/host/ABI.md</c>).
    /// </summary>
    /// <returns>The JSON report.</returns>
    public static string CapabilitiesJson()
    {
        EnsureSupportedOperatingSystem();
        return ReadJson(0, Native.Capabilities, null);
    }

    /// <summary>
    /// Returns this host's capability report as JSON: the binary-level report plus a <c>host</c> object
    /// with the concrete backend, device identity and the device report of <see cref="DeviceReportJson"/>.
    /// </summary>
    /// <returns>The JSON report.</returns>
    public string HostCapabilitiesJson() => ReadJson(Handle, Native.Capabilities, lifetime);

    /// <summary>
    /// Returns the JSON list of the passes captured by the most recent successful render (name, type,
    /// components, written).
    /// </summary>
    /// <returns>The JSON list.</returns>
    /// <exception cref="NativeFailure">No render has succeeded since the last render started (status 5).</exception>
    public string RenderPassesJson() => ReadJson(Handle, Native.RenderPasses, lifetime);

    private delegate int JsonReader(ulong host, byte* json, ulong capacity, out ulong required);

    private static string ReadJson(ulong host, JsonReader reader, SafeHandle keepAlive)
    {
        int status = reader(host, null, 0, out ulong required);
        GC.KeepAlive(keepAlive);
        if (status != 6) Check(status);
        for (;;)
        {
            byte[] result = new byte[checked((int)required)];
            fixed (byte* data = result) status = reader(host, data, (ulong)result.Length, out required);
            GC.KeepAlive(keepAlive);
            if (status == 6) continue;
            Check(status);
            return Encoding.UTF8.GetString(result, 0, checked((int)required - 1));
        }
    }

    /// <summary>
    /// Deletes a node and invalidates its identifier: objects, geometry, lights, particle systems,
    /// passes and shaders created with <see cref="CreateNode"/>, and shader graph nodes (their links are
    /// removed). Deleting a shader also invalidates its graph nodes. Singletons and shader output nodes
    /// cannot be deleted.
    /// </summary>
    /// <param name="node">The node to delete.</param>
    /// <exception cref="NativeFailure">Another node still references the node, or the node cannot be deleted (status 1).</exception>
    public void DeleteNode(Node node) => Finish(Native.DeleteNode(Handle, node.Value));

    /// <summary>
    /// Replaces the pixels behind a live image identifier (size and format may change). Every texture
    /// still bound to it uses the new pixels on the next render; bindings made before
    /// <see cref="ReleaseImage"/> keep the pixels they had.
    /// </summary>
    /// <param name="image">The image identifier.</param>
    /// <param name="width">The width in pixels.</param>
    /// <param name="height">The height in pixels.</param>
    /// <param name="format">The pixel layout.</param>
    /// <param name="pixels">The tightly packed raw pixel bytes; copied before the call returns.</param>
    /// <exception cref="NativeFailure">The identifier was released, or the size, byte count or values are invalid (status 1).</exception>
    public void UpdateImage(Image image, uint width, uint height, ImageFormat format, ReadOnlySpan<byte> pixels)
    {
        Native.ImageConfig config = new() { Size = 32, Width = width, Height = height, Format = (uint)format };
        fixed (byte* data = pixels) Finish(Native.UpdateImage(Handle, image.Value, ref config, data, (ulong)pixels.Length));
    }

    /// <summary>
    /// Sets a mesh's deformation motion: <paramref name="steps"/> (odd, 3 to 129) time-ordered arrays of
    /// x, y, z vertex positions whose center step equals the current positions bit for bit. Pass
    /// <paramref name="steps"/> 1 with no positions to remove it. Configure it before the mesh's first
    /// render: the native host refuses it afterwards (status 5).
    /// </summary>
    /// <param name="mesh">The mesh node.</param>
    /// <param name="steps">The number of time steps.</param>
    /// <param name="positions">The flattened positions, <c>steps * vertices * 3</c> floats.</param>
    /// <exception cref="NativeFailure">The input is malformed (status 1) or the mesh was already rendered (status 5).</exception>
    public void MotionPositions(Node mesh, uint steps, ReadOnlySpan<float> positions)
    {
        fixed (float* xyz = positions) Finish(Native.MotionPositions(Handle, mesh.Value, steps, xyz, (ulong)positions.Length));
    }

    /// <summary>
    /// Registers (or, with a null callback, clears) the host's native render event callback. The
    /// callback runs on a native render thread or the rendering thread, only while a render runs, and
    /// must not call the native library except <c>cbc_host_error</c> and <c>cbc_host_version</c>;
    /// returning nonzero requests cancellation. After this returns the previous callback is never
    /// invoked again. <see cref="CyclesScene"/> manages this callback itself while it renders.
    /// </summary>
    /// <param name="callback">The unmanaged callback, or null.</param>
    /// <param name="userData">The value passed back to the callback.</param>
    public void SetEventCallback(delegate* unmanaged[Cdecl]<nint, Native.Event*, int> callback, nint userData)
        => Finish(Native.SetEventCallback(Handle, callback, userData));

    /// <summary>
    /// Copies one pass captured by the most recent successful render: raw scene-linear float32, lower-left
    /// origin, <paramref name="components"/> floats per pixel. An empty destination is a size query.
    /// </summary>
    /// <param name="name">The pass name (<c>Combined</c> is always present).</param>
    /// <param name="values">The destination.</param>
    /// <param name="required">Receives the float count the pass holds.</param>
    /// <param name="components">Receives the floats per pixel.</param>
    /// <returns><see langword="true"/> when the values were copied; <see langword="false"/> when the destination is too small.</returns>
    /// <exception cref="NativeFailure">No render has succeeded (status 5), the pass is unknown (status 1) or the device did not write it (status 2).</exception>
    public bool PassFloats(string name, Span<float> values, out ulong required, out uint components)
    {
        int status;
        fixed (float* data = values) status = Native.PassFloats(Handle, name, data, (ulong)values.Length, out required, out components);
        GC.KeepAlive(lifetime);
        if (status == 6) return false;
        Check(status);
        return true;
    }
}
