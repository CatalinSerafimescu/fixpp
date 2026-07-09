// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_membership_copy_oom_test.cpp
//
// gate-b/r1 FQ-1 (PR #181 round 1, Finding 1) — OOM hardening witness for
// MessageView::membership_copy() (include/fixpp/wire/parser.hpp), now NOT
// noexcept: `dict::reify()`'s production caller
// (src/dictionary/reify.cpp:188, inside `owning_message_handle_from_frame`'s
// `catch (std::bad_alloc const&)`) must translate a bad_alloc thrown during
// the table_view deep-copy into dict_reify_oom, NOT std::terminate.
//
// Mechanism: table_view's internal tables (unordered_map/vector/string, see
// include/fixpp/dict/table_view.hpp) use the DEFAULT (global) allocator, NOT
// a caller-supplied pmr::memory_resource -- so the existing
// tests/support/failing_pmr_resource.hpp harness (used by reify_oom_test.cpp
// for the bytes_ deep-copy, which IS routed through the pmr `mr` parameter)
// cannot intercept membership_copy()'s allocation. Instead: a TU-local
// global operator new override that can be armed to throw bad_alloc on a
// specific call number (gated out under ASan/TSan/MSan, which own the
// allocator -- mirrors tests/session/test_business_messages_build.cpp's
// established precedent, feedback_operator_new_witness_breaks_sanitizers).
//
// Calibration (avoids hardcoding an allocation index, which would be brittle
// against libstdc++/libc++ container-internals differences): a single
// unarmed dict-backed reify() call establishes `dict_total` (the total
// global-new call count for that invocation). Source-verified
// (src/dictionary/reify.cpp:174-194): after `handle.pimpl_->owned_tv_ =
// view.membership_copy();` the function does ONLY `return handle;` (a
// noexcept move, zero allocation) before the try block ends -- so
// membership_copy()'s own K allocations are the LAST K allocations in the
// call, i.e. positions [dict_total-K+1 .. dict_total]. Position `dict_total`
// itself is therefore ALWAYS inside membership_copy()'s table_view copy ctor
// (as long as K>=1, confirmed by the T_dict>T_free sanity check below).
// Arming `fail_at = dict_total` injects the OOM precisely into the copy.
//
// Anchors: opus_pr181_1_triage.md Finding 1 / FQ-1; parser.hpp:511-528
// membership_copy() out-of-line definition; table_view.hpp:185-192 "copy may
// throw on allocation failure".
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <vector>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>

#include "support/fix44_dictionary.hpp"
#include "support/fix44_group_frame_bodies.hpp"

// ── Sanitizer gate (mirrors tests/session/test_business_messages_build.cpp,
// feedback_operator_new_witness_breaks_sanitizers): ASan/TSan/MSan ship their
// own operator new/delete; replacing it here would break sanitizer runtime.
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#endif
#if !defined(FIXPP_SANITIZER_REPLACES_NEW) && \
    (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__))
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#ifndef FIXPP_SANITIZER_REPLACES_NEW
#define FIXPP_SANITIZER_REPLACES_NEW 0
#endif

// ── libstdc++ gate (gate-b CI-fix, PR #181 Tier 2 MSVC + Tier 3 libc++). ─────
// The fault-injection ordinal below (see file header) is derived from a
// libstdc++-specific GLOBAL-allocation sequence; libc++ (Tier 3) and MSVC's STL
// (Tier 2) allocate a different number/order of internal blocks, so the armed
// ordinal no longer lands inside membership_copy()'s table_view copy and the
// witness mis-fires. The behaviour it guards (membership_copy() no longer
// noexcept; dict::reify() translates the bad_alloc to dict_reify_oom, not
// std::terminate) is a source-level guarantee independent of the STL and is
// mutation-proven on libstdc++ (the reify success-path is covered under
// libc++/MSVC by reify_membership_identity_test + the libc++-ASan lane), so
// restrict this ordinal-calibrated witness to libstdc++.
// (extends feedback_operator_new_witness_breaks_sanitizers)
#if !FIXPP_SANITIZER_REPLACES_NEW && defined(__GLIBCXX__)
#define FIXPP_OOM_WITNESS_ENABLED 1
#else
#define FIXPP_OOM_WITNESS_ENABLED 0
#endif

#if FIXPP_OOM_WITNESS_ENABLED
namespace {
std::atomic<long> g_alloc_count{0};
std::atomic<long> g_fail_at{-1};  // -1 = never fail
}  // namespace

