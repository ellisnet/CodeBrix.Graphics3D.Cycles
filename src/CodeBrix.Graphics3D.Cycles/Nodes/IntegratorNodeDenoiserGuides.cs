// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using CodeBrix.Graphics3D.Cycles.Rendering;

namespace CodeBrix.Graphics3D.Cycles.Generated.Scene;

public sealed partial class IntegratorNode
{
    /// <summary>
    /// Gets or sets the guide passes the denoiser uses: the <c>denoiser_passes</c> socket (<see cref="DenoiserPasses"/>)
    /// as flags. Default <see cref="Rendering.DenoiserGuides.Albedo"/> | <see cref="Rendering.DenoiserGuides.Normal"/>.
    /// </summary>
    public DenoiserGuides DenoiserGuides { get => (DenoiserGuides)DenoiserPasses; set => DenoiserPasses = (int)value; }
}
