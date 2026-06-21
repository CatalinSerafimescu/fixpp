// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_atomic_shared_ptr_concurrency.cpp — 046-atomic-shared-ptr NFR-017
//
// Multi-threaded obligations for fixpp::sync::atomic_shared_ptr<T>.
// These are the TSan/ASan load-bearing tests. Rows ported from the locked
// research harness (18/18):
//   - AtomicSharedPtrPublishAcquireOrdering  (row 6)
//   - AtomicSharedPtrRefcountIntegrity       (row 3)
//   - AtomicSharedPtrContentionStress        (row 4)
//   - AtomicSharedPtrLinearizability         (row 7a)
//   - AtomicSharedPtrAllocatorPressure       (row 7b)
//   - AtomicSharedPtrManyInstancesIsolation  (row 10)
//   - AtomicSharedPtrRandomizedStress        (row 10)
//
// ANTI-HANG: all loops are bounded by a fixed iteration count (default 2000),
// readable from the FIXPP_ASP_STRESS_ITERS environment variable. The wall-time
// sleep_for() model from the harness is replaced with iteration-bounded loops so
// tests always terminate quickly in CI. The publish-acquire test uses a fixed
// iteration count (20000) because correctness depends on that quantity.
//
// Anchor: specs/046-atomic-shared-ptr/spec.md FR-001..FR-015 / plan.md test plan.

#include <gtest/gtest.h>

#include <fixpp/core/sync/detail/atomic_shared_ptr.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

using AtomicIntPtr = fixpp::sync::atomic_shared_ptr<int>;
using Clock = std::chrono::steady_clock;

// ── Helper: read a positive int from an env var or return fallback ─────────────

static int read_env_int(const char* key, int fallback) {
  const char* val = std::getenv(key);
  if (val == nullptr || *val == '\0') {
    return fallback;
  }
  try {
    int parsed = std::stoi(val);
    return parsed > 0 ? parsed : fallback;
  } catch (...) {
    return fallback;
  }
}

// ── Helper: shared-ownership equivalence (mirrors harness) ───────────────────

static bool same_owner(const std::shared_ptr<int>& a,
                       const std::shared_ptr<int>& b) noexcept {
  return !a.owner_before(b) && !b.owner_before(a);
}

static bool equivalent(const std::shared_ptr<int>& a,
                        const std::shared_ptr<int>& b) noexcept {
  return a.get() == b.get() && same_owner(a, b);
}

// ── Row 6: publish-acquire ordering ──────────────────────────────────────────
//
// Writer stores Payloads with a checksum; readers verify no torn/partial reads.
// Uses a fixed iteration count (not sleep) — writer runs exactly kIterations
// stores; readers spin until writer signals done. Bounded by kIterations.

