// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Device-tier selection for WebGPU pipelines (graceful degradation).
//
// WebGPU reports adapter identity and hard limits, not shader-compiler time or
// memory, and a vendor compiler crash terminates the process. The pipeline tier
// ("full" or "conservative", pipelines.h) is therefore chosen once per device,
// before any renderer pipeline is compiled, from (highest precedence first):
//   1. an explicit request (host API or CODEBRIX_CYCLES_PIPELINE_TIER);
//   2. the persisted device-identity record of earlier runs on this adapter,
//      driver, runtime and shader source: a full-tier crash breadcrumb or
//      creation failure selects conservative; full-tier compiles without any
//      failure select full;
//   3. the known-adapter table (known_adapters());
//   4. full fidelity.
// An opt-in init canary then compiles one representative module; a canary
// failure selects conservative before the scene compiles. A slow compile never
// changes the tier (the conservative tier is not bit-identical): it only warns
// and marks the session degraded.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ccl {
enum DeviceTierRequest : uint32_t {TIER_REQUEST_AUTO=0,TIER_REQUEST_FULL=1,TIER_REQUEST_CONSERVATIVE=2};
// Process-wide configuration from the host API (cbc_host_tier_config). Applies
// to WebGPU devices created afterwards. Environment overrides for gates:
//   CODEBRIX_CYCLES_PIPELINE_TIER=full|conservative|auto
//   CODEBRIX_CYCLES_DEVICE_RECORD_DIR=<directory>
//   CODEBRIX_CYCLES_DEVICE_RECORD=on|off|ignore|readonly
//   CODEBRIX_CYCLES_PIPELINE_CANARY=1|0
struct DeviceTierConfig {
  uint32_t request=TIER_REQUEST_AUTO;
  bool record_off=false,record_read_only=false,canary=false;
  std::string record_directory;  // Empty: no persisted record.
};
void set_device_tier_config(const DeviceTierConfig &config);
DeviceTierConfig device_tier_config();

struct AdapterIdentity {
  std::string vendor,architecture,device,description;
  uint32_t vendor_id=0,device_id=0;
  bool simulated=false;  // CODEBRIX_CYCLES_TEST_ADAPTER (gates only).
};
// Measured devices. device/driver are substrings of the adapter device name and
// driver description (empty driver: any). budget_ms 0 keeps the default.
struct KnownAdapter {const char *name,*device,*driver,*tier;double budget_ms;const char *evidence;};
const std::vector<KnownAdapter> &known_adapters();
const KnownAdapter *match_known_adapter(const AdapterIdentity &identity);
// Mobile GPU vendors (Qualcomm, ARM, Imagination, Samsung, Apple) or an
// Android build. Unknown mobile adapters start full with a tighter budget.
bool mobile_adapter(const AdapterIdentity &identity);
inline constexpr double unknown_mobile_budget_ms=20000.0;
inline constexpr double canary_default_budget_ms=10000.0;

// FNV-1a 64 over every production WGSL module source (unspecialized).
std::string production_shader_hash();
// Runtime identity from wgpuCodebrixGetBuildInfo (API, wgpu versions, target, patches).
std::string webgpu_runtime_version();

struct DeviceRecordStage {std::string tier,module,entry;uint64_t successes=0,failures=0;double last_ms=0,max_ms=0;};
struct DeviceRecordBreadcrumb {std::string tier,module,entry,kind;int signal=0;int64_t time=0;};
struct DeviceRecordFailure {std::string tier,module,entry,message;};
struct DeviceRecord {
  std::string vendor,architecture,device,description,runtime,shader_hash;
  std::vector<DeviceRecordStage> stages;
  std::vector<DeviceRecordBreadcrumb> breadcrumbs;
  std::vector<DeviceRecordFailure> failures;
};
// Text format "codebrix-cycles-device-record 1" (native/DEVICE.md).
bool parse_device_record(const std::string &text,DeviceRecord &record);
std::string format_device_record(const DeviceRecord &record);

class DeviceTierPolicy {
 public:
  // Chooses the tier; performs no compile. Never throws for record problems
  // (they become warnings).
  void decide(const AdapterIdentity &identity,const std::string &runtime,const std::string &shader_hash);
  // Pending breadcrumb around one pipeline creation (record enabled only).
  void begin_pipeline(const char *module,const char *entry,bool conservative);
  void end_pipeline(bool success,double ms,const std::string &failure);
  // Clears the pending breadcrumb without recording (WGSL module rejection is
  // a source defect, not device capacity, and never changes the tier).
  void abandon_pipeline();
  // Descriptor of the open pending breadcrumb (-1: none) for cbc::Device::pipeline.
  int pending_descriptor() const{return pending_fd;}
  void canary_result(const std::string &result,double ms,const std::string &detail);
  void switch_conservative(const std::string &reason,const std::string &detail);
  std::string report_json() const;

  bool conservative=false;
  std::string reason="default",detail;
  double budget_ms=0;  // 0: keep the declared default.
  std::string budget_source="default";
  std::vector<std::string> warnings,overrides;
  bool canary_enabled=false;
  std::string canary="not-run",canary_detail;double canary_ms=0;
 private:
  AdapterIdentity identity;std::string runtime,shader_hash;
  DeviceTierConfig config;
  const KnownAdapter *table=nullptr;
  std::string record_path,record_mode="none",record_status="none";
  bool record_writable=false;
  DeviceRecord record;
  std::string pending_path;int pending_fd=-1;unsigned pending_serial=0;
  std::string pending_tier,pending_module,pending_entry;
  void persist(const std::function<void(DeviceRecord&)> &apply);
  void consume_pending();
};
}
