// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_quickfix_compat_path_b_guard.cpp
//
// Seam 11 — Path B compile-time guard (FR-032 / AC US4 #2).
// Task T044.
//
// The binding gate is a FILE-SCOPE static_assert:
//   static_assert(!std::is_constructible_v<fixpp::session::MessageStore,
//                                          FakeQuickFixStore*>)
//
// If a future change accidentally introduces an implicit conversion path from
// a synchronous QuickFIX-shaped type into fixpp::session::MessageStore, the
// build fails here before any test binary runs.  No runtime assertion is
// needed — the BUILD IS THE TEST.
//
// The GoogleTest case body is intentionally empty (comment-only): the
// compile-time check is the entire test.  A test binary that compiles is a
// passing test; a test binary that fails to compile is a failing test.

#include <gtest/gtest.h>

#include <fixpp/session/message_store.hpp>
#include <string>
#include <type_traits>
#include <vector>

namespace {

// FakeQuickFixStore: mimics the synchronous QuickFIX MessageStore interface.
// The exact method signatures mirror the real QuickFIX shape (synchronous,
// non-awaitable, no expected_t return):
//   void set(int seq, std::string body)
//   bool get(int begin, int end, std::vector<std::string>& out)
// This is intentionally incompatible with fixpp::session::MessageStore's
// async awaitable<expected_t<...>> interface.
struct FakeQuickFixStore {
    void set(int /*seq*/, std::string /*body*/) {}
    bool get(int /*begin*/, int /*end*/, std::vector<std::string>& /*out*/) { return false; }
};

// ── Compile-time Path-B guard ─────────────────────────────────────────────────
//
// fixpp::session::MessageStore is non-constructible from a FakeQuickFixStore*:
//   - MessageStore's protected constructor takes a flush_hook_fn (a function
//     pointer type), which is distinct from FakeQuickFixStore*.
//   - MessageStore has no converting constructors accepting arbitrary pointers.
//   - MessageStore is abstract (4 pure-virtual methods) and therefore cannot
//     be directly constructed at all.
//
// The double guard below covers both:
//   (a) direct construction from a raw pointer (the main Path-B hazard)
//   (b) direct default construction (MessageStore is abstract — excluded for
//       completeness, though this is already guaranteed by the pure virtuals)
static_assert(!std::is_constructible_v<fixpp::session::MessageStore, FakeQuickFixStore*>,
              "Path-B guard: implicit construction from sync QuickFIX shape is forbidden. "
              "fixpp::session::MessageStore must not be constructible from a "
              "FakeQuickFixStore* — this regression means an implicit conversion path "
              "was accidentally introduced.");

static_assert(!std::is_default_constructible_v<fixpp::session::MessageStore>,
              "Path-B guard: MessageStore must remain abstract (non-default-constructible). "
              "A default-constructible MessageStore would bypass the async interface "
              "contract and allow invalid store objects.");

// ── GoogleTest placeholder ────────────────────────────────────────────────────
//
// The test body is intentionally empty.  The compile-time static_asserts above
// ARE the test.  If this file compiles, the Path-B guard is intact.
// If a future change breaks the guard, the build fails on the static_assert
// and this binary is never produced.
TEST(QuickFixCompatPathBGuard, CompileTimeGuardIsIntact) {
    // No runtime assertions needed.  The fact that this binary was compiled
    // is proof that:
    //   1. MessageStore is not constructible from FakeQuickFixStore*.
    //   2. MessageStore is not default-constructible (it is abstract).
    //
    // Any regression that makes either static_assert fire will produce a
    // build error, which CTest will report as a configure/build failure
    // BEFORE this test body is ever reached.
    SUCCEED();
}

}  // namespace
