// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// An opaque identifier for an immutable in-memory image created by
/// <see cref="CyclesHost.CreateImage"/> and owned by one <see cref="CyclesHost"/>.
/// </summary>
/// <param name="Value">The native image identifier.</param>
public readonly record struct Image(ulong Value);
