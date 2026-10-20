// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Options for <see cref="CyclesHost.ConfigureTiers"/>. The values equal the
/// <c>CBC_TIER_RECORD_*</c> and <c>CBC_TIER_CANARY</c> constants of the native host ABI.
/// </summary>
[Flags]
public enum PipelineTierFlags : uint
{
    /// <summary>No options.</summary>
    None = 0,

    /// <summary>Disable the persisted device-identity record.</summary>
    RecordOff = 1,

    /// <summary>Read the persisted device-identity record but never write it.</summary>
    RecordReadOnly = 2,

    /// <summary>
    /// Compile one representative module when the device is created; its failure selects the
    /// conservative tier before the scene compiles.
    /// </summary>
    Canary = 4
}
