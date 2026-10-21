// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// One pass captured by the most recent successful render, as listed by
/// <see cref="CyclesScene.GetRenderedPasses"/>.
/// </summary>
/// <param name="Name">The pass name (<c>Combined</c>, or the <c>name</c> of a pass node).</param>
/// <param name="Type">The native pass type name (for example <c>combined</c>, <c>depth</c>, <c>normal</c>, <c>aov_value</c>).</param>
/// <param name="Components">The floats per pixel (1, 3 or 4).</param>
/// <param name="Written">Whether the device wrote the pass; a pass that was not written cannot be read.</param>
public readonly record struct RenderPassInfo(string Name, string Type, int Components, bool Written);
