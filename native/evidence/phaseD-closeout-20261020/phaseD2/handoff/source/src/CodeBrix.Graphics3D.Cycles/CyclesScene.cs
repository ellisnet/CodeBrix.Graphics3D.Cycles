// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using CodeBrix.Graphics3D.Cycles.Generated;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Lights;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Nodes;
using CodeBrix.Graphics3D.Cycles.Rendering;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// A Cycles scene with a typed object model: the entry point of the idiomatic API. It owns one
/// native scene host (<see cref="Host"/>), hands out the generated node classes, uploads raw pixel
/// images and renders synchronously or asynchronously into raw scene-linear float32 RGBA films.
/// </summary>
/// <remarks>
/// Every member maps onto the native host ABI; nothing is rendered, cached or simulated in managed
/// code. Node properties read and write native sockets directly. Edits serialize with rendering in
/// the native host. Dispose (or await <see cref="DisposeAsync"/>) to cancel any active render and
/// release the native scene; afterwards every member throws <see cref="ObjectDisposedException"/>.
/// </remarks>
public sealed class CyclesScene : IDisposable, IAsyncDisposable
{
    private readonly object sync = new();
    private readonly Dictionary<ulong, CyclesNode> nodes = [];
    private CameraNode camera;
    private FilmNode film;
    private IntegratorNode integrator;
    private BackgroundNode background;
    private Task activeRender = Task.CompletedTask;
    private int disposed;

    /// <summary>
    /// Creates a scene on a backend-neutral device.
    /// </summary>
    /// <param name="backend">The device choice; <see cref="CyclesBackend.Gpu"/> resolves per platform (see <see cref="ResolveBackend"/>).</param>
    /// <param name="threads">The number of native worker threads (CPU reference backend).</param>
    /// <exception cref="PlatformNotSupportedException">The process is not 64-bit, or the operating system is older than the native library supports.</exception>
    /// <exception cref="NativeFailure">The native host could not be created (for example no usable GPU, status 2 or 3).</exception>
    public CyclesScene(CyclesBackend backend, uint threads = 1) : this(ResolveBackend(backend), threads)
    {
    }

    /// <summary>
    /// Creates a scene on an explicit native backend.
    /// </summary>
    /// <param name="backend">The concrete backend.</param>
    /// <param name="threads">The number of native worker threads (CPU reference backend).</param>
    /// <exception cref="PlatformNotSupportedException">The process is not 64-bit, or the operating system is older than the native library supports.</exception>
    /// <exception cref="NativeFailure">The native host could not be created.</exception>
    public CyclesScene(Backend backend, uint threads = 1)
    {
        Host = new CyclesHost(backend, threads);
        Backend = backend;
    }

    /// <summary>
    /// Maps a backend-neutral choice onto the concrete backend for this process: GPU is the native
    /// Metal device in osx-arm64 processes and WebGPU everywhere else.
    /// </summary>
    /// <param name="backend">The backend-neutral choice.</param>
    /// <returns>The concrete backend.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="backend"/> is not a defined value.</exception>
    public static Backend ResolveBackend(CyclesBackend backend) => backend switch
    {
        CyclesBackend.CpuReference => Backend.CpuReference,
        CyclesBackend.Gpu => OperatingSystem.IsMacOS() && RuntimeInformation.ProcessArchitecture == Architecture.Arm64 ? Backend.Metal : Backend.WebGpu,
        _ => throw new ArgumentOutOfRangeException(nameof(backend))
    };

    /// <summary>Gets the low-level, by-name host this scene owns. Edits made through it are visible to the typed nodes.</summary>
    public CyclesHost Host { get; }

    /// <summary>Gets the concrete backend the scene renders with.</summary>
    public Backend Backend { get; }

    /// <summary>
    /// Gets or sets how often <see cref="RenderAsync(RenderSettings, IProgress{RenderProgress}, CancellationToken)"/>
    /// polls the native host and reports progress while a render runs. The default is 50 milliseconds.
    /// </summary>
    public TimeSpan ProgressInterval { get; set; } = TimeSpan.FromMilliseconds(50);

    /// <summary>Gets a value indicating whether a render is running on this scene.</summary>
    public bool IsRendering => Host.IsRendering;

    /// <summary>Gets the scene camera.</summary>
    public CameraNode Camera => ThrowIfDisposed(camera ??= Wrap<CameraNode>(Host.SceneNode("camera")));

    /// <summary>Gets the film settings.</summary>
    public FilmNode Film => ThrowIfDisposed(film ??= Wrap<FilmNode>(Host.SceneNode("film")));

