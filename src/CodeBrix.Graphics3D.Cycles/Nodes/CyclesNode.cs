// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using CodeBrix.Graphics3D.Cycles.Generated;

namespace CodeBrix.Graphics3D.Cycles.Nodes;

/// <summary>
/// The base class of every generated node class. A node belongs to one <see cref="CyclesScene"/>;
/// its typed properties read and write the native sockets directly (values are copied, nothing is
/// cached), so a change made through the low-level <see cref="CyclesHost"/> is visible here too.
/// </summary>
/// <remarks>
/// The protected accessors are what the generated properties call. Each one maps onto exactly one
/// native getter or setter; reads use the native size-query protocol and retry when another edit
/// resizes the socket between the query and the copy.
/// </remarks>
public abstract class CyclesNode
{
    internal CyclesNode(CyclesScene scene, Node handle)
    {
        Scene = scene;
        Handle = handle;
    }

    /// <summary>Gets the scene that owns the node.</summary>
    public CyclesScene Scene { get; }

    /// <summary>Gets the native node identifier, for use with the low-level <see cref="CyclesHost"/>.</summary>
    public Node Handle { get; }

    /// <summary>Gets the catalogue entry of the node's native type.</summary>
    public NodeTypeInfo TypeInfo => NodeType.Of(GetType());

    /// <summary>
    /// Describes one of the node's sockets as the native host currently sees it.
    /// </summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The socket description.</returns>
    public PropertyInfo Describe(string socket) => Host.Property(Handle, socket);

    /// <summary>
    /// Gets a value indicating whether the node was removed with <see cref="CyclesScene.Remove"/> (or
    /// its shader graph was). A removed node's native identifier is invalid: every typed accessor then
    /// throws <see cref="ObjectDisposedException"/>, and it cannot be assigned to another node.
    /// </summary>
    public bool IsRemoved => removed;

    private volatile bool removed;

    internal void MarkRemoved() => removed = true;

    /// <summary>Returns the native type name and node identifier.</summary>
    /// <returns>A diagnostic description.</returns>
    public override string ToString() => TypeInfo.Name + " #" + Handle.Value;

    private CyclesHost Host
    {
        get
        {
            ObjectDisposedException.ThrowIf(removed, this);
            return Scene.Host;
        }
    }

    private void ReadExact(string socket, Span<float> destination)
    {
        if (!Host.GetFloats(Handle, socket, destination, out ulong required) || required != (ulong)destination.Length)
            throw new InvalidOperationException($"Socket '{socket}' holds {required} floats, not {destination.Length}.");
    }

    private float[] ReadFloats(string socket)
    {
        Host.GetFloats(Handle, socket, Span<float>.Empty, out ulong required);
        for (;;)
        {
            float[] values = new float[checked((int)required)];
            if (Host.GetFloats(Handle, socket, values, out required)) return values.Length == (int)required ? values : values[..(int)required];
        }
    }

    private long[] ReadInts(string socket)
    {
        Host.GetInts(Handle, socket, Span<long>.Empty, out ulong required);
        for (;;)
        {
            long[] values = new long[checked((int)required)];
            if (Host.GetInts(Handle, socket, values, out required)) return values.Length == (int)required ? values : values[..(int)required];
        }
    }

    private ulong[] ReadNodes(string socket)
    {
        Host.GetNodes(Handle, socket, Span<ulong>.Empty, out ulong required);
        for (;;)
        {
            ulong[] values = new ulong[checked((int)required)];
            if (Host.GetNodes(Handle, socket, values, out required)) return values.Length == (int)required ? values : values[..(int)required];
        }
    }

    private string ReadString(string socket, ulong index)
    {
        Host.GetUtf8(Handle, socket, index, Span<byte>.Empty, out ulong required);
        for (;;)
        {
            byte[] utf8 = new byte[checked((int)required)];
            if (Host.GetUtf8(Handle, socket, index, utf8, out required)) return Encoding.UTF8.GetString(utf8, 0, checked((int)required - 1));
        }
    }

    private long ReadInt(string socket)
    {
        Span<long> value = stackalloc long[1];
        if (!Host.GetInts(Handle, socket, value, out ulong required) || required != 1)
            throw new InvalidOperationException($"Socket '{socket}' holds {required} integers, not one.");
        return value[0];
    }

    /// <summary>Reads a float socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The value.</returns>
    protected float GetFloat(string socket)
    {
        Span<float> value = stackalloc float[1];
        ReadExact(socket, value);
        return value[0];
    }

