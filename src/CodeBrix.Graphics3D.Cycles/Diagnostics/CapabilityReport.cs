// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Collections.Generic;
using System.Text.Json;

namespace CodeBrix.Graphics3D.Cycles.Diagnostics;

/// <summary>
/// The <see cref="ICapabilityReport"/> parsed from the native capability report
/// (<c>cbc_host_capabilities_json</c>, whose <c>host.deviceReport</c> object is the device report of
/// <c>cbc_host_device_report_json</c>). A bare device report is accepted too; its binary-level members
/// are then empty.
/// </summary>
public sealed class CapabilityReport : ICapabilityReport
{
    private static readonly IReadOnlyList<string> NoStrings = [];

    private CapabilityReport(string json)
    {
        Json = json;
    }

    /// <inheritdoc/>
    public int SchemaVersion { get; private set; }

    /// <inheritdoc/>
    public uint AbiVersion { get; private set; }

    /// <inheritdoc/>
    public string LibraryVersion { get; private set; }

    /// <inheritdoc/>
    public string CyclesRevision { get; private set; }

    /// <inheritdoc/>
    public bool HasDevice => BackendName != null;

    /// <inheritdoc/>
    public string BackendName { get; private set; }

    /// <inheritdoc/>
    public Backend Backend => BackendName switch
    {
        null => Backend.Gpu,
        "webgpu" => Backend.WebGpu,
        "metal" => Backend.Metal,
        _ => Backend.CpuReference
    };

    /// <inheritdoc/>
    public bool IsGpu => HasDevice && Backend != Backend.CpuReference;

    /// <inheritdoc/>
    public string DeviceDescription { get; private set; }

    /// <inheritdoc/>
    public string DeviceId { get; private set; }

    /// <inheritdoc/>
    public string PipelineTier { get; private set; }

    /// <inheritdoc/>
    public IReadOnlyList<string> Warnings { get; private set; } = NoStrings;

    /// <inheritdoc/>
    public IReadOnlyList<string> CompiledBackends { get; private set; } = NoStrings;

    /// <inheritdoc/>
    public string GpuBackendName { get; private set; }

    /// <inheritdoc/>
    public bool GpuFallsBackToCpu { get; private set; }

    /// <inheritdoc/>
    public IReadOnlyDictionary<string, BackendScope> BackendScopes { get; private set; } = new Dictionary<string, BackendScope>();

    /// <inheritdoc/>
    public BackendScope Scope => BackendName != null && BackendScopes.TryGetValue(BackendName, out BackendScope scope) ? scope : null;

    /// <inheritdoc/>
    public IReadOnlyDictionary<string, bool> OptionalLibraries { get; private set; } = new Dictionary<string, bool>();

    /// <inheritdoc/>
    public IReadOnlyList<string> ImageFormats { get; private set; } = NoStrings;

    /// <inheritdoc/>
    public IReadOnlyDictionary<string, int> PassTypes { get; private set; } = new Dictionary<string, int>();

    /// <inheritdoc/>
    public IReadOnlyList<string> Events { get; private set; } = NoStrings;

    /// <inheritdoc/>
    public IReadOnlyDictionary<string, long> Limits { get; private set; } = new Dictionary<string, long>();

    /// <inheritdoc/>
    public string DeterminismReference { get; private set; }

    /// <inheritdoc/>
    public string Json { get; }