    /// <summary>Gets the integrator settings.</summary>
    public IntegratorNode Integrator => ThrowIfDisposed(integrator ??= Wrap<IntegratorNode>(Host.SceneNode("integrator")));

    /// <summary>Gets the world background (assign a shader graph to its <see cref="BackgroundNode.Shader"/>).</summary>
    public BackgroundNode Background => ThrowIfDisposed(background ??= Wrap<BackgroundNode>(Host.SceneNode("background")));

    /// <summary>
    /// Creates a scene node of a generated type that the native host creates directly
    /// (<see cref="NodeCreation.Scene"/>: objects, geometry, lights, shader graphs, particle systems, passes).
    /// </summary>
    /// <typeparam name="T">The generated node class, for example <see cref="ObjectNode"/> or <see cref="MeshNode"/>.</typeparam>
    /// <returns>The new node.</returns>
    /// <exception cref="ArgumentException"><typeparamref name="T"/> is not created directly in the scene.</exception>
    public T Create<T>() where T : CyclesNode
    {
        NodeTypeInfo info = NodeType.Of(typeof(T));
        if (info == null || info.Creation != NodeCreation.Scene || info.Factory == null)
            throw new ArgumentException($"{typeof(T).Name} is not a scene node type the native host creates; see NodeType.Of(typeof(T)).Creation.");
        return Track((T)info.Factory(this, Host.CreateNode(info.Name), null));
    }

    /// <summary>
    /// Creates a new, empty shader graph (the native <c>shader</c> node). Add nodes to it with
    /// <see cref="ShaderNode.Add{T}"/> and link them with <see cref="ShaderNode.Connect"/>.
    /// </summary>
    /// <returns>The new shader graph.</returns>
    public ShaderNode CreateShader() => Create<ShaderNode>();

    /// <summary>
    /// Creates a light of a generated light type (its geometry); place it in the scene with
    /// <see cref="CreateObject"/>.
    /// </summary>
    /// <typeparam name="T">The light class, for example <see cref="PointLightNode"/> or <see cref="SunLightNode"/>.</typeparam>
    /// <returns>The new light.</returns>
    public T CreateLight<T>() where T : LightNode => Create<T>();

    /// <summary>
    /// Creates a triangle mesh with its geometry and, optionally, the shader every face uses.
    /// </summary>
    /// <param name="positions">Vertex positions as x, y, z triples.</param>
    /// <param name="indices">Triangle vertex indices, three per triangle.</param>
    /// <param name="shader">The shader graph every face uses, or <see langword="null"/> for the scene default.</param>
    /// <returns>The new mesh.</returns>
    /// <exception cref="ArgumentException">Either span length is not a multiple of three.</exception>
    public MeshNode CreateMesh(ReadOnlySpan<float> positions, ReadOnlySpan<uint> indices, ShaderNode shader = null)
    {
        MeshNode mesh = Create<MeshNode>();
        if (shader != null) mesh.UsedShaders = [shader];
        mesh.SetTriangles(positions, indices);
        return mesh;
    }

    /// <summary>
    /// Creates an object that places a geometry (a mesh, other geometry or a light) in the scene.
    /// </summary>
    /// <param name="geometry">The geometry or light the object instances.</param>
    /// <param name="transform">The object-to-world transform, or <see langword="null"/> for the identity.</param>
    /// <returns>The new object.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="geometry"/> is <see langword="null"/>.</exception>
    public ObjectNode CreateObject(GeometryBaseNode geometry, Transform3x4? transform = null)
    {
        ArgumentNullException.ThrowIfNull(geometry);
        ObjectNode instance = Create<ObjectNode>();
        instance.Geometry = geometry;
        if (transform.HasValue) instance.Tfm = transform.Value;
        return instance;
    }

