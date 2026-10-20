# Android native-host application validation

Copyright (c) 2026 Jeremy Ellis and contributors

This SDK-built test application consumes the maintained thin .NET wrapper and
native host on Android ARM64 and x64, with minimum API 33. It tests the explicit
CPU reference backend in a real app installation. Native WebGPU lifecycle tests
are separate; loading the WebGPU library does not prove GPU rendering in the app.

From the repository root, first build both Android RIDs with
`python3 tools/build_android.py --rid android-arm64` and
`python3 tools/build_android.py --rid android-x64`. Generate independent Linux
references with `python3 native/tools/host_gate.py --output native/work/host-validation`.
A failed reference-validation run must be investigated; the app builder requires
a passed reference record and verifies its pinned outputs. Existing validated
reference directories may be reused while their fixture inputs still match.

Build and inspect a new APK with:

```sh
python3 native/tools/build_android_app.py \
  --output native/work/android-app/validation-v1 \
  --references native/work/host-validation
```

The builder audits the current native inputs when `--native-review` is omitted.
An existing matching review directory can be supplied explicitly. The current
recipe requires the installed Linux x64 build tools, pinned NDK 29.0.14206865,
.NET SDK/Android workload and runtime-pack versions recorded in the script,
Java 21 and Android build-tools 36.1.0. NuGet feeds are cleared for this test build.
No renderer or dependency implementation source is fetched.

Run on an explicitly selected attached device after other native tests on that
device have completed:

```sh
python3 native/tools/android_app_gate.py \
  --build-output native/work/android-app/validation-v1 \
  --output native/work/android-app/device-v1 \
  --device tablet,android-arm64,DEVICE_SERIAL
```

Repeat `--device LABEL,RID,SERIAL` for additional devices. Use a new output directory
for each build/run. The runner refuses to replace an already-installed validation
app and force-stops its own installation after collecting output.

The builder generates callable checks from `HostLifecycle/Program.cs` and
`ImageModes/Program.cs`; this directory is a template, not a standalone build.
The original Linux bitwise checks remain unchanged. The app tests registry and
property readback, cancellation/reuse, disposal during rendering, three lifecycle
films and six A/B/C/A/B/C configuration rounds totaling 1,560 image films.
Cross-platform films retain `5e-5 + 2e-4*abs(reference)` practical acceptance and
`1e-7 + 1e-7*abs(reference)` strict diagnostics. Lifecycle films also require
strict agreement. Same-process CPU repeat assertions remain bitwise.

The runner independently compares the saved film bytes and verifies installed
native hashes and app-private loading. Comparison of a serialized maximum-error
diagnostic alone allows one double ULP for Mono JSON formatting; that allowance
does not change either film threshold. Reports retain strict image differences.

Only staged copies lose native debug information; the shipped WebGPU binaries
are unchanged. APK inspection verifies native payloads, reference and notice
assets, runtime-pack provenance, minimum API and ELF segment alignment. These
are local diagnostic APKs, not release packages or proof of 16-KiB-page execution.
