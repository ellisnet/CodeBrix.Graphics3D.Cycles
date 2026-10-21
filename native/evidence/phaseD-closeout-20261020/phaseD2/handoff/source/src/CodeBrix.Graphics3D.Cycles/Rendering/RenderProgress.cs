// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// A progress report from <see cref="CyclesScene.RenderAsync(RenderSettings, IProgress{RenderProgress}, System.Threading.CancellationToken)"/>.
/// </summary>
/// <remarks>
/// The native host does not yet report per-sample progress, so <see cref="Fraction"/> and
/// <see cref="CompletedSamples"/> are <see langword="null"/> and reports arrive at the scene's
/// <see cref="CyclesScene.ProgressInterval"/> while the render runs (the wrapper polls the native
/// rendering state). When the native host gains progress callbacks those fields are filled from them.
/// </remarks>
/// <param name="Stage">The render stage.</param>
/// <param name="Elapsed">The time since the render was submitted.</param>
/// <param name="TotalSamples">The requested samples per pixel.</param>
/// <param name="CompletedSamples">The samples completed so far, when the native host reports them; otherwise <see langword="null"/>.</param>
/// <param name="Fraction">The completed fraction (0 to 1), when the native host reports it; otherwise <see langword="null"/>.</param>
public readonly record struct RenderProgress(RenderStage Stage, TimeSpan Elapsed, uint TotalSamples, uint? CompletedSamples, double? Fraction);