    /// <summary>Writes a float socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value.</param>
    protected void SetFloat(string socket, float value) => Host.SetFloats(Handle, socket, [value]);

    /// <summary>Reads an integer socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The value.</returns>
    protected int GetInt(string socket) => checked((int)ReadInt(socket));

    /// <summary>Writes an integer socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value.</param>
    protected void SetInt(string socket, int value) => Host.SetInts(Handle, socket, [value]);

    /// <summary>Reads a boolean socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The value.</returns>
    protected bool GetBool(string socket) => ReadInt(socket) != 0;

    /// <summary>Writes a boolean socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value.</param>
    protected void SetBool(string socket, bool value) => Host.SetInts(Handle, socket, [value ? 1 : 0]);

    /// <summary>Reads a 32-bit unsigned socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The value.</returns>
    protected uint GetUInt32(string socket) => checked((uint)Host.GetUInt(Handle, socket));

    /// <summary>Writes a 32-bit unsigned socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value.</param>
    protected void SetUInt32(string socket, uint value) => Host.SetUInt(Handle, socket, value);

    /// <summary>Reads a 64-bit unsigned socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The value.</returns>
    protected ulong GetUInt64(string socket) => Host.GetUInt(Handle, socket);

    /// <summary>Writes a 64-bit unsigned socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value.</param>
    protected void SetUInt64(string socket, ulong value) => Host.SetUInt(Handle, socket, value);

    /// <summary>Reads a color, vector, point or normal socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The value.</returns>
    protected Vector3 GetVector3(string socket)
    {
        Span<float> value = stackalloc float[3];
        ReadExact(socket, value);
        return new Vector3(value[0], value[1], value[2]);
    }

    /// <summary>Writes a color, vector, point or normal socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value.</param>
    protected void SetVector3(string socket, Vector3 value) => Host.SetFloats(Handle, socket, [value.X, value.Y, value.Z]);

    /// <summary>Reads a two-component point socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The value.</returns>
    protected Vector2 GetVector2(string socket)
    {
        Span<float> value = stackalloc float[2];
        ReadExact(socket, value);
        return new Vector2(value[0], value[1]);
    }

    /// <summary>Writes a two-component point socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value.</param>
    protected void SetVector2(string socket, Vector2 value) => Host.SetFloats(Handle, socket, [value.X, value.Y]);

    /// <summary>Reads a transform socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The value.</returns>
    protected Transform3x4 GetTransform(string socket)
    {
        Span<float> value = stackalloc float[Transform3x4.FloatCount];
        ReadExact(socket, value);
        return Transform3x4.FromSpan(value);
    }

    /// <summary>Writes a transform socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value.</param>
    protected void SetTransform(string socket, Transform3x4 value)
    {
        Span<float> values = stackalloc float[Transform3x4.FloatCount];
        value.CopyTo(values);
        Host.SetFloats(Handle, socket, values);
    }

    /// <summary>Reads a string socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The value (empty for the native empty string).</returns>
    protected string GetString(string socket) => ReadString(socket, 0);

    /// <summary>Writes a string socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value; <see langword="null"/> writes the empty string.</param>
    protected void SetString(string socket, string value) => Host.SetString(Handle, socket, value ?? string.Empty);

    /// <summary>Reads an enumeration socket.</summary>
    /// <typeparam name="T">The generated enumeration type.</typeparam>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The value.</returns>
    protected T GetEnum<T>(string socket) where T : struct, Enum => (T)Enum.ToObject(typeof(T), ReadInt(socket));

    /// <summary>Writes an enumeration socket by its native value.</summary>
    /// <typeparam name="T">The generated enumeration type.</typeparam>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The value.</param>
    protected void SetEnum<T>(string socket, T value) where T : struct, Enum => Host.SetInts(Handle, socket, [Convert.ToInt64(value)]);

    /// <summary>Reads a float-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>A copy of the values.</returns>
    protected float[] GetFloatArray(string socket) => ReadFloats(socket);

    /// <summary>Writes a float-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The values; <see langword="null"/> writes an empty array.</param>
    protected void SetFloatArray(string socket, float[] values) => Host.SetFloats(Handle, socket, values);

    /// <summary>Reads an integer-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>A copy of the values.</returns>
    protected int[] GetIntArray(string socket) => Array.ConvertAll(ReadInts(socket), value => checked((int)value));

    /// <summary>Writes an integer-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The values; <see langword="null"/> writes an empty array.</param>
    protected void SetIntArray(string socket, int[] values) => Host.SetInts(Handle, socket, Array.ConvertAll(values ?? [], value => (long)value));

