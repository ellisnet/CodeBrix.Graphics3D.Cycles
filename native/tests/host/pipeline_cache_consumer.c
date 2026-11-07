// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// The persistent pipeline cache through the public C ABI; native/tools/pipeline_cache_gate.py
// runs one process per scenario. Creating a WebGPU host compiles the device's control
// pipeline, which is enough to fill, save, reload and report a cache entry.
// Usage: pipeline_cache_test none|off|dir:PATH|record:PATH|record-dir:RECORD:PATH|record-off:PATH cpu|gpu [hosts]
// Prints each host's device report on one line. "none" makes no configuration call.
#include "cycles_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int status, const char *call, int line) {
  if (status == CBC_OK) return;
  char message[2048] = {0}; uint64_t required = 0;
  cbc_host_error(message, sizeof(message), &required);
  fprintf(stderr, "Line %d: %s returned %d: %s\n", line, call, status, message);
  exit(1);
}
#define CHECK(call) check((call), #call, __LINE__)
#define REQUIRE(test) do { if (!(test)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

static void configure(const char *directory, uint32_t mode) {
  CBCPipelineCacheConfig cache = {sizeof(cache), CBC_HOST_ABI, mode, 0, {0}};
  CHECK(cbc_host_pipeline_cache_config(&cache, directory));
}
static void record(const char *directory, uint32_t flags) {
  CBCTierConfig tier = {sizeof(tier), CBC_HOST_ABI, CBC_TIER_AUTO, flags, {0}};
  CHECK(cbc_host_tier_config(&tier, directory));
}
int main(int argc, char **argv) {
  REQUIRE(argc == 3 || argc == 4);
  /* Size, version, reserved, flag and mode validation. */
  CBCPipelineCacheConfig bad = {sizeof(bad), CBC_HOST_ABI, CBC_PIPELINE_CACHE_DEFAULT, 0, {0}};
  REQUIRE(cbc_host_pipeline_cache_config(NULL, NULL) == CBC_INVALID);
  bad.struct_size--; REQUIRE(cbc_host_pipeline_cache_config(&bad, NULL) == CBC_INVALID); bad.struct_size++;
  bad.abi_version++; REQUIRE(cbc_host_pipeline_cache_config(&bad, NULL) == CBC_INVALID); bad.abi_version--;
  bad.reserved[0] = 1; REQUIRE(cbc_host_pipeline_cache_config(&bad, NULL) == CBC_INVALID); bad.reserved[0] = 0;
  bad.flags = 1; REQUIRE(cbc_host_pipeline_cache_config(&bad, NULL) == CBC_INVALID); bad.flags = 0;
  bad.mode = CBC_PIPELINE_CACHE_OFF + 1; REQUIRE(cbc_host_pipeline_cache_config(&bad, NULL) == CBC_INVALID);
  const char *how = argv[1];
  if (strcmp(how, "off") == 0) configure(NULL, CBC_PIPELINE_CACHE_OFF);
  else if (strncmp(how, "dir:", 4) == 0) configure(how + 4, CBC_PIPELINE_CACHE_DEFAULT);
  else if (strncmp(how, "record:", 7) == 0) record(how + 7, 0);
  else if (strncmp(how, "record-off:", 11) == 0) { record(how + 11, 0); configure(NULL, CBC_PIPELINE_CACHE_OFF); }
  else if (strncmp(how, "record-dir:", 11) == 0) {
    char *both = strdup(how + 11), *split = strchr(both, ':');
    REQUIRE(split != NULL); *split = 0;
    record(both, 0); configure(split + 1, CBC_PIPELINE_CACHE_DEFAULT); free(both);
  } else REQUIRE(strcmp(how, "none") == 0);
  const int hosts = argc == 4 ? atoi(argv[3]) : 1;
  for (int i = 0; i < hosts; i++) {
    CBCHostConfig config = {sizeof(config), CBC_HOST_ABI, strcmp(argv[2], "gpu") == 0 ? CBC_HOST_GPU : CBC_HOST_CPU_REFERENCE, 1, {0}};
    CBCHost host = 0; CHECK(cbc_host_create(&config, &host));
    uint64_t required = 0;
    REQUIRE(cbc_host_device_report_json(host, NULL, 0, &required) == CBC_SMALL_BUFFER);
    char *json = malloc(required); REQUIRE(json != NULL);
    CHECK(cbc_host_device_report_json(host, json, required, &required));
    printf("%s\n", json); free(json);
    CHECK(cbc_host_destroy(host));
  }
  return 0;
}
