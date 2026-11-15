// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Options fixed when a host is created (provisional; the native <c>CBC_HOST_*</c> flags of <c>CBCHostConfig.flags</c>).
/// The values equal the native constants.
/// </summary>
[Flags]
public enum HostFlags : uint
{
    /// <summary>No option: Cycles' headless scheduling, and a render returns its film only when it completes.</summary>
    None = 0,

    /// <summary>
    /// Progressive results (<c>CBC_HOST_PROGRESSIVE_RESULTS</c>): the host's renders schedule Cycles' display updates and
    /// deliver each intermediate film (<see cref="CyclesScene.IntermediateFilmReceived"/>). The WebGPU backend refuses a
    /// render of such a host.
    /// </summary>
    ProgressiveResults = 1,
}
