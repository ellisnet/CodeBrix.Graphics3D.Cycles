// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Describes one node socket, as returned by <see cref="CyclesHost.Property"/>.
/// </summary>
/// <param name="ValueType">The stable value kind (a <c>CBC_VALUE_*</c> constant of the native host ABI).</param>
/// <param name="Flags">The property flags (a combination of the <c>CBC_PROPERTY_*</c> constants: array, linkable, internal, readable, writable).</param>
/// <param name="Components">The number of flattened components per element (for example 3 for a vector, 12 for a transform).</param>
/// <param name="Count">The current number of elements stored in the socket.</param>
public readonly record struct PropertyInfo(uint ValueType, uint Flags, uint Components, ulong Count)
{
    /// <summary>
    /// Gets <see cref="ValueType"/> as a <see cref="ValueKind"/>.
    /// </summary>
    public ValueKind Kind => (ValueKind)ValueType;

    /// <summary>
    /// Gets <see cref="Flags"/> as <see cref="Cycles.PropertyFlags"/>.
    /// </summary>
    public PropertyFlags PropertyFlags => (PropertyFlags)Flags;
}
