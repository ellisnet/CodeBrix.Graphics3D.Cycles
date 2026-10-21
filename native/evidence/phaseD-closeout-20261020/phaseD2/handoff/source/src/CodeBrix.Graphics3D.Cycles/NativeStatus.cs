// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// The status code every native host function returns. The values equal the <c>CBC_*</c> status
/// constants of the native host ABI.
/// </summary>
public enum NativeStatus
{
    /// <summary>The call succeeded.</summary>
    Ok = 0,

    /// <summary>An argument, handle, socket name or value was invalid.</summary>
    Invalid = 1,

    /// <summary>The request is not supported by this native build or backend.</summary>
    Unsupported = 2,

    /// <summary>The native operation failed.</summary>
    Failed = 3,

    /// <summary>The render was cancelled.</summary>
    Cancelled = 4,

    /// <summary>The operation is not possible in the current state (for example nothing is rendering, or a host still exists).</summary>
    NotReady = 5,

    /// <summary>The destination buffer was too small; the required size was reported and the destination left unchanged.</summary>
    SmallBuffer = 6
}
