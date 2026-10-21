// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// A progress report from <see cref="CyclesScene.RenderAsync(RenderSettings, IProgress{RenderProgress}, System.Threading.CancellationToken)"/>.
/// </summary>
/// <remarks>
/// <see cref="RenderStage.Starting"/> is reported when the render is submitted, one
/// <see cref="RenderStage.Rendering"/> report follows every native progress event (the completed
/// sample counter changed) with <see cref="CompletedSamples"/> and <see cref="Fraction"/> filled from
/// it, and a final <see cref="RenderStage.Completed"/>, <see cref="RenderStage.Cancelled"/> or
/// <see cref="RenderStage.Failed"/> report ends the render. The fields are <see langword="null"/> while
/// the native host has reported no sample count. Reports raised from native events are delivered on a
/// native render thread unless the <see cref="IProgress{T}"/> implementation marshals them (as
/// <see cref="Progress{T}"/> does).
/// </remarks>
/// <param name="Stage">The render stage.</param>
/// <param name="Elapsed">The time since the render was submitted.</param>
/// <param name="TotalSamples">The requested samples per pixel.</param>
/// <param name="CompletedSamples">The samples completed so far, when the native host has reported them; otherwise <see langword="null"/>.</param>
/// <param name="Fraction">The completed fraction (0 to 1), when the native host has reported it; otherwise <see langword="null"/>.</param>
public readonly record struct RenderProgress(RenderStage Stage, TimeSpan Elapsed, uint TotalSamples, uint? CompletedSamples, double? Fraction);
