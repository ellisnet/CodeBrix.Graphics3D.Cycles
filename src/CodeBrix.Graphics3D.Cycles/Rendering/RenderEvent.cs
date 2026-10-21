// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// One native render event, raised through <see cref="CyclesScene.RenderEventReceived"/>.
/// </summary>
/// <remarks>
/// Events are raised on a native render thread or on the thread that runs the render, only while a
/// render runs, and never concurrently with each other for one scene. A handler must return quickly
/// and must not call any member of the scene, its host or its nodes: the native host refuses every
/// re-entrant call (status 5). Cancel through the render's <see cref="System.Threading.CancellationToken"/>
/// instead.
/// </remarks>
/// <param name="Kind">The event kind.</param>
/// <param name="SamplesDone">The completed samples per pixel.</param>
/// <param name="SamplesTotal">The requested samples per pixel.</param>
/// <param name="Elapsed">The time since the native render started.</param>
/// <param name="Message">The status or error text (empty for progress events without text).</param>
public readonly record struct RenderEvent(RenderEventKind Kind, ulong SamplesDone, ulong SamplesTotal, TimeSpan Elapsed, string Message);
