// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "util/atomic_ops_codebrix.h"
#include <algorithm>
#include <barrier>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef CODEBRIX_TEST_CYCLES_FILM
#  include "util/atomic.h"
#  include "kernel/globals.h"
#  include "kernel/integrator/state.h"
#  define __ATOMIC_PASS_WRITE__
#  include "kernel/film/write.h"
#  include "util/stats.h"
#endif

static void check(bool condition, const char *message)
{
  if (!condition) throw std::runtime_error(message);
}
static std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
int main()
{
  try {
    std::uint32_t word = UINT32_MAX;
    check(atomic_fetch_and_add_uint32(&word, 1) == UINT32_MAX && word == 0, "unsigned wrap/add return");
    check(atomic_fetch_and_sub_uint32(&word, 1) == 0 && word == UINT32_MAX, "unsigned wrap/sub return");
    word = 1;
    check(atomic_fetch_and_or_uint32(&word, 4) == 1 && word == 5, "OR prior value");
    check(atomic_cas_uint32(&word, 4, 9) == 5 && word == 5, "failed integer CAS");
    check(atomic_cas_uint32(&word, 5, 9) == 5 && word == 9, "successful integer CAS");
    std::int32_t signed_word = INT32_MAX;
    check(atomic_fetch_and_add_uint32(&signed_word, 1) == INT32_MAX && signed_word == INT32_MIN, "signed wrap");
    volatile unsigned int u = 4;
    check(atomic_add_and_fetch_u(&u, 3) == 7 && u == 7, "add-and-fetch u");
    volatile std::size_t z = 8;
    check(atomic_add_and_fetch_z(&z, 7) == 15, "add-and-fetch z");
    check(atomic_sub_and_fetch_z(&z, 5) == 10, "sub-and-fetch z");
    check(atomic_fetch_and_update_max_z(&z, 5) == 10 && atomic_load_z(&z) == 10, "max no decrease");
    check(atomic_fetch_and_update_max_z(&z, 12) == 10 && atomic_load_z(&z) == 12, "max increase");
    float f = 2;
    check(atomic_add_and_fetch_fl(&f, .5f) == 2.5f && f == 2.5f, "float new value");
    check(atomic_cas_float(&f, 2, 9) == 2.5f && f == 2.5f, "failed float CAS");
    check(atomic_cas_float(&f, 2.5f, 9) == 2.5f && f == 9, "successful float CAS");
    f = std::bit_cast<float>(0x80000000u);
    check(bits(atomic_cas_float(&f, 0.f, 1.f)) == 0x80000000u && bits(f) == 0x80000000u, "CAS distinguishes signed zero");
    check(bits(atomic_cas_float(&f, std::bit_cast<float>(0x80000000u), 1.f)) == 0x80000000u && f == 1.f, "negative zero CAS succeeds");
    f = std::bit_cast<float>(0x7fc01234u);
    check(bits(atomic_cas_float(&f, std::bit_cast<float>(0x7fc01235u), 1)) == 0x7fc01234u && bits(f) == 0x7fc01234u, "CAS distinguishes NaN payload");
    check(bits(atomic_cas_float(&f, std::bit_cast<float>(0x7fc01234u), 1)) == 0x7fc01234u && f == 1, "NaN representation CAS succeeds");
    f = std::numeric_limits<float>::infinity();
    check(std::isinf(atomic_add_and_fetch_fl(&f, 1)), "infinite add terminates");
    f = std::bit_cast<float>(0x7fc01234u);
    check(std::isnan(atomic_add_and_fetch_fl(&f, 1)), "NaN add terminates");

    constexpr unsigned threads = 8, iterations = 25000, total = threads * iterations;
    std::uint32_t count = 0, cas_count = 0, mask = 0;
    std::size_t sum = 0, maximum = 0;
    float accumulated = 0;
    std::vector<unsigned> visits(total);
    std::barrier start(threads);
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < threads; t++) workers.emplace_back([&, t] {
      start.arrive_and_wait();
      for (unsigned i = 0; i < iterations; i++) {
        const auto index = atomic_fetch_and_add_uint32(&count, 1);
        visits.at(index) = 1;  // Every index must have exactly one owner.
        atomic_add_and_fetch_z(&sum, 1);
        atomic_fetch_and_update_max_z(&maximum, t * iterations + i);
        atomic_fetch_and_or_uint32(&mask, 1u << t);
        atomic_add_and_fetch_fl(&accumulated, 1.f);
        auto previous = std::atomic_ref(cas_count).load();
        for (;;) {
          const auto observed = atomic_cas_uint32(&cas_count, previous, previous + 1);
          if (observed == previous) break;
          previous = observed;
        }
      }
    });
    for (auto &worker : workers) worker.join();
    check(count == total && cas_count == total && sum == total, "concurrent integer updates lost");
    check(accumulated == float(total), "concurrent float updates lost");
    check(maximum == total - 1 && mask == (1u << threads) - 1, "concurrent max/OR");
    check(std::all_of(visits.begin(), visits.end(), [](unsigned n) { return n == 1; }), "fetch values not unique");
#ifdef CODEBRIX_TEST_CYCLES_FILM
    // Exercise the original film writer with atomic passes enabled. Integer-
    // representable contributions make the serial expected film independent
    // of ordering and give a precise lost-update check under contention.
    constexpr unsigned pixels = 256, samples = 100;
    std::vector<float> film(pixels * 4);
    ccl::Stats stats;
    workers.clear();
    for (unsigned t = 0; t < threads; t++) workers.emplace_back([&] {
      for (unsigned sample = 0; sample < samples; sample++) {
        stats.mem_alloc(64);
        for (unsigned pixel = 0; pixel < pixels; pixel++) {
          ccl::film_write_pass_float4(film.data() + pixel * 4,
                                     ccl::make_float4(.125f, .25f, .5f, 1.f));
        }
        stats.mem_free(64);
      }
    });
    for (auto &worker : workers) worker.join();
    const float weights[] = {.125f, .25f, .5f, 1.f};
    for (unsigned i = 0; i < film.size(); i++)
      check(film[i] == weights[i % 4] * (threads * samples), "original CPU film lost concurrent writes");
    check(stats.mem_used == 0 && stats.mem_peak > 0 && stats.mem_peak <= threads * 64,
          "concurrent native memory accounting");
    std::cout << "{\"originalCpuFilm\":true,\"filmFloatChecks\":" << film.size()
              << ",\"filmSamplesPerPixel\":" << threads * samples
              << ",\"memoryAccountingPassed\":true,\"atomicContract\":";
#endif
    std::cout << "{\"passed\":true,\"threads\":" << threads << ",\"updatesPerOperation\":" << total
              << ",\"floatAtomicRefLockFree\":" << std::atomic_ref(accumulated).is_lock_free()
              << ",\"sizeAtomicRefLockFree\":" << std::atomic_ref(sum).is_lock_free() << "}";
#ifdef CODEBRIX_TEST_CYCLES_FILM
    std::cout << '}';
#endif
    std::cout << '\n';
  }
  catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
