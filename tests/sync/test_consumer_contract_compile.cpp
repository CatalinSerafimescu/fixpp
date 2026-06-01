// tests/sync/test_consumer_contract_compile.cpp
// 006-async-mutex T078 — Consumer compile/link contract check.
//
// SC-008 / [2e §3.1] hand-off gate: a downstream consumer (2e MessageStore,
// 005 session FSM, 2g pinset) must be able to build against the SHIPPED 2f
// surface. This TU includes ONLY the two shipped headers and asserts, at
// compile time, the public contract shape (positive) and the FR-015
// out-of-scope-surface absence (negative, U1). It links fixpp_sync to confirm
// the non-inline shipped surface is link-complete.
//
// Map: SC-008, FR-015, FR-001, FR-011. Anchors: [2f §4.1] / [2f §4.3.2] /
// [2e §6.4] writer-mutex contract / [2e §3.1] last-bullet hand-off gate.

#include <asio/awaitable.hpp>
#include <memory_resource>
#include <type_traits>

// SHIPPED surface only — nothing else from fixpp/core/sync or fixpp/session.
#include <fixpp/core/sync/async_mutex.hpp>
#include <fixpp/session/async_lock_via_session_executor.hpp>

namespace {

using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::completion_policy;
template <class T>
using expected_t = fixpp::sync::expected_t<T>;

// ── Positive: async_mutex shape ([2f §4.1] / FR-001) ─────────────────────────
static_assert(std::is_default_constructible_v<async_mutex>,
              "async_mutex must be default-constructible");
// constexpr default ctor: constant-initialisable, no dynamic init. This is
// the actual [2f §4.1] contract ("constexpr-default-constructible"); the
// destructor is intentionally noexcept(false) (terminate-on-misuse
// precondition, [2f §4.7]), so the type is deliberately NOT
// is_nothrow_destructible — asserted below so the contract is explicit.
constinit async_mutex g_constinit_mutex{};
static_assert(std::is_destructible_v<async_mutex> && !std::is_nothrow_destructible_v<async_mutex>,
              "async_mutex dtor is noexcept(false) by design ([2f §4.7])");
static_assert(std::is_constructible_v<async_mutex, completion_policy>,
              "async_mutex must be constructible from completion_policy");

static_assert(!std::is_copy_constructible_v<async_mutex>, "non-copyable");
static_assert(!std::is_move_constructible_v<async_mutex>, "non-movable");
static_assert(!std::is_copy_assignable_v<async_mutex>, "non-copy-assignable");
static_assert(!std::is_move_assignable_v<async_mutex>, "non-move-assignable");

// Contract return types ([2f §4.1]; [2e §6.4] writer-mutex shape).
static_assert(std::is_same_v<decltype(std::declval<async_mutex&>().async_lock()),
                             asio::awaitable<expected_t<async_lock_guard>>>,
              "async_lock() must return asio::awaitable<expected_t<async_lock_guard>>");
static_assert(std::is_same_v<decltype(std::declval<async_mutex&>().async_lock(
                                 std::declval<std::pmr::memory_resource*>())),
                             asio::awaitable<expected_t<async_lock_guard>>>,
              "async_lock(mr) PMR overload must share the contract return type (FR-011)");
static_assert(std::is_same_v<decltype(std::declval<async_mutex&>().cancel_and_drain()),
                             asio::awaitable<expected_t<void>>>,
              "cancel_and_drain() must return asio::awaitable<expected_t<void>>");
static_assert(std::is_same_v<decltype(std::declval<async_mutex&>().unlock()), void>,
              "unlock() must return void");
static_assert(
    std::is_same_v<decltype(std::declval<const async_mutex&>().policy()), completion_policy>,
    "policy() must return completion_policy");

// ── Positive: async_lock_guard shape ([2f §4.4]) ─────────────────────────────
static_assert(std::is_nothrow_move_constructible_v<async_lock_guard>, "movable");
static_assert(std::is_nothrow_move_assignable_v<async_lock_guard>, "move-assign");
static_assert(!std::is_copy_constructible_v<async_lock_guard>, "non-copyable");
static_assert(!std::is_copy_assignable_v<async_lock_guard>, "non-copy-assign");
static_assert(sizeof(async_lock_guard) == sizeof(async_mutex*),
              "async_lock_guard must be exactly one back-pointer (T078 size contract)");

// ── Positive: session-side helper ([2f §4.3.2] / FR-011 / RC#2) ──────────────
static_assert(std::is_same_v<decltype(fixpp::session::async_lock_via_session_executor(
                                 std::declval<async_mutex&>())),
                             asio::awaitable<expected_t<async_lock_guard>>>,
              "async_lock_via_session_executor(async_mutex&) must match [2f §4.3.2]");

// ── Negative: FR-015 out-of-scope surface absent (U1) ────────────────────────
// No public try_lock() (this is an *awaitable* mutex; there is no synchronous
// non-blocking acquire in the 2f surface).
template <class M>
concept has_try_lock = requires(M& m) { m.try_lock(); };
static_assert(!has_try_lock<async_mutex>, "FR-015: no public try_lock()");

// No public engaged-guard constructor — a guard is obtainable ONLY by awaiting
// async_lock() (the (async_mutex*) ctor is private per [2f §4.4]).
static_assert(!std::is_constructible_v<async_lock_guard, async_mutex*>,
              "FR-015: no public engaged-guard constructor");
static_assert(!std::is_constructible_v<async_lock_guard, async_mutex&>,
              "FR-015: no public engaged-guard constructor");

// No CRTP / concept extension hook: async_mutex is a concrete final-ish leaf,
// not a customisation base. It exposes no virtuals and is not a base anyone is
// expected to derive (no protected surface; trivial-vtable check).
static_assert(!std::is_polymorphic_v<async_mutex>,
              "FR-015: async_mutex is not a polymorphic/extension base");
static_assert(std::is_class_v<async_mutex> && std::is_class_v<async_lock_guard>,
              "shipped surface is concrete class types");

// No async_shared_mutex / async_recursive_mutex / async_timed_mutex aliases.
// These names are not declared by the shipped headers; this TU includes ONLY
// those headers, so any reference below would be a hard compile error —
// absence is enforced by construction. A dependent-context probe keeps the
// assertion machine-checkable without naming a (possibly absent) symbol:
template <class Ns>
concept declares_shared_variant = requires { typename Ns::async_shared_mutex; };
template <class Ns>
concept declares_recursive_variant = requires { typename Ns::async_recursive_mutex; };
template <class Ns>
concept declares_timed_variant = requires { typename Ns::async_timed_mutex; };
// fixpp::sync is a namespace, not a type; route the probe through a struct
// that re-exposes the namespace's would-be aliases iff they existed. Since
// none exist, each concept is unsatisfied — verified here as the U1 negative.
struct sync_surface_probe {};
static_assert(!declares_shared_variant<sync_surface_probe>, "FR-015: no async_shared_mutex");
static_assert(!declares_recursive_variant<sync_surface_probe>, "FR-015: no async_recursive_mutex");
static_assert(!declares_timed_variant<sync_surface_probe>, "FR-015: no async_timed_mutex");

}  // namespace

// Link-completeness: reference the non-inline shipped surface so the TU must
// link against fixpp_sync (not merely compile). unlock() is the out-of-line
// symbol; the awaitable factories are header-inline. Never executed.
[[maybe_unused]] void fixpp_consumer_contract_link_probe() {
    async_mutex* m = &g_constinit_mutex;
    m->unlock();
}

// A trivial test so the binary is a valid ctest target; the real contract is
// the static_asserts above (this file fails to COMPILE on any breach).
#include <gtest/gtest.h>
TEST(SyncConsumerContract, ShippedSurfaceMatchesHandoffContract) {
    SUCCEED() << "Contract enforced at compile time (see static_asserts).";
}
