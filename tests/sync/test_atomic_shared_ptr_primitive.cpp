// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_atomic_shared_ptr_primitive.cpp — 046-atomic-shared-ptr NFR-017
//
// Single-threaded and static obligations for fixpp::sync::atomic_shared_ptr<T>.
// Rows ported from the locked research harness (18/18):
//   - AtomicSharedPtrSignatureStaticAssert   (row 1)
//   - AtomicSharedPtrNativeAliasIdentity     (row +A)
//   - AtomicSharedPtrSingleThreadRoundtrip   (row 2)
//   - AtomicSharedPtrCasEquivalence          (row 5)
//   - AtomicSharedPtrLockFreeReporting       (row 9)
//   - AtomicSharedPtrDetectionProbe          (row 8)
//
// Anchor: specs/046-atomic-shared-ptr/spec.md FR-001..FR-015 / plan.md test plan.

#include <gtest/gtest.h>

#include <fixpp/core/sync/detail/atomic_shared_ptr.hpp>

#include <memory>
#include <type_traits>

namespace {

using AtomicIntPtr = fixpp::sync::atomic_shared_ptr<int>;

// ── Helper: shared-ownership equivalence (mirrors harness) ───────────────────

static bool same_owner(const std::shared_ptr<int>& a,
                       const std::shared_ptr<int>& b) noexcept {
  return !a.owner_before(b) && !b.owner_before(a);
}

static bool equivalent(const std::shared_ptr<int>& a,
                        const std::shared_ptr<int>& b) noexcept {
  return a.get() == b.get() && same_owner(a, b);
}

// ── Row 1: compile-time API surface static_asserts ───────────────────────────
//
// AtomicSharedPtrLike concept (mirrors harness): checks all required members.
// value_type and is_always_lock_free probes added per advisor guidance.

template <typename AtomicLike>
concept AtomicSharedPtrLike = requires(AtomicLike a, const AtomicLike ca,
                                       std::shared_ptr<int> sp,
                                       std::shared_ptr<int>& expected,
                                       std::memory_order order) {
  AtomicLike{};
  AtomicLike{sp};
  { a.store(sp, order) } -> std::same_as<void>;
  { ca.load(order) } -> std::same_as<std::shared_ptr<int>>;
  { a.exchange(sp, order) } -> std::same_as<std::shared_ptr<int>>;
  { a.compare_exchange_weak(expected, sp, order, order) } -> std::same_as<bool>;
  { a.compare_exchange_weak(expected, sp, order) } -> std::same_as<bool>;
  { a.compare_exchange_strong(expected, sp, order, order) } -> std::same_as<bool>;
  { a.compare_exchange_strong(expected, sp, order) } -> std::same_as<bool>;
  a = sp;
  { static_cast<std::shared_ptr<int>>(ca) } -> std::same_as<std::shared_ptr<int>>;
  { ca.is_lock_free() } -> std::same_as<bool>;
};

static_assert(AtomicSharedPtrLike<AtomicIntPtr>,
              "atomic_shared_ptr API is missing required members.");
static_assert(!std::is_copy_constructible_v<AtomicIntPtr>,
              "atomic_shared_ptr must be non-copyable.");
static_assert(!std::is_copy_assignable_v<AtomicIntPtr>,
              "atomic_shared_ptr must be non-copy-assignable.");

// value_type must be shared_ptr<T>.
static_assert(std::is_same_v<AtomicIntPtr::value_type, std::shared_ptr<int>>,
              "value_type must be shared_ptr<T>");

// is_always_lock_free must be present and have the correct fallback value.
#if !FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE
static_assert(AtomicIntPtr::is_always_lock_free == false,
              "Fallback contract requires is_always_lock_free=false.");
#endif

TEST(AtomicSharedPtrSignatureStaticAssert, CompilesWithCorrectSurface) {
  // All assertions are compile-time (static_assert above). A trivial runtime
  // check confirms the test binary executed this TU and linked correctly.
  AtomicIntPtr ptr;
  EXPECT_EQ(ptr.load(), nullptr);
}

// ── Row +A: native alias identity ─────────────────────────────────────────────

TEST(AtomicSharedPtrNativeAliasIdentity, AliasOrSkip) {
#if FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE
  static_assert(
      std::is_same_v<fixpp::sync::atomic_shared_ptr<int>,
                     std::atomic<std::shared_ptr<int>>>,
      "On the native path, atomic_shared_ptr<T> must be an alias for "
      "std::atomic<std::shared_ptr<T>>");
  SUCCEED() << "Native alias identity confirmed at compile time.";
#else
  GTEST_SKIP() << "Fallback path is active (FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE=0); "
                  "native alias identity check skipped.";
#endif
}

// ── Row 2: single-thread round-trips ─────────────────────────────────────────

TEST(AtomicSharedPtrSingleThreadRoundtrip, DefaultNullAndRoundtrips) {
  AtomicIntPtr ptr;
  EXPECT_EQ(ptr.load(), nullptr) << "Default-constructed must be null";

  auto one = std::make_shared<int>(1);
  auto two = std::make_shared<int>(2);

  ptr.store(one);
  EXPECT_TRUE(equivalent(ptr.load(), one)) << "load() after store(one) must return one";

  auto old = ptr.exchange(two);
  EXPECT_TRUE(equivalent(old, one)) << "exchange() must return previous value";
  EXPECT_TRUE(equivalent(ptr.load(), two)) << "load() after exchange(two) must return two";

  // CAS success: expected == current → swap in three.
  auto expected = two;
  auto three = std::make_shared<int>(3);
  EXPECT_TRUE(ptr.compare_exchange_strong(expected, three))
      << "CAS must succeed when expected shares ownership with current";
  EXPECT_TRUE(equivalent(ptr.load(), three)) << "load() must reflect CAS result";
}

TEST(AtomicSharedPtrSingleThreadRoundtrip, CasFailureUpdatesExpected) {
  auto stored = std::make_shared<int>(10);
  AtomicIntPtr ptr(stored);

  // Distinct control block even though value matches — must fail.
  auto expected = std::make_shared<int>(10);
  auto desired = std::make_shared<int>(11);
  EXPECT_FALSE(ptr.compare_exchange_strong(expected, desired))
      << "CAS must fail when expected has a distinct control block";
  // On failure, expected is updated to the current stored value.
  EXPECT_TRUE(equivalent(expected, stored))
      << "On CAS failure, expected must be updated to the current value";
  EXPECT_TRUE(equivalent(ptr.load(), stored))
      << "Atomic value must be unchanged after CAS failure";

  // Now use the shared-ownership pointer — must succeed.
  expected = stored;
  auto expected_before = expected;
  EXPECT_TRUE(ptr.compare_exchange_strong(expected, desired))
      << "CAS must succeed with shared-ownership expected";
  // On success, expected must NOT be overwritten (harness row 2 invariant).
  EXPECT_TRUE(equivalent(expected, expected_before))
      << "On CAS success, expected must retain its pre-call value";
  EXPECT_TRUE(equivalent(ptr.load(), desired)) << "Atomic value must be desired after success";
}

// ── Row 5: CAS equivalence semantics (the 3-discriminator rule) ───────────────

TEST(AtomicSharedPtrCasEquivalence, ThreeDiscriminators) {
  // Discriminator 1: distinct objects with equal VALUE → not equivalent, CAS fails.
  auto a1 = std::make_shared<int>(7);
  auto a2 = std::make_shared<int>(7);
  AtomicIntPtr ptr(a1);
  auto expected_distinct = a2;
  auto desired = std::make_shared<int>(9);
  EXPECT_FALSE(ptr.compare_exchange_strong(expected_distinct, desired))
      << "Distinct control blocks must fail CAS even when values are equal";
  EXPECT_TRUE(equivalent(expected_distinct, a1))
      << "expected must be updated to current on failure";

  // Discriminator 2: aliasing pointers — same raw pointer, different control block.
  // owner2 keeps owner1's raw pointer via alias: raw() equal but !same_owner().
  auto owner1 = std::make_shared<int>(111);
  auto owner2 = std::make_shared<int>(222);
  std::shared_ptr<int> alias_wrong_owner(owner2, owner1.get());
  ptr.store(owner1);
  auto expected_alias = alias_wrong_owner;
  EXPECT_FALSE(ptr.compare_exchange_strong(expected_alias, desired))
      << "Aliasing ptr (same raw, different control block) must fail CAS";
  EXPECT_TRUE(equivalent(expected_alias, owner1))
      << "expected must be updated to current on failure";
  // Sanity: alias conditions hold.
  EXPECT_EQ(owner1.get(), alias_wrong_owner.get());
  EXPECT_FALSE(same_owner(owner1, alias_wrong_owner));

  // Discriminator 3: same control block and same raw pointer → CAS succeeds.
  auto shared_owner = std::make_shared<int>(42);
  ptr.store(shared_owner);
  auto expected_same_owner = shared_owner;
  EXPECT_TRUE(ptr.compare_exchange_strong(expected_same_owner, desired))
      << "Shared-ownership expected must succeed CAS";
  EXPECT_TRUE(equivalent(ptr.load(), desired));

  // Null vs null: must succeed.
  AtomicIntPtr null_ptr;
  std::shared_ptr<int> expected_null;
  EXPECT_TRUE(null_ptr.compare_exchange_strong(expected_null, std::make_shared<int>(1)))
      << "null vs null CAS must succeed";
}

// ── Row 9: lock-free reporting ────────────────────────────────────────────────

TEST(AtomicSharedPtrLockFreeReporting, LockFreeContract) {
#if FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE
  AtomicIntPtr ptr;
  // On the native path we only record; we don't assert a specific bool value
  // since the standard does not mandate is_lock_free() == true for all
  // implementations.
  RecordProperty("native_is_lock_free",
                 ptr.is_lock_free() ? "true" : "false");
  SUCCEED() << "Native path: is_lock_free()=" << std::boolalpha
            << ptr.is_lock_free() << " (informational)";
#else
  AtomicIntPtr ptr;
  EXPECT_FALSE(ptr.is_lock_free())
      << "Fallback must report is_lock_free()==false";
  EXPECT_FALSE(AtomicIntPtr::is_always_lock_free)
      << "Fallback must report is_always_lock_free==false";
#endif
}

// ── Row 8: detection probe ────────────────────────────────────────────────────

TEST(AtomicSharedPtrDetectionProbe, MacrosConsistent) {
  // Print both macro values so the ctest tail proves the gate is real.
  RecordProperty("FIXPP_HAS_STD_ATOMIC_SHARED_PTR",
                 std::to_string(FIXPP_HAS_STD_ATOMIC_SHARED_PTR));
  RecordProperty("FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE",
                 std::to_string(FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE));

  // Consistency: if forced-fallback is ON, both macros must report fallback.
#if defined(FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK)
  static_assert(FIXPP_HAS_STD_ATOMIC_SHARED_PTR == 0,
                "Forced fallback must set detection macro to 0");
  static_assert(FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE == 0,
                "Forced fallback must set native-active macro to 0");
  EXPECT_EQ(FIXPP_HAS_STD_ATOMIC_SHARED_PTR, 0);
  EXPECT_EQ(FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE, 0);
#else
  // Without forcing, the two macros must be consistent with each other:
  // NATIVE_ACTIVE can only be 1 if HAS_STD is 1.
#if FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE
  EXPECT_EQ(FIXPP_HAS_STD_ATOMIC_SHARED_PTR, 1)
      << "NATIVE_ACTIVE=1 implies HAS_STD_ATOMIC_SHARED_PTR=1";
#else
  // Fallback active — both 0 or only HAS_STD=0.
  EXPECT_EQ(FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE, 0);
#endif
#endif

  // Print resolved path so it appears in --output-on-failure.
#if FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE
  std::cout << "atomic_shared_ptr path: NATIVE (std::atomic<std::shared_ptr<T>>)\n";
#else
  std::cout << "atomic_shared_ptr path: FALLBACK (sharded-mutex)\n";
#endif
  std::cout << "FIXPP_HAS_STD_ATOMIC_SHARED_PTR=" << FIXPP_HAS_STD_ATOMIC_SHARED_PTR << "\n";
  std::cout << "FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE=" << FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE << "\n";
}

}  // namespace
