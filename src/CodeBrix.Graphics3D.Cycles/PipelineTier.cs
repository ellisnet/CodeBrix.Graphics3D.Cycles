// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// The WebGPU pipeline tier requested through <see cref="CyclesHost.ConfigureTiers"/>, used for
/// graceful degradation on devices that cannot compile the full pipeline set. The values equal the
/// <c>CBC_TIER_*</c> constants of the native host ABI.
/// </summary>
public enum PipelineTier : uint
{
    /// <summary>
    /// Choose automatically: an explicit request, then the persisted device record, then the
    /// known-adapter table, then the full tier.
    /// </summary>
    Auto = 0,

    /// <summary>Always use the full pipeline tier.</summary>
    Full = 1,

    /// <summary>Always use the conservative pipeline tier.</summary>
    Conservative = 2
}