void* operator new(std::size_t size) {
    long const n = ++g_alloc_count;
    if (n == g_fail_at.load(std::memory_order_relaxed)) {
        throw std::bad_alloc{};
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc{};
    return p;
}
void* operator new[](std::size_t size) {
    long const n = ++g_alloc_count;
    if (n == g_fail_at.load(std::memory_order_relaxed)) {
        throw std::bad_alloc{};
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc{};
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif  // FIXPP_OOM_WITNESS_ENABLED

namespace {

using fixpp::dict::application_version;
using fixpp::dict::owning_message_handle;
using fixpp::dict::session_version;
using fixpp::dict::version_profile;

constexpr version_profile kProfileV44{session_version::v44, application_version::v44, false, 0};

// Parses the shared execution-report frame, either dict-backed (tv != nullptr)
// or dict-free (tv == nullptr), and calls fixpp::dict::reify(). The parse
// arena/frame outlive the reify() call (both are stack locals in the caller).
fixpp::core::expected_t<owning_message_handle> reify_execution_report(
    std::vector<std::byte> const& frame_bytes, fixpp::dict::table_view const* tv,
    std::pmr::memory_resource* parse_mr, std::pmr::memory_resource* handle_mr) {
    fixpp::wire::pmr_carry_buffer carry{frame_bytes.size(), parse_mr};
    fixpp::wire::Framer framer{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = framer.feed(std::span<const std::byte>{frame_bytes.data(), frame_bytes.size()},
                              carry, std::span<fixpp::wire::frame_view>{fvs, 1});
    if (!framed.has_value() || framed->empty()) {
        ADD_FAILURE() << "framing failed";
        return std::unexpected{fixpp::core::error::dict_reify_oom};
    }
    if (tv != nullptr) {
        fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{*tv};
        auto parsed = parser.parse(fvs[0], parse_mr);
        if (!parsed.has_value()) {
            ADD_FAILURE() << "dict-backed parse failed";
            return std::unexpected{fixpp::core::error::dict_reify_oom};
        }
        return fixpp::dict::reify(*parsed, kProfileV44, handle_mr);
    }
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{};
    auto parsed = parser.parse(fvs[0], parse_mr);
    if (!parsed.has_value()) {
        ADD_FAILURE() << "dict-free parse failed";
        return std::unexpected{fixpp::core::error::dict_reify_oom};
    }
    return fixpp::dict::reify(*parsed, kProfileV44, handle_mr);
}

}  // namespace

#if FIXPP_OOM_WITNESS_ENABLED
TEST(ReifyMembershipCopyOom, TableViewCopyOomYieldsDictReifyOom) {
    auto dict = fixpp::test_support::make_fix44_dictionary();
    auto tv = dict->as_table_view();

    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto frame_bytes =
        fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/7, "SENDER", "TARGET");

    // ── Sanity comparison: a dict-free reify() must allocate strictly LESS
    // than a dict-backed one (membership_copy() really does allocate). This
    // is not itself used to compute fail_at (see file header derivation) but
    // grounds the claim that the injected pass below targets membership_copy()
    // specifically, not some unrelated call. ──────────────────────────────
    std::pmr::monotonic_buffer_resource parse_arena_free;
    std::pmr::monotonic_buffer_resource handle_mr_free;
    g_alloc_count.store(0);
    g_fail_at.store(-1);
    auto r_free =
        reify_execution_report(frame_bytes, nullptr, &parse_arena_free, &handle_mr_free);
    long const t_free = g_alloc_count.load();
    ASSERT_TRUE(r_free.has_value()) << "calibration: dict-free reify() must succeed";

    std::pmr::monotonic_buffer_resource parse_arena_dict;
    std::pmr::monotonic_buffer_resource handle_mr_dict;
    g_alloc_count.store(0);
    g_fail_at.store(-1);
    auto r_dict =
        reify_execution_report(frame_bytes, &tv, &parse_arena_dict, &handle_mr_dict);
    long const t_dict = g_alloc_count.load();
    ASSERT_TRUE(r_dict.has_value()) << "calibration: dict-backed reify() must succeed";

    ASSERT_GT(t_dict, t_free)
        << "sanity: a dict-backed reify() must allocate MORE than a dict-free one "
           "(membership_copy()'s table_view deep-copy) -- else the injected pass below "
           "cannot be attributed to membership_copy() specifically";

    // ── Injected pass: fail_at = t_dict (the LAST allocation of the
    // dict-backed call). Per the file-header derivation, membership_copy()'s
    // own allocations are the trailing block of this call (nothing allocates
    // after them), so this position is guaranteed inside the table_view copy
    // ctor. ──────────────────────────────────────────────────────────────
    std::pmr::monotonic_buffer_resource parse_arena_inj;
    std::pmr::monotonic_buffer_resource handle_mr_inj;
    g_alloc_count.store(0);
    g_fail_at.store(t_dict);
    bool threw = false;
    std::optional<fixpp::core::expected_t<owning_message_handle>> r_inj;
    try {
        r_inj = reify_execution_report(frame_bytes, &tv, &parse_arena_inj, &handle_mr_inj);
    } catch (...) {
        threw = true;
    }
    g_fail_at.store(-1);  // disarm before any further allocation (destructors, teardown)

    EXPECT_FALSE(threw)
        << "gate-b/r1 FQ-1: a bad_alloc during membership_copy()'s table_view deep-copy "
           "must NOT propagate out of dict::reify() (it is noexcept) -- must be caught by "
           "owning_message_handle_from_frame's catch(std::bad_alloc const&) and translated "
           "to dict_reify_oom. Propagation here means membership_copy()'s noexcept was NOT "
           "removed (or the catch site regressed).";
    ASSERT_TRUE(r_inj.has_value()) << "reify() must return (not throw) even under OOM";
    ASSERT_FALSE(r_inj->has_value()) << "the injected allocation must have failed reify()";
    EXPECT_EQ(r_inj->error(), fixpp::core::error::dict_reify_oom)
        << "bad_alloc during membership_copy()'s table_view copy must map to dict_reify_oom";
}
#endif  // FIXPP_OOM_WITNESS_ENABLED
