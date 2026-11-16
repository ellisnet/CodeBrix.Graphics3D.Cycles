// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// An intermediate film of a render on a scene created with <see cref="HostFlags.ProgressiveResults"/> (provisional),
/// raised through <see cref="CyclesScene.IntermediateFilmReceived"/>: a copy of the Combined film after
/// <see cref="SamplesDone"/> of <see cref="SamplesTotal"/> samples, in the layout of the returned film.
/// </summary>
/// <param name="Film">The film so far (Combined, scene-linear RGBA, rows bottom to top; zero outside a camera border).</param>
/// <param name="SamplesDone">The samples per pixel in the film.</param>
/// <param name="SamplesTotal">The samples per pixel the render was asked for.</param>
/// <param name="Elapsed">The time since the native render started.</param>
public sealed record IntermediateFilm(FilmBuffer Film, uint SamplesDone, uint SamplesTotal, TimeSpan Elapsed);