TEST(AtomicSharedPtrPublishAcquireOrdering, WriterReaderNeverSeesTornPayload) {
  struct Payload {
    int a;
    int b;
    int c;
    int checksum;
  };

  fixpp::sync::atomic_shared_ptr<Payload> ptr;
  std::atomic<bool> stop{false};
  std::atomic<bool> observed_broken{false};
  // Gate B #4: count validated non-null reader observations so the test fails if
  // the writer raced ahead and finished before any reader ever loaded (a
  // stimulus-guarded no-op). A barrier (readers_ready + go) also forces all
  // readers into their loop BEFORE the writer publishes.
  std::atomic<int> valid_reads{0};
  std::atomic<int> readers_ready{0};
  std::atomic<bool> go{false};

  const int kReaderCount = 4;
  const int kIterations = 20000;

  // Seed a valid payload so a reader that wins the very first scheduling slot
  // still observes a consistent (non-null) value to count.
  {
    auto seed = std::make_shared<Payload>();
    seed->a = 0; seed->b = 0; seed->c = 0; seed->checksum = 0;
    ptr.store(seed, std::memory_order_release);
  }

  std::vector<std::thread> readers;
  readers.reserve(kReaderCount);
  for (int t = 0; t < kReaderCount; ++t) {
    readers.emplace_back([&]() {
      readers_ready.fetch_add(1, std::memory_order_relaxed);
      while (!go.load(std::memory_order_acquire)) { /* spin to the barrier */ }
      while (!stop.load(std::memory_order_relaxed)) {
        auto snapshot = ptr.load(std::memory_order_acquire);
        if (!snapshot) {
          continue;
        }
        if (snapshot->checksum != (snapshot->a + snapshot->b + snapshot->c)) {
          observed_broken.store(true, std::memory_order_relaxed);
          stop.store(true, std::memory_order_relaxed);
          return;
        }
        valid_reads.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  // Barrier: do not start publishing until every reader is in its loop.
  while (readers_ready.load(std::memory_order_relaxed) < kReaderCount) { /* spin */ }
  go.store(true, std::memory_order_release);

  std::thread writer([&]() {
    for (int i = 1; i <= kIterations && !stop.load(std::memory_order_relaxed); ++i) {
      auto payload = std::make_shared<Payload>();
      payload->a = i;
      payload->b = i * 3;
      payload->c = i ^ 0x55AA55AA;
      payload->checksum = payload->a + payload->b + payload->c;
      ptr.store(payload, std::memory_order_release);
    }
    stop.store(true, std::memory_order_relaxed);
  });

  writer.join();
  for (auto& thread : readers) {
    thread.join();
  }

  EXPECT_FALSE(observed_broken.load(std::memory_order_relaxed))
      << "A torn/partial Payload was observed — store/load ordering is broken";
  EXPECT_GT(valid_reads.load(std::memory_order_relaxed), 0)
      << "No reader ever observed a published value — the publish/acquire "
         "stimulus did not actually overlap a reader (Gate B #4).";
}

// ── Gate B P1 regression: pointee destructor re-enters the SAME atomic ────────
//
// Without deferred destruction, store()/CAS would run the displaced pointee's
// destructor while holding the (non-recursive) shard mutex; a destructor that
// re-enters the same atomic_shared_ptr re-locks the same shard → deadlock (the
// test would HANG). With the fix (displaced pointee destructs after the guard
// is released), the re-entrant store/load completes. Self-referential so the
// shard always collides. Forced-fallback only (the alias path has no shard).
TEST(AtomicSharedPtrReentrantDtor, DisplacedPointeeDtorReentersSameAtomicNoDeadlock) {
#if FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE
  GTEST_SKIP() << "native alias path has no shard lock; re-entrancy is N/A";
#else
  struct Reentrant {
    fixpp::sync::atomic_shared_ptr<Reentrant>* owner = nullptr;
    ~Reentrant() {
      // Re-enter the same atomic from the displaced pointee's destructor.
      if (owner != nullptr) {
        (void)owner->load(std::memory_order_acquire);
      }
    }
  };

  fixpp::sync::atomic_shared_ptr<Reentrant> a;
  auto first = std::make_shared<Reentrant>();
  first->owner = &a;
  a.store(first, std::memory_order_release);
  first.reset();  // a holds the only strong ref now

  // This store displaces `first`'s pointee; its dtor calls a.load() — must NOT
  // deadlock. If it hangs, the deferred-destruction fix regressed.
  a.store(std::make_shared<Reentrant>(), std::memory_order_release);
  SUCCEED() << "re-entrant displaced-pointee destructor did not deadlock";
#endif
}

// ── Row 3: refcount integrity under contention ────────────────────────────────
//
// Seeds an AtomicIntPtr; fires N threads doing load/store/exchange/CAS for
// kIters operations. Afterwards, clears the atomic and the seed; verifies the
// weak_ptr has expired (no leaked strong reference, no UAF).

TEST(AtomicSharedPtrRefcountIntegrity, WeakPtrExpiresAfterStore) {
  const int kIters = read_env_int("FIXPP_ASP_STRESS_ITERS", 2000);

  auto seed = std::make_shared<int>(5);
  std::weak_ptr<int> seed_weak = seed;
  AtomicIntPtr ptr(seed);

  const int kThreadCount = 6;
  std::vector<std::thread> workers;
  workers.reserve(kThreadCount);

  for (int t = 0; t < kThreadCount; ++t) {
    workers.emplace_back([&, t]() {
      std::mt19937 rng(static_cast<std::uint32_t>(1234 + t * 19));
      std::uniform_int_distribution<int> op_dist(0, 3);
      std::uniform_int_distribution<int> val_dist(1, 1000000);
      for (int i = 0; i < kIters; ++i) {
        const int op = op_dist(rng);
        if (op == 0) {
          (void)ptr.load(std::memory_order_relaxed);
        } else if (op == 1) {
          ptr.store(std::make_shared<int>(val_dist(rng)), std::memory_order_relaxed);
        } else if (op == 2) {
          (void)ptr.exchange(std::make_shared<int>(val_dist(rng)),
                             std::memory_order_relaxed);
        } else {
          auto expected = ptr.load(std::memory_order_relaxed);
          auto desired = std::make_shared<int>(val_dist(rng));
          (void)ptr.compare_exchange_weak(expected, desired, std::memory_order_relaxed,
                                          std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }

  // Clear the atomic and drop the seed strong reference.
  ptr.store(nullptr, std::memory_order_seq_cst);
  seed.reset();

  // Give any in-flight copies a moment to drain (short polling loop).
  for (int i = 0; i < 200 && !seed_weak.expired(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(seed_weak.expired())
      << "weak_ptr must expire: a strong reference was leaked or there is a UAF";
}

// ── Row 4: contention stress (load/store/exchange mix) ────────────────────────
//
// Many threads perform all four operations concurrently. Success = no crash,
// no torn reads, process exits cleanly (TSan/ASan catch any race).

TEST(AtomicSharedPtrContentionStress, NoTornReadUnderContention) {
  const int kIters = read_env_int("FIXPP_ASP_STRESS_ITERS", 2000);

  AtomicIntPtr ptr;
  std::atomic<bool> invalid_seen{false};

  const int kThreadCount = 8;
  std::vector<std::thread> workers;
  workers.reserve(kThreadCount);

  for (int t = 0; t < kThreadCount; ++t) {
    workers.emplace_back([&, t]() {
      std::mt19937 rng(static_cast<std::uint32_t>(4321 + t * 7));
      std::uniform_int_distribution<int> op_dist(0, 3);
      std::uniform_int_distribution<int> val_dist(1, 1'000'000);
      for (int i = 0; i < kIters; ++i) {
        const int op = op_dist(rng);
        if (op == 0) {
          ptr.store(std::make_shared<int>(val_dist(rng)), std::memory_order_relaxed);
        } else if (op == 1) {
          auto got = ptr.load(std::memory_order_acquire);
          // Any non-null value must be in-range (we only store positive ints).
          if (got && (*got < 1 || *got > 1'000'000)) {
            invalid_seen.store(true, std::memory_order_relaxed);
          }
        } else if (op == 2) {
          (void)ptr.exchange(std::make_shared<int>(val_dist(rng)),
                             std::memory_order_relaxed);
        } else {
          auto expected = ptr.load(std::memory_order_relaxed);
          auto desired = std::make_shared<int>(val_dist(rng));
          (void)ptr.compare_exchange_weak(expected, desired, std::memory_order_relaxed,
                                          std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_FALSE(invalid_seen.load()) << "Torn read observed (out-of-range value)";
}

// ── Row 7a: linearizability spot-check ───────────────────────────────────────
//
// Three threads each perform two operations on a single AtomicIntPtr,
// recording timestamps. After joining, we verify there exists at least one
// sequential ordering of the 6 ops (consistent with per-thread ordering and
// observed timestamps) that is consistent with a sequential shared-memory model.

TEST(AtomicSharedPtrLinearizability, SpotCheck) {
  struct Op {
    enum class Kind { Store, Load, Exchange, Cas };
    Kind kind{Kind::Load};
    int thread_id{0};
    int start_ns{0};
    int end_ns{0};
    int arg_expected{-1};
    int arg_desired{-1};
    int observed_value{-1};
    bool cas_success{false};
    int expected_after{-1};
  };

  auto null_ptr = std::shared_ptr<int>{};
  auto p1 = std::make_shared<int>(1);
  auto p2 = std::make_shared<int>(2);
  auto p3 = std::make_shared<int>(3);

  // Short random micro-sleep to increase interleaving.
  auto short_sleep = [](std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(5, 60);
    std::this_thread::sleep_for(std::chrono::microseconds(dist(rng)));
  };

  auto now_ns = []() -> int {
    const auto n = Clock::now().time_since_epoch();
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(n).count() & 0x7fffffff);
  };

  auto id_of = [&](const std::shared_ptr<int>& p) -> int {
    if (!p) return 0;
    if (equivalent(p, p1)) return 1;
    if (equivalent(p, p2)) return 2;
    if (equivalent(p, p3)) return 3;
    return -1;  // should not happen
  };

  AtomicIntPtr ptr;
  std::atomic<bool> go{false};
  std::array<Op, 6> ops{};
  std::atomic<int> cursor{0};

  auto thread_fn = [&](int thread_id) {
    while (!go.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    if (thread_id == 0) {
      {
        Op op;
        op.kind = Op::Kind::Store;
        op.thread_id = thread_id;
        op.arg_desired = 1;
        short_sleep(11U);
        op.start_ns = now_ns();
        ptr.store(p1);
        op.end_ns = now_ns();
        ops[cursor.fetch_add(1, std::memory_order_relaxed)] = op;
      }
      {
        Op op;
        op.kind = Op::Kind::Exchange;
        op.thread_id = thread_id;
        op.arg_desired = 2;
        short_sleep(23U);
        op.start_ns = now_ns();
        op.observed_value = id_of(ptr.exchange(p2));
        op.end_ns = now_ns();
        ops[cursor.fetch_add(1, std::memory_order_relaxed)] = op;
      }
    } else if (thread_id == 1) {
      {
        Op op;
        op.kind = Op::Kind::Load;
        op.thread_id = thread_id;
        short_sleep(37U);
        op.start_ns = now_ns();
        op.observed_value = id_of(ptr.load());
        op.end_ns = now_ns();
        ops[cursor.fetch_add(1, std::memory_order_relaxed)] = op;
      }
      {
        Op op;
        op.kind = Op::Kind::Cas;
        op.thread_id = thread_id;
        op.arg_expected = 2;
        op.arg_desired = 3;
        short_sleep(41U);
        op.start_ns = now_ns();
        auto expected = p2;
        op.cas_success = ptr.compare_exchange_strong(expected, p3);
        op.expected_after = id_of(expected);
        op.end_ns = now_ns();
        ops[cursor.fetch_add(1, std::memory_order_relaxed)] = op;
      }
    } else {
      {
        Op op;
        op.kind = Op::Kind::Cas;
        op.thread_id = thread_id;
        op.arg_expected = 0;
        op.arg_desired = 3;
        short_sleep(53U);
        op.start_ns = now_ns();
        auto expected = null_ptr;
        op.cas_success = ptr.compare_exchange_strong(expected, p3);
        op.expected_after = id_of(expected);
        op.end_ns = now_ns();
        ops[cursor.fetch_add(1, std::memory_order_relaxed)] = op;
      }
      {
        Op op;
        op.kind = Op::Kind::Load;
        op.thread_id = thread_id;
        short_sleep(67U);
        op.start_ns = now_ns();
        op.observed_value = id_of(ptr.load());
        op.end_ns = now_ns();
        ops[cursor.fetch_add(1, std::memory_order_relaxed)] = op;
      }
    }
  };

  std::thread t0(thread_fn, 0);
  std::thread t1(thread_fn, 1);
  std::thread t2(thread_fn, 2);
  go.store(true, std::memory_order_release);
  t0.join();
  t1.join();
  t2.join();

  ASSERT_EQ(cursor.load(std::memory_order_relaxed), 6) << "All 6 ops must complete";

  // Build must_before[i][j]: i must appear before j in the linearization.
  std::array<std::array<bool, 6>, 6> must_before{};
  for (auto& row : must_before) row.fill(false);
  for (int i = 0; i < 6; ++i) {
    for (int j = 0; j < 6; ++j) {
      if (i == j) continue;
      if (ops[i].thread_id == ops[j].thread_id && i < j) {
        must_before[i][j] = true;
      }
      if (ops[i].end_ns < ops[j].start_ns) {
        must_before[i][j] = true;
      }
    }
  }

  auto consistent_with_constraints = [&](const std::array<int, 6>& perm) {
    std::array<int, 6> pos{};
    for (int i = 0; i < 6; ++i) pos[perm[i]] = i;
    for (int i = 0; i < 6; ++i) {
      for (int j = 0; j < 6; ++j) {
        if (must_before[i][j] && !(pos[i] < pos[j])) return false;
      }
    }
    return true;
  };

  auto replay_and_check = [&](const std::array<int, 6>& perm) {
    int state = 0;
    for (int index : perm) {
      const Op& op = ops[static_cast<std::size_t>(index)];
      switch (op.kind) {
        case Op::Kind::Store:
          state = op.arg_desired;
          break;
        case Op::Kind::Load:
          if (op.observed_value != state) return false;
          break;
        case Op::Kind::Exchange:
          if (op.observed_value != state) return false;
          state = op.arg_desired;
          break;
        case Op::Kind::Cas: {
          const bool success = (state == op.arg_expected);
          if (success != op.cas_success) return false;
          if (success) {
            if (op.expected_after != op.arg_expected) return false;
            state = op.arg_desired;
          } else {
            if (op.expected_after != state) return false;
          }
          break;
        }
      }
    }
    return true;
  };

  std::array<int, 6> order{0, 1, 2, 3, 4, 5};
  bool found_linearization = false;
  std::sort(order.begin(), order.end());
  do {
    if (!consistent_with_constraints(order)) continue;
    if (replay_and_check(order)) {
      found_linearization = true;
      break;
    }
  } while (std::next_permutation(order.begin(), order.end()));

  EXPECT_TRUE(found_linearization)
      << "No valid linearization found — atomicity or ordering contract is broken";
}

// ── Row 7b: allocator-pressure stress ────────────────────────────────────────
//
// Many threads make_shared + store/exchange/CAS in a tight loop, exercising
// the allocator heavily. Absence of crashes / TSan/ASan findings = pass.

TEST(AtomicSharedPtrAllocatorPressure, NoMemoryErrorsUnderHighAlloc) {
  const int kIters = read_env_int("FIXPP_ASP_STRESS_ITERS", 2000);

  AtomicIntPtr ptr;
  const int kThreadCount = 8;
  std::vector<std::thread> workers;
  workers.reserve(kThreadCount);

  for (int t = 0; t < kThreadCount; ++t) {
    workers.emplace_back([&, t]() {
      std::mt19937 rng(static_cast<std::uint32_t>(6543 + t * 29));
      std::uniform_int_distribution<int> op_dist(0, 4);
      std::uniform_int_distribution<int> val_dist(1, 1'000'000);
      for (int i = 0; i < kIters; ++i) {
        const int op = op_dist(rng);
        auto fresh = std::make_shared<int>(val_dist(rng));
        if (op == 0) {
          ptr.store(fresh, std::memory_order_relaxed);
        } else if (op == 1) {
          (void)ptr.exchange(fresh, std::memory_order_relaxed);
        } else if (op == 2) {
          auto expected = ptr.load(std::memory_order_relaxed);
          (void)ptr.compare_exchange_weak(expected, fresh, std::memory_order_relaxed,
                                          std::memory_order_relaxed);
        } else if (op == 3) {
          (void)ptr.load(std::memory_order_relaxed);
        } else {
          auto expected = fresh;
          (void)ptr.compare_exchange_strong(
              expected, std::make_shared<int>(val_dist(rng)),
              std::memory_order_seq_cst, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }
  ptr.store(nullptr);
  SUCCEED() << "Allocator-pressure stress completed without memory errors";
}

// ── Row 10: many instances parallel isolation ────────────────────────────────
//
// 64 AtomicIntPtr instances, 8 threads each picking a random instance to
// store/load. No cross-instance corruption must occur.

TEST(AtomicSharedPtrManyInstancesIsolation, NoCrossInstanceCorruption) {
  const int kIters = read_env_int("FIXPP_ASP_STRESS_ITERS", 2000);
  constexpr int kInstances = 64;
  constexpr int kThreads = 8;

  std::vector<std::unique_ptr<AtomicIntPtr>> atoms;
  atoms.reserve(kInstances);
  for (int i = 0; i < kInstances; ++i) {
    atoms.push_back(std::make_unique<AtomicIntPtr>(std::make_shared<int>(i)));
  }

  std::atomic<bool> invalid_observation{false};
  std::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&, t]() {
      std::mt19937 rng(static_cast<std::uint32_t>(7100 + t));
      std::uniform_int_distribution<int> idx_dist(0, kInstances - 1);
      std::uniform_int_distribution<int> val_dist(0, kInstances - 1);
      for (int i = 0; i < kIters && !invalid_observation.load(std::memory_order_relaxed); ++i) {
        const int idx = idx_dist(rng);
        const int val = val_dist(rng);
        atoms[static_cast<std::size_t>(idx)]->store(
            std::make_shared<int>(val), std::memory_order_relaxed);
        auto got = atoms[static_cast<std::size_t>(idx)]->load(std::memory_order_relaxed);
        if (!got || *got < 0 || *got >= kInstances) {
          invalid_observation.store(true, std::memory_order_relaxed);
          return;
        }
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_FALSE(invalid_observation.load(std::memory_order_relaxed))
      << "An invalid (out-of-range or null) value was observed — cross-instance "
         "shard collision or memory corruption";
}

// ── Row 10 (cont): randomized mixed-op stress ────────────────────────────────
//
// Many threads, all four operations, multiple memory orders, bounded iterations.
// TSan/ASan are the defect-detection mechanism; we also assert at least one op ran.

TEST(AtomicSharedPtrRandomizedStress, BoundedMixedOpNoErrors) {
  const int kIters = read_env_int("FIXPP_ASP_STRESS_ITERS", 2000);
  const int kThreadCount = static_cast<int>(
      std::max(4U, std::thread::hardware_concurrency()));

  AtomicIntPtr ptr;
  std::atomic<std::uint64_t> op_count{0};
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(kThreadCount));

  for (int t = 0; t < kThreadCount; ++t) {
    workers.emplace_back([&, t]() {
      std::mt19937 rng(static_cast<std::uint32_t>(9001 + t * 7));
      std::uniform_int_distribution<int> op_dist(0, 3);
      std::uniform_int_distribution<int> val_dist(1, 50'000'000);

      for (int i = 0; i < kIters; ++i) {
        const int op = op_dist(rng);
        if (op == 0) {
          ptr.store(std::make_shared<int>(val_dist(rng)), std::memory_order_release);
        } else if (op == 1) {
          (void)ptr.load(std::memory_order_acquire);
        } else if (op == 2) {
          (void)ptr.exchange(std::make_shared<int>(val_dist(rng)),
                             std::memory_order_acq_rel);
        } else {
          auto expected = ptr.load(std::memory_order_relaxed);
          (void)ptr.compare_exchange_weak(
              expected, std::make_shared<int>(val_dist(rng)),
              std::memory_order_acq_rel, std::memory_order_acquire);
        }
        op_count.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  for (auto& thread : workers) {
    thread.join();
  }
  ptr.store(nullptr);

  EXPECT_GT(op_count.load(std::memory_order_relaxed), 0ULL)
      << "At least one operation must have run";
}

}  // namespace