    /// <summary>
    /// Copies caller-supplied raw pixels into a new immutable image. Pixels are tightly packed, row zero
    /// at v = 0, in one of the eight <see cref="ImageFormat"/> layouts; <typeparamref name="T"/> is the
    /// component type (<see cref="float"/>, <see cref="Half"/>, <see cref="byte"/> or <see cref="ushort"/>)
    /// or <see cref="byte"/> for raw bytes. Float and half data are native-endian and must be finite.
    /// Decode image files with CodeBrix.Imaging first; this library reads no image formats.
    /// </summary>
    /// <typeparam name="T">The pixel component type.</typeparam>
    /// <param name="width">The width in pixels.</param>
    /// <param name="height">The height in pixels.</param>
    /// <param name="format">The pixel layout.</param>
    /// <param name="pixels">The pixels; copied before the call returns.</param>
    /// <returns>The image; dispose it once it is bound (bindings keep the pixels).</returns>
    /// <exception cref="ArgumentException"><typeparamref name="T"/> does not match the component size of <paramref name="format"/>.</exception>
    /// <exception cref="NativeFailure">The size, byte count or values are invalid (status 1).</exception>
    public CyclesImage CreateImage<T>(uint width, uint height, ImageFormat format, ReadOnlySpan<T> pixels) where T : unmanaged
    {
        int component = ComponentBytes(format);
        int size = Unsafe.SizeOf<T>();
        if (size != 1 && size != component)
            throw new ArgumentException($"{typeof(T).Name} components do not match the {component}-byte components of {format}.", nameof(pixels));
        Image image = Host.CreateImage(width, height, format, MemoryMarshal.AsBytes(pixels));
        return new CyclesImage(this, image, width, height, format);
    }

    /// <summary>
    /// Returns the size in bytes of one component of a pixel layout.
    /// </summary>
    /// <param name="format">The pixel layout.</param>
    /// <returns>4 for float, 2 for half and unsigned short, 1 for byte layouts.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="format"/> is not a defined value.</exception>
    public static int ComponentBytes(ImageFormat format) => format switch
    {
        ImageFormat.Float or ImageFormat.Float4 => 4,
        ImageFormat.Half or ImageFormat.Half4 or ImageFormat.UShort or ImageFormat.UShort4 => 2,
        ImageFormat.Byte or ImageFormat.Byte4 => 1,
        _ => throw new ArgumentOutOfRangeException(nameof(format))
    };

    /// <summary>
    /// Returns the number of channels of a pixel layout.
    /// </summary>
    /// <param name="format">The pixel layout.</param>
    /// <returns>4 for the four-channel layouts, otherwise 1.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="format"/> is not a defined value.</exception>
    public static int Channels(ImageFormat format) => format switch
    {
        ImageFormat.Float4 or ImageFormat.Half4 or ImageFormat.Byte4 or ImageFormat.UShort4 => 4,
        ImageFormat.Float or ImageFormat.Half or ImageFormat.Byte or ImageFormat.UShort => 1,
        _ => throw new ArgumentOutOfRangeException(nameof(format))
    };

    /// <summary>
    /// Returns the typed node for a native node identifier of this scene: the same object for a node
    /// already handed out, otherwise a new wrapper of the generated class matching its native type.
    /// </summary>
    /// <param name="handle">The native node identifier (for example from <see cref="Host"/>).</param>
    /// <returns>The typed node.</returns>
    /// <exception cref="InvalidOperationException">The node is a shader graph node this scene did not create, so its graph is unknown.</exception>
    public CyclesNode Wrap(Node handle)
    {
        lock (sync)
        {
            if (nodes.TryGetValue(handle.Value, out CyclesNode known)) return known;
        }
        string type = Host.NodeTypeName(handle);
        NodeTypeInfo info = NodeType.Find(type);
        if (info == null || info.Factory == null || info.Category == NodeCategory.Shader)
            throw new InvalidOperationException($"Node {handle.Value} of native type '{type}' cannot be wrapped by this scene.");
        return Track(info.Factory(this, handle, null));
    }

    /// <summary>
    /// Returns the typed node for a native node identifier, as <typeparamref name="T"/>.
    /// </summary>
    /// <typeparam name="T">The expected generated class (or a base class of it).</typeparam>
    /// <param name="handle">The native node identifier.</param>
    /// <returns>The typed node.</returns>
    /// <exception cref="InvalidCastException">The node is not a <typeparamref name="T"/>.</exception>
    public T Wrap<T>(Node handle) where T : CyclesNode
    {
        CyclesNode node = Wrap(handle);
        return node as T ?? throw new InvalidCastException($"{node} is not a {typeof(T).Name}.");
    }

    /// <summary>
    /// Takes a snapshot of the device's capability report (backend, adapter, pipeline tier, warnings).
    /// </summary>
    /// <returns>The report.</returns>
    public ICapabilityReport GetCapabilities() => CapabilityReport.Parse(Host.DeviceReportJson());

    /// <summary>
    /// Renders synchronously into a caller-owned buffer of raw scene-linear float32 RGBA (lower-left
    /// origin). The buffer changes only on success.
    /// </summary>
    /// <param name="settings">The size and samples.</param>
    /// <param name="destination">At least <see cref="RenderSettings.FloatCount"/> floats.</param>
    /// <exception cref="NativeFailure">The render failed, was rejected (status 1) or was cancelled (status 4).</exception>
    public void Render(RenderSettings settings, Span<float> destination) => Host.Render(settings.Width, settings.Height, settings.Samples, destination);

