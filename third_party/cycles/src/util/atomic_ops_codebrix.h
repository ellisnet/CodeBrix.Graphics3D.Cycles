// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Original implementation from the interfaces used by Cycles' permissively
// licensed call sites and C++20 atomic_ref semantics. No Blender atomic header
// implementation was used. See tools/licensing/ATOMIC-CONTRACT.md.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace codebrix_atomic_detail {
template<class T> inline auto reference(T *address) noexcept
{
  using Value = std::remove_volatile_t<T>;
  static_assert(!std::is_const_v<Value>);
  // Native Cycles objects use their ordinary type alignment. Reject a platform
  // requiring stronger alignment instead of issuing undefined atomic accesses.
  static_assert(std::atomic_ref<Value>::required_alignment <= alignof(Value));
  return std::atomic_ref<Value>(*const_cast<Value *>(address));
}
template<class T> using enable_word = std::enable_if_t<
    std::is_integral_v<std::remove_volatile_t<T>> && sizeof(T) == 4, int>;
}  // namespace codebrix_atomic_detail

// All operations are sequentially consistent. Fetch operations return the
// previous value; add/sub-and-fetch return the new value. Arithmetic on unsigned
// counters wraps modulo their width. Signed atomic fetch arithmetic is defined
// by atomic_ref without ordinary signed-overflow undefined behavior.
template<class T, codebrix_atomic_detail::enable_word<T> = 0>
inline std::remove_volatile_t<T> atomic_fetch_and_add_uint32(
    T *address, std::remove_volatile_t<T> value) noexcept
{
  return codebrix_atomic_detail::reference(address).fetch_add(value);
}
template<class T, codebrix_atomic_detail::enable_word<T> = 0>
inline std::remove_volatile_t<T> atomic_fetch_and_sub_uint32(
    T *address, std::remove_volatile_t<T> value) noexcept
{
  return codebrix_atomic_detail::reference(address).fetch_sub(value);
}
template<class T, codebrix_atomic_detail::enable_word<T> = 0>
inline std::remove_volatile_t<T> atomic_fetch_and_or_uint32(
    T *address, std::remove_volatile_t<T> value) noexcept
{
  return codebrix_atomic_detail::reference(address).fetch_or(value);
}
inline std::uint32_t atomic_cas_uint32(volatile std::uint32_t *address,
                                     std::uint32_t expected,
                                     std::uint32_t desired) noexcept
{
  codebrix_atomic_detail::reference(address).compare_exchange_strong(expected, desired);
  return expected;
}
inline unsigned int atomic_add_and_fetch_u(volatile unsigned int *address,
                                            unsigned int value) noexcept
{
  return codebrix_atomic_detail::reference(address).fetch_add(value) + value;
}
inline std::size_t atomic_add_and_fetch_z(volatile std::size_t *address,
                                         std::size_t value) noexcept
{
  return codebrix_atomic_detail::reference(address).fetch_add(value) + value;
}
inline std::size_t atomic_sub_and_fetch_z(volatile std::size_t *address,
                                         std::size_t value) noexcept
{
  return codebrix_atomic_detail::reference(address).fetch_sub(value) - value;
}
inline std::size_t atomic_load_z(const volatile std::size_t *address) noexcept
{
  // Atomic reads share the same synchronization discipline as atomic writes.
  return codebrix_atomic_detail::reference(const_cast<std::size_t *>(address)).load();
}
inline std::size_t atomic_fetch_and_update_max_z(volatile std::size_t *address,
                                                 std::size_t candidate) noexcept
{
  auto atom = codebrix_atomic_detail::reference(address);
  auto observed = atom.load();
  while (observed < candidate) {
    if (atom.compare_exchange_weak(observed, candidate)) {
      break;
    }
  }
  return observed;
}
inline float atomic_cas_float(volatile float *address, float expected, float desired) noexcept
{
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
  // Compare representations, including signed zero and NaN payloads. There is
  // no integer-pointer alias of the float object and no floating equality test.
  codebrix_atomic_detail::reference(address).compare_exchange_strong(expected, desired);
  return expected;
}
inline float atomic_add_and_fetch_fl(volatile float *address, float value) noexcept
{
  auto atom = codebrix_atomic_detail::reference(address);
  float observed = atom.load();
  for (;;) {
    const float desired = observed + value;
    if (atom.compare_exchange_weak(observed, desired)) {
      return desired;
    }
  }
}
