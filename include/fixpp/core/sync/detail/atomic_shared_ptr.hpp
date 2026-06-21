#ifndef FIXPP_CORE_SYNC_DETAIL_ATOMIC_SHARED_PTR_HPP
#define FIXPP_CORE_SYNC_DETAIL_ATOMIC_SHARED_PTR_HPP

// fixpp::sync::atomic_shared_ptr<T> — libc++ portability fallback (NFR-017 /
// feature 046). Algorithm integrated from the locked research harness (18/18),
// with ONE adaptation: the fallback lock is TYPE-ERASED out of this header into
// src/core/sync/atomic_shared_ptr.cpp behind detail::shard_guard, so this
// header carries NO `std::mutex` token (and no <mutex>) on EITHER standard
// library. That keeps the awaitable consumers (async_mutex.hpp, engine.hpp)
// compliant with Article XI §3 / XV §9 with no constitutional amendment.
//
// \internal — physically installed but excluded from the supported API /
// Doxygen stability surface ([arch §9.1]); not new public API (FR-015).

#include <atomic>    // std::memory_order
#include <cstddef>   // std::size_t
#include <memory>    // std::shared_ptr
#include <utility>   // std::move

#include "fixpp/core/sync/detail/atomic_shared_ptr_detect.hpp"

#if FIXPP_HAS_STD_ATOMIC_SHARED_PTR == 1 && \
    !defined(FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK)
#define FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE 1
#else
#define FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE 0
#endif

namespace fixpp::sync::detail {

// Type-erased shard lock. Declared UNCONDITIONALLY (so the always-compiled
// src/core/sync/atomic_shared_ptr.cpp definition always matches a declaration —
// "always-ship-guard", research D-9 / contract CT-4). The 128 std::mutex shard
// table + the address-hash + the lock/unlock bodies live in the .cpp; this
// header sees ONLY a plain std::size_t member (never a std::mutex / unique_lock
// token). On the native path the alias never constructs a shard_guard, so these
// always-shipped symbols are present-but-never-called (zero overhead, SC-005).
//
// The ctor is noexcept by design: a throw from std::mutex::lock() in the .cpp
// std::terminate()s, matching std::atomic's no-throw operation contract — a
// shard-lock failure is unrecoverable and proceeding without the lock would be
// a data race (spec Clarifications 2026-06-21 / contract C-5).
class shard_guard {
 public:
  explicit shard_guard(const void* self) noexcept;
  ~shard_guard();

  shard_guard(const shard_guard&) = delete;
  shard_guard& operator=(const shard_guard&) = delete;
  shard_guard(shard_guard&&) = delete;
  shard_guard& operator=(shard_guard&&) = delete;

 private:
  std::size_t shard_index_;
};

}  // namespace fixpp::sync::detail

namespace fixpp::sync {

#if FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE

template <class T>
using atomic_shared_ptr = std::atomic<std::shared_ptr<T>>;

#else

template <class T>
class atomic_shared_ptr {
 public:
  using value_type = std::shared_ptr<T>;
  static constexpr bool is_always_lock_free = false;

  atomic_shared_ptr() noexcept = default;
  constexpr atomic_shared_ptr(std::nullptr_t) noexcept : value_(nullptr) {}

  // Implicit by design — mirrors std::atomic<std::shared_ptr<T>> so the
  // fallback is a true drop-in for the native alias.
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  atomic_shared_ptr(value_type desired) noexcept : value_(std::move(desired)) {}

  atomic_shared_ptr(const atomic_shared_ptr&) = delete;
  atomic_shared_ptr& operator=(const atomic_shared_ptr&) = delete;

  // void return + implicit conversion mirror std::atomic's operator= and
  // operator T; required for drop-in parity (locked harness design).
  // NOLINTNEXTLINE(misc-unconventional-assign-operator,cppcoreguidelines-c-copy-assignment-signature)
  void operator=(value_type desired) noexcept {
    store(std::move(desired), std::memory_order_seq_cst);
  }

  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  operator value_type() const noexcept { return load(std::memory_order_seq_cst); }

  bool is_lock_free() const noexcept { return false; }

  // wait/notify_one/notify_all are intentionally omitted in this v1 fallback.
  // Mutex synchronization provides at least acquire/release semantics for all
  // operations; stronger memory_order values are accepted for API compatibility.
  void store(value_type desired,
             std::memory_order = std::memory_order_seq_cst) noexcept {
    detail::shard_guard guard(this);
    value_ = std::move(desired);
  }

  value_type load(std::memory_order = std::memory_order_seq_cst) const noexcept {
    detail::shard_guard guard(this);
    return value_;
  }

  value_type exchange(
      value_type desired,
      std::memory_order = std::memory_order_seq_cst) noexcept {
    detail::shard_guard guard(this);
    value_.swap(desired);
    return desired;
  }

  bool compare_exchange_weak(value_type& expected, value_type desired,
                             std::memory_order success,
                             std::memory_order failure) noexcept {
    return compare_exchange_strong(expected, std::move(desired), success, failure);
  }

  bool compare_exchange_weak(
      value_type& expected, value_type desired,
      std::memory_order order = std::memory_order_seq_cst) noexcept {
    return compare_exchange_weak(expected, std::move(desired), order,
                                 failure_order(order));
  }

  bool compare_exchange_strong(value_type& expected, value_type desired,
                               std::memory_order /*success*/,
                               std::memory_order /*failure*/) noexcept {
    detail::shard_guard guard(this);
    if (equivalent(expected, value_)) {
      value_ = std::move(desired);
      return true;
    }
    expected = value_;
    return false;
  }

  bool compare_exchange_strong(
      value_type& expected, value_type desired,
      std::memory_order order = std::memory_order_seq_cst) noexcept {
    return compare_exchange_strong(expected, std::move(desired), order,
                                   failure_order(order));
  }

 private:
  static bool same_owner(const value_type& a, const value_type& b) noexcept {
    return !a.owner_before(b) && !b.owner_before(a);
  }

  static bool equivalent(const value_type& lhs, const value_type& rhs) noexcept {
    return lhs.get() == rhs.get() && same_owner(lhs, rhs);
  }

  static std::memory_order failure_order(std::memory_order order) noexcept {
    switch (order) {
      case std::memory_order_release:
        return std::memory_order_relaxed;
      case std::memory_order_acq_rel:
        return std::memory_order_acquire;
      default:
        return order;
    }
  }

  mutable value_type value_{};
};

#endif

}  // namespace fixpp::sync

#endif  // FIXPP_CORE_SYNC_DETAIL_ATOMIC_SHARED_PTR_HPP