    /// <summary>
    /// Renders synchronously into a new film.
    /// </summary>
    /// <param name="settings">The size and samples.</param>
    /// <returns>The film.</returns>
    /// <exception cref="NativeFailure">The render failed or was cancelled (status 4).</exception>
    public FilmBuffer Render(RenderSettings settings)
    {
        float[] pixels = new float[checked((int)settings.FloatCount)];
        Render(settings, pixels);
        return new FilmBuffer(settings.Width, settings.Height, pixels, RenderPass.Combined);
    }

    /// <summary>
    /// Renders on a thread-pool thread into a new film, reporting progress and honouring cancellation.
    /// </summary>
    /// <param name="settings">The size and samples.</param>
    /// <param name="progress">Receives progress reports, or <see langword="null"/>.</param>
    /// <param name="cancellationToken">Cancels the native render; the task then throws <see cref="OperationCanceledException"/>.</param>
    /// <returns>The film.</returns>
    /// <exception cref="OperationCanceledException"><paramref name="cancellationToken"/> cancelled the render.</exception>
    /// <exception cref="NativeFailure">The render failed.</exception>
    public async Task<FilmBuffer> RenderAsync(RenderSettings settings, IProgress<RenderProgress> progress = null, CancellationToken cancellationToken = default)
    {
        float[] pixels = new float[checked((int)settings.FloatCount)];
        await RenderAsync(settings, pixels, progress, cancellationToken).ConfigureAwait(false);
        return new FilmBuffer(settings.Width, settings.Height, pixels, RenderPass.Combined);
    }

    /// <summary>
    /// Renders on a thread-pool thread into a caller-owned buffer, reporting progress and honouring
    /// cancellation. The buffer changes only on success; do not touch it until the task completes.
    /// </summary>
    /// <remarks>
    /// Cancellation calls the native cancel export as soon as the token fires and again on every
    /// progress poll until the native render stops, so it is prompt even when the token fires before
    /// the native render has started. A render that completes before the cancellation reaches it
    /// returns normally. Progress is polled (see <see cref="RenderProgress"/>).
    /// </remarks>
    /// <param name="settings">The size and samples.</param>
    /// <param name="destination">At least <see cref="RenderSettings.FloatCount"/> floats.</param>
    /// <param name="progress">Receives progress reports, or <see langword="null"/>.</param>
    /// <param name="cancellationToken">Cancels the native render; the task then throws <see cref="OperationCanceledException"/>.</param>
    /// <returns>A task that completes when the film is written.</returns>
    /// <exception cref="OperationCanceledException"><paramref name="cancellationToken"/> cancelled the render.</exception>
    /// <exception cref="NativeFailure">The render failed or was rejected.</exception>
    public async Task RenderAsync(RenderSettings settings, Memory<float> destination, IProgress<RenderProgress> progress = null, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ObjectDisposedException.ThrowIf(Volatile.Read(ref disposed) != 0, this);
        Stopwatch timer = Stopwatch.StartNew();
        progress?.Report(new RenderProgress(RenderStage.Starting, TimeSpan.Zero, settings.Samples, null, null));
        Task render = Task.Run(() => Render(settings, destination.Span), CancellationToken.None);
        lock (sync) activeRender = render;
        try
        {
            using (cancellationToken.Register(static state => ((CyclesScene)state).TryCancel(), this))
            {
                while (!render.IsCompleted)
                {
                    if (cancellationToken.IsCancellationRequested) TryCancel();
                    progress?.Report(new RenderProgress(RenderStage.Rendering, timer.Elapsed, settings.Samples, null, null));
                    await Task.WhenAny(render, Task.Delay(ProgressInterval, CancellationToken.None)).ConfigureAwait(false);
                }
            }
            await render.ConfigureAwait(false);
        }
        catch (NativeFailure failure) when (failure.NativeStatus == NativeStatus.Cancelled)
        {
            progress?.Report(new RenderProgress(RenderStage.Cancelled, timer.Elapsed, settings.Samples, null, null));
            if (cancellationToken.IsCancellationRequested) throw new OperationCanceledException("The render was cancelled.", failure, cancellationToken);
            throw;
        }
        catch (Exception)
        {
            progress?.Report(new RenderProgress(RenderStage.Failed, timer.Elapsed, settings.Samples, null, null));
            throw;
        }
        progress?.Report(new RenderProgress(RenderStage.Completed, timer.Elapsed, settings.Samples, settings.Samples, 1));
    }