    /// <summary>
    /// Parses a native capability report (binary-level or with a host) or a bare device report.
    /// </summary>
    /// <param name="json">The JSON returned by <see cref="CyclesHost.HostCapabilitiesJson"/>,
    /// <see cref="CyclesHost.CapabilitiesJson"/> or <see cref="CyclesHost.DeviceReportJson"/>.</param>
    /// <returns>The parsed report.</returns>
    /// <exception cref="JsonException"><paramref name="json"/> is not a capability or device report.</exception>
    public static CapabilityReport Parse(string json)
    {
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object) throw new JsonException("A capability report is a JSON object.");
        CapabilityReport report = new(json);
        if (root.TryGetProperty("schemaVersion", out JsonElement schema))
        {
            report.SchemaVersion = schema.GetInt32();
            report.ReadBinary(root);
            if (root.TryGetProperty("host", out JsonElement host) && host.ValueKind == JsonValueKind.Object)
            {
                report.BackendName = Text(host, "backend");
                report.DeviceDescription = Text(host, "deviceDescription");
                report.DeviceId = Text(host, "deviceId");
                if (host.TryGetProperty("deviceReport", out JsonElement device) && device.ValueKind == JsonValueKind.Object)
                    report.ReadDevice(device);
            }
        }
        else if (root.TryGetProperty("backend", out JsonElement backend) && backend.ValueKind == JsonValueKind.String)
        {
            report.AbiVersion = root.TryGetProperty("abiVersion", out JsonElement abi) ? abi.GetUInt32() : 0;
            report.BackendName = backend.GetString();
            report.ReadDevice(root);
        }
        else
        {
            throw new JsonException("The JSON is neither a native capability report nor a device report.");
        }
        return report;
    }

    private void ReadBinary(JsonElement root)
    {
        AbiVersion = root.TryGetProperty("abiVersion", out JsonElement abi) ? abi.GetUInt32() : 0;
        LibraryVersion = Text(root, "libraryVersion");
        CyclesRevision = Text(root, "cyclesRevision");
        if (root.TryGetProperty("backends", out JsonElement backends) && backends.ValueKind == JsonValueKind.Object)
        {
            CompiledBackends = Strings(backends, "compiled");
            GpuBackendName = Text(backends, "gpu");
            GpuFallsBackToCpu = backends.TryGetProperty("gpuFallbackToCpu", out JsonElement fallback) && fallback.ValueKind == JsonValueKind.True;
        }
        Dictionary<string, BackendScope> scopes = [];
        if (root.TryGetProperty("backendScope", out JsonElement scopeList) && scopeList.ValueKind == JsonValueKind.Object)
        {
            foreach (JsonProperty entry in scopeList.EnumerateObject())
            {
                JsonElement value = entry.Value;
                if (value.ValueKind != JsonValueKind.Object) continue;
                bool passList = value.TryGetProperty("passes", out JsonElement passes) && passes.ValueKind == JsonValueKind.Array;
                scopes[entry.Name] = new BackendScope(Text(value, "device"), Strings(value, "cameraTypes"), Strings(value, "primitives"),
                    passList ? Strings(value, "passes") : NoStrings, passList ? null : Text(value, "passes"),
                    Strings(value, "renderTimeRejections"), Text(value, "reference"));
            }
        }
        BackendScopes = scopes;
        Dictionary<string, bool> libraries = [];
        if (root.TryGetProperty("optionalLibraries", out JsonElement optional) && optional.ValueKind == JsonValueKind.Object)
            foreach (JsonProperty entry in optional.EnumerateObject()) libraries[entry.Name] = entry.Value.ValueKind == JsonValueKind.True;
        OptionalLibraries = libraries;
        if (root.TryGetProperty("imageInput", out JsonElement images) && images.ValueKind == JsonValueKind.Object) ImageFormats = Strings(images, "formats");
        Dictionary<string, int> passTypes = [];
        if (root.TryGetProperty("passTypes", out JsonElement types) && types.ValueKind == JsonValueKind.Array)
        {
            foreach (JsonElement type in types.EnumerateArray())
            {
                if (type.ValueKind == JsonValueKind.Object && type.TryGetProperty("name", out JsonElement name) && type.TryGetProperty("value", out JsonElement value))
                    passTypes[name.GetString()] = value.GetInt32();
            }
        }
        PassTypes = passTypes;
        Events = Strings(root, "events");
        Dictionary<string, long> limits = [];
        if (root.TryGetProperty("limits", out JsonElement limitList) && limitList.ValueKind == JsonValueKind.Object)
        {
            foreach (JsonProperty entry in limitList.EnumerateObject())
                if (entry.Value.ValueKind == JsonValueKind.Number && entry.Value.TryGetInt64(out long limit)) limits[entry.Name] = limit;
        }
        Limits = limits;
        DeterminismReference = Text(root, "determinism");
    }

    private void ReadDevice(JsonElement device)
    {
        if (AbiVersion == 0 && device.TryGetProperty("abiVersion", out JsonElement abi)) AbiVersion = abi.GetUInt32();
        List<string> warnings = [];
        if (device.TryGetProperty("pipelines", out JsonElement pipelines) && pipelines.ValueKind == JsonValueKind.Object)
        {
            if (pipelines.TryGetProperty("tier", out JsonElement tier) && tier.ValueKind == JsonValueKind.String) PipelineTier = tier.GetString();
            if (pipelines.TryGetProperty("warnings", out JsonElement list) && list.ValueKind == JsonValueKind.Array)
                foreach (JsonElement warning in list.EnumerateArray()) warnings.Add(warning.ValueKind == JsonValueKind.String ? warning.GetString() : warning.GetRawText());
        }
        Warnings = warnings;
    }

    private static string Text(JsonElement element, string name)
        => element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String ? value.GetString() : null;

    private static IReadOnlyList<string> Strings(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Array) return NoStrings;
        List<string> list = [];
        foreach (JsonElement item in value.EnumerateArray()) list.Add(item.ValueKind == JsonValueKind.String ? item.GetString() : item.GetRawText());
        return list;
    }
}
