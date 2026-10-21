// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Thrown when a native host call returns a failure status. <see cref="System.Exception.Message"/>
/// carries the native thread-local error text. It is a <see cref="CyclesException"/>, so callers can
/// catch either type.
/// </summary>
/// <param name="status">The native status code (a <c>CBC_*</c> status constant of the native host ABI).</param>
/// <param name="message">The native error text.</param>
public sealed class NativeFailure(int status, string message) : CyclesException((NativeStatus)status, message)
{
    /// <summary>
    /// Gets the native status code as an integer: 1 invalid argument, 2 unsupported, 3 failed,
    /// 4 cancelled, 5 not ready, 6 buffer too small. <see cref="CyclesException.NativeStatus"/>
    /// carries the same value as a <see cref="Cycles.NativeStatus"/>.
    /// </summary>
    public int Status { get; } = status;
}
