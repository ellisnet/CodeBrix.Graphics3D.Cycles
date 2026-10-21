// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// The kind of a native render event (<see cref="RenderEvent"/>). The values equal the native
/// <c>CBC_EVENT_*</c> constants.
/// </summary>
public enum RenderEventKind
{
    /// <summary>The render status text changed (for example scene update or kernel loading).</summary>
    Status = 1,

    /// <summary>The completed sample counter changed.</summary>
    Progress = 2,

    /// <summary>The render is failing; <see cref="RenderEvent.Message"/> holds the native error.</summary>
    Error = 3
}
