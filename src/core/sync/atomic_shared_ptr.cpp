// src/core/sync/atomic_shared_ptr.cpp — out-of-line definition of the type-
// erased fallback shard lock (NFR-017 / feature 046).
//
// This is the ONLY project translation unit that names std::mutex for the
// atomic_shared_ptr primitive. By keeping the 128-mutex shard table + the lock
// acquisition here (not in the header), fixpp/core/sync/detail/
// atomic_shared_ptr.hpp stays free of any std::mutex token on EVERY standard
// library, so the two awaitable consumers that include it (async_mutex.hpp,
// engine.hpp) never breach Article XI §3 / XV §9 — with no constitutional
// amendment.
//
// ALWAYS compiled into fixpp_sync on every mode (always-ship-guard, research
// D-9 / contract CT-4): on the native path the alias never constructs a
// shard_guard, so these symbols are present-but-never-called (the
// function-local-static shard table's lazy-init never fires) → zero overhead.

#include "fixpp/core/sync/detail/atomic_shared_ptr.hpp"

#include <array>
#include <cstdint>
#include <mutex>

namespace fixpp::sync::detail {
namespace {

constexpr std::size_t kShardCount = 128;

// One shared global table for all atomic_shared_ptr instances. Function-local
// static: lazily initialized on first lock (never on the native path).
std::array<std::mutex, kShardCount>& shard_table() noexcept {
  static std::array<std::mutex, kShardCount> table{};
  return table;
}

// Hash the object's address with high-bit mixing before sharding (murmur-style
// finalizer) — reduces collisions for aligned addresses vs low-bit masking.
// Unchanged from the locked harness; now computed here in the .cpp.
std::size_t shard_index_for(const void* self) noexcept {
  std::uint64_t x =
      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(self));
  x ^= (x >> 33U);
  x *= 0xff51afd7ed558ccdULL;
  x ^= (x >> 33U);
  return static_cast<std::size_t>(x) & (kShardCount - 1U);
}

}  // namespace

// noexcept by design: a throw from std::mutex::lock() std::terminate()s here,
// matching std::atomic's no-throw operation contract (spec Clarifications
// 2026-06-21 / contract C-5). A shard-lock failure is unrecoverable resource
// exhaustion; proceeding without the lock would be a data race.
shard_guard::shard_guard(const void* self) noexcept
    : shard_index_(shard_index_for(self)) {
  shard_table()[shard_index_].lock();
}

shard_guard::~shard_guard() { shard_table()[shard_index_].unlock(); }

}  // namespace fixpp::sync::detail
