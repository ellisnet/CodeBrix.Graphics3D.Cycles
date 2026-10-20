// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// An opaque identifier for a scene node (a scene singleton, an object, a geometry, a shader graph
/// or a shader node) owned by one <see cref="CyclesHost"/>. The native registry validates the
/// identifier on every call.
/// </summary>
/// <param name="Value">The native node identifier.</param>
public readonly record struct Node(ulong Value);
