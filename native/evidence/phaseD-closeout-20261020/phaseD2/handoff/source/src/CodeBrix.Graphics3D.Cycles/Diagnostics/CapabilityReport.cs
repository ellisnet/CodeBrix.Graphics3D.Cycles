// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Collections.Generic;
using System.Text.Json;

namespace CodeBrix.Graphics3D.Cycles.Diagnostics;

/// <summary>
/// The <see cref="ICapabilityReport"/> parsed from the native device report
/// (<c>cbc_host_device_report_json</c>).
/// </summary>
public sealed class CapabilityReport : ICapabilityReport
{
    // Extension point: when the native host exports its JSON capability report (feature support,
    // adapter limits), its fields are parsed here as additional members; existing members keep
    // their meaning.

    private CapabilityReport(uint abiVersion, string backendName, string pipelineTier, IReadOnlyList<string> warnings, string json)
    {
        AbiVersion = abiVersion;
        BackendName = backendName;
        PipelineTier = pipelineTier;
        Warnings = warnings;
        Json = json;
    }

    /// <inheritdoc/>
    public uint AbiVersion { get; }

    /// <inheritdoc/>
    public string BackendName { get; }

    /// <inheritdoc/>
    public Backend Backend => BackendName switch
    {
        "webgpu" => Backend.WebGpu,
        "metal" => Backend.Metal,
        _ => Backend.CpuReference
    };

    /// <inheritdoc/>
    public bool IsGpu => Backend != Backend.CpuReference;

    /// <inheritdoc/>
    public string PipelineTier { get; }

    /// <inheritdoc/>
    public IReadOnlyList<string> Warnings { get; }

    /// <inheritdoc/>
    public string Json { get; }

    /// <summary>
    /// Parses a native device report.
    /// </summary>
    /// <param name="json">The JSON returned by <see cref="CyclesHost.DeviceReportJson"/>.</param>
    /// <returns>The parsed report.</returns>
    /// <exception cref="JsonException"><paramref name="json"/> is not a device report.</exception>
    public static CapabilityReport Parse(string json)
    {
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement root = document.RootElement;
        uint abi = root.TryGetProperty("abiVersion", out JsonElement abiElement) ? abiElement.GetUInt32() : 0;
        string backend = root.GetProperty("backend").GetString();
        string tier = null;
        List<string> warnings = [];
        if (root.TryGetProperty("pipelines", out JsonElement pipelines) && pipelines.ValueKind == JsonValueKind.Object)
        {
            if (pipelines.TryGetProperty("tier", out JsonElement tierElement) && tierElement.ValueKind == JsonValueKind.String) tier = tierElement.GetString();
            if (pipelines.TryGetProperty("warnings", out JsonElement list) && list.ValueKind == JsonValueKind.Array)
                foreach (JsonElement warning in list.EnumerateArray()) warnings.Add(warning.ValueKind == JsonValueKind.String ? warning.GetString() : warning.GetRawText());
        }
        return new CapabilityReport(abi, backend, tier, warnings, json);
    }
}