    /// <summary>Reads a boolean-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>A copy of the values.</returns>
    protected bool[] GetBoolArray(string socket) => Array.ConvertAll(ReadInts(socket), value => value != 0);

    /// <summary>Writes a boolean-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The values; <see langword="null"/> writes an empty array.</param>
    protected void SetBoolArray(string socket, bool[] values) => Host.SetInts(Handle, socket, Array.ConvertAll(values ?? [], value => value ? 1L : 0L));

    /// <summary>Reads a color-, vector-, point- or normal-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>A copy of the values.</returns>
    protected Vector3[] GetVector3Array(string socket) => MemoryMarshal.Cast<float, Vector3>(ReadFloats(socket)).ToArray();

    /// <summary>Writes a color-, vector-, point- or normal-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The values; <see langword="null"/> writes an empty array.</param>
    protected void SetVector3Array(string socket, Vector3[] values) => Host.SetFloats(Handle, socket, MemoryMarshal.Cast<Vector3, float>(values ?? []));

    /// <summary>Reads a two-component point-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>A copy of the values.</returns>
    protected Vector2[] GetVector2Array(string socket) => MemoryMarshal.Cast<float, Vector2>(ReadFloats(socket)).ToArray();

    /// <summary>Writes a two-component point-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The values; <see langword="null"/> writes an empty array.</param>
    protected void SetVector2Array(string socket, Vector2[] values) => Host.SetFloats(Handle, socket, MemoryMarshal.Cast<Vector2, float>(values ?? []));

    /// <summary>Reads a transform-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>A copy of the values.</returns>
    protected Transform3x4[] GetTransformArray(string socket)
    {
        float[] values = ReadFloats(socket);
        Transform3x4[] result = new Transform3x4[values.Length / Transform3x4.FloatCount];
        for (int i = 0; i < result.Length; ++i) result[i] = Transform3x4.FromSpan(values.AsSpan(i * Transform3x4.FloatCount));
        return result;
    }

    /// <summary>Writes a transform-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The values; <see langword="null"/> writes an empty array.</param>
    protected void SetTransformArray(string socket, Transform3x4[] values)
    {
        values ??= [];
        float[] flat = new float[values.Length * Transform3x4.FloatCount];
        for (int i = 0; i < values.Length; ++i) values[i].CopyTo(flat.AsSpan(i * Transform3x4.FloatCount));
        Host.SetFloats(Handle, socket, flat);
    }

    /// <summary>Reads a string-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <returns>A copy of the values.</returns>
    protected string[] GetStringArray(string socket)
    {
        string[] result = new string[checked((int)Describe(socket).Count)];
        for (int i = 0; i < result.Length; ++i) result[i] = ReadString(socket, (ulong)i);
        return result;
    }

    /// <summary>Writes a string-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The values; <see langword="null"/> writes an empty array.</param>
    protected void SetStringArray(string socket, string[] values) => Host.SetStrings(Handle, socket, values ?? []);

    /// <summary>Reads a node-reference socket.</summary>
    /// <typeparam name="T">The generated class of the referenced node type.</typeparam>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The referenced node, or <see langword="null"/> when the socket is empty.</returns>
    protected T GetNode<T>(string socket) where T : CyclesNode
    {
        ulong[] values = ReadNodes(socket);
        return values.Length == 0 || values[0] == 0 ? null : Scene.Wrap<T>(new Node(values[0]));
    }

    /// <summary>Writes a node-reference socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="value">The referenced node, or <see langword="null"/> to clear the reference.</param>
    protected void SetNode(string socket, CyclesNode value) => Host.SetNodes(Handle, socket, [Scene.HandleOf(value)]);

    /// <summary>Reads a node-array socket.</summary>
    /// <typeparam name="T">The generated class of the referenced node type.</typeparam>
    /// <param name="socket">The native socket name.</param>
    /// <returns>The referenced nodes.</returns>
    protected T[] GetNodeArray<T>(string socket) where T : CyclesNode
        => Array.ConvertAll(ReadNodes(socket), value => Scene.Wrap<T>(new Node(value)));

    /// <summary>Writes a node-array socket.</summary>
    /// <param name="socket">The native socket name.</param>
    /// <param name="values">The referenced nodes; <see langword="null"/> writes an empty array.</param>
    protected void SetNodeArray(string socket, CyclesNode[] values)
        => Host.SetNodes(Handle, socket, Array.ConvertAll(values ?? [], Scene.HandleOf));
}
