// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Generated.Scene;

public sealed partial class ObjectNode
{
    /// <summary>
    /// The native node name of the object (not a socket). Cryptomatte's object id is the hash of
    /// this name, so give every object a distinct name to tell objects apart in the cryptomatte passes.
    /// </summary>
    public string Name { get => GetString("name"); set => SetString("name", value); }
}

public sealed partial class ShaderNode
{
    /// <summary>
    /// The native node name of the shader (not a socket). Cryptomatte's material id is the hash of
    /// this name, so give every material a distinct name to tell materials apart in the cryptomatte passes.
    /// </summary>
    public string Name { get => GetString("name"); set => SetString("name", value); }
}