    /// <summary>
    /// Requests cancellation of the active render. It may be called from any thread.
    /// </summary>
    /// <returns><see langword="true"/> when a render was cancelled; <see langword="false"/> when none was running.</returns>
    public bool Cancel() => Host.Cancel();

    /// <summary>
    /// Cancels any active render, waits for the native host to finish and destroys it. Calling it more
    /// than once has no further effect.
    /// </summary>
    public void Dispose()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        Host.Dispose();
    }

    /// <summary>
    /// Cancels any active render, waits (without blocking the caller) for the native host and any
    /// <see cref="RenderAsync(RenderSettings, IProgress{RenderProgress}, CancellationToken)"/> task to
    /// finish, and destroys the host.
    /// </summary>
    /// <returns>A task that completes when the scene is released.</returns>
    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        Task render;
        lock (sync) render = activeRender;
        await Task.Run(Host.Dispose).ConfigureAwait(false);
        try
        {
            await render.ConfigureAwait(false);
        }
        catch (Exception)
        {
            // The render was cancelled by the disposal; its outcome belongs to its own awaiter.
        }
    }

    private T ThrowIfDisposed<T>(T value)
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref disposed) != 0, this);
        return value;
    }

    private void TryCancel()
    {
        try
        {
            Host.Cancel();
        }
        catch (ObjectDisposedException)
        {
            // The scene was disposed, which cancels the render itself.
        }
    }

    private T Track<T>(T node) where T : CyclesNode
    {
        lock (sync)
        {
            if (nodes.TryGetValue(node.Handle.Value, out CyclesNode known) && known is T existing) return existing;
            nodes[node.Handle.Value] = node;
            return node;
        }
    }

    internal Node HandleOf(CyclesNode node)
    {
        if (node == null) return default;
        if (!ReferenceEquals(node.Scene, this)) throw new ArgumentException("The node belongs to another scene.", nameof(node));
        return node.Handle;
    }

    internal OutputNode ShaderOutputOf(ShaderNode graph)
    {
        Node handle = Host.ShaderOutput(graph.Handle);
        lock (sync)
        {
            if (nodes.TryGetValue(handle.Value, out CyclesNode known)) return (OutputNode)known;
        }
        return Track(new OutputNode(this, handle, graph));
    }

    internal T AddShaderNode<T>(ShaderNode graph) where T : ShaderGraphNode
    {
        NodeTypeInfo info = NodeType.Of(typeof(T));
        if (info == null || info.Creation != NodeCreation.ShaderGraph || info.Factory == null)
            throw new ArgumentException($"{typeof(T).Name} cannot be added to a shader graph; use ShaderNode.Output for the graph output.");
        return Track((T)info.Factory(this, Host.ShaderNode(graph.Handle, info.Name), graph));
    }

    internal void ConnectShader(ShaderNode graph, ShaderOutput from, ShaderInput to)
    {
        if (from.Node == null || to.Node == null) throw new ArgumentException("Both sockets need a node.");
        if (!ReferenceEquals(from.Node.Graph, graph) || !ReferenceEquals(to.Node.Graph, graph))
            throw new ArgumentException("Both sockets must belong to this shader graph.");
        Host.Connect(from.Node.Handle, from.Socket, to.Node.Handle, to.Socket);
    }

    internal float[] ReadAttribute(GeometryBaseNode geometry, string name, AttributeFlags flags)
    {
        Host.GetAttribute(geometry.Handle, name, flags, Span<float>.Empty, out ulong required);
        for (;;)
        {
            float[] values = new float[checked((int)required)];
            if (Host.GetAttribute(geometry.Handle, name, flags, values, out required)) return values.Length == (int)required ? values : values[..(int)required];
        }
    }

    internal void BindImage(ShaderGraphNode texture, CyclesImage image)
    {
        ArgumentNullException.ThrowIfNull(image);
        if (!ReferenceEquals(image.Scene, this)) throw new ArgumentException("The image belongs to another scene.", nameof(image));
        ObjectDisposedException.ThrowIf(image.IsReleased, image);
        Host.BindImage(texture.Handle, image.Handle);
    }

    internal void UnbindImage(ShaderGraphNode texture) => Host.BindImage(texture.Handle, default);

    internal void ReleaseImage(CyclesImage image)
    {
        if (Volatile.Read(ref disposed) != 0) return;
        Host.ReleaseImage(image.Handle);
    }
}
