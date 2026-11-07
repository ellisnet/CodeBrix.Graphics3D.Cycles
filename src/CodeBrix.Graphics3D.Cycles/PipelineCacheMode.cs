// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// The switch of the persistent GPU pipeline cache, set through <see cref="CyclesHost.ConfigurePipelineCache"/>.
/// The values equal the <c>CBC_PIPELINE_CACHE_*</c> constants of the native host ABI.
/// </summary>
public enum PipelineCacheMode : uint
{
    /// <summary>
    /// Cache compiled pipelines: in the directory given, else in the record directory of
    /// <see cref="CyclesHost.ConfigureTiers"/>, else in the user's cache directory on Linux, else not at all.
    /// </summary>
    Default = 0,

    /// <summary>Do not cache: every process compiles the pipelines it needs.</summary>
    Off = 1
}
