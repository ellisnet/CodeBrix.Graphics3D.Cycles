// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// The base exception for failures reported by the native Cycles host. Every failing native call
/// made through <see cref="CyclesHost"/> or <see cref="CyclesScene"/> throws a
/// <see cref="NativeFailure"/>, which derives from this type.
/// </summary>
public class CyclesException : Exception
{
    /// <summary>
    /// Creates an exception for a native status and its error text.
    /// </summary>
    /// <param name="status">The native status code.</param>
    /// <param name="message">The native error text.</param>
    public CyclesException(NativeStatus status, string message) : base(message)
    {
        NativeStatus = status;
    }

    /// <summary>
    /// Creates an exception for a native status, its error text and the exception that caused it.
    /// </summary>
    /// <param name="status">The native status code.</param>
    /// <param name="message">The native error text.</param>
    /// <param name="innerException">The exception that caused this one.</param>
    public CyclesException(NativeStatus status, string message, Exception innerException) : base(message, innerException)
    {
        NativeStatus = status;
    }

    /// <summary>
    /// Gets the native status code the failing call returned.
    /// </summary>
    public NativeStatus NativeStatus { get; }
}
