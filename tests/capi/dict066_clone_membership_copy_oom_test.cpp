// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/dict066_clone_membership_copy_oom_test.cpp
//
// gate-b/r1 FQ-1 (PR #181 round 1, Finding 1) — OOM hardening witness for
// MessageView::membership_copy() (include/fixpp/wire/parser.hpp), now NOT
// noexcept: `fixpp_msg_clone()`'s production caller
// (src/capi/message_write.cpp:446, inside the function's single
// `catch (...)` block) must translate a bad_alloc thrown during the
// table_view deep-copy into FIXPP_ERR_CAPI_CONFIG_INVALID, NOT
// std::terminate.
//
// Construction strategy: mirrors tests/capi/message_read_test.cpp's
// "InboundHandle" pattern (a stack fixpp_msg wrapping a real
// wire::MessageView<Index>, reinterpret_cast to fixpp_msg_t*) rather than a
// live engine/session loopback (tests/capi/dict066_clone_identity_test.cpp)
// -- no sockets/threads are needed to exercise fixpp_msg_clone(), and a
// single-threaded, deterministic harness is required for the global
// operator-new fault-injection technique below.
//
// Mechanism: table_view's internal tables use the DEFAULT (global)
// allocator (include/fixpp/dict/table_view.hpp), NOT a caller-supplied
// pmr::memory_resource, so a TU-local global operator new override (gated
// out under ASan/TSan/MSan — mirrors
// tests/session/test_business_messages_build.cpp /
// feedback_operator_new_witness_breaks_sanitizers) is armed to throw
// bad_alloc on a specific call number.
//
// Calibration: source-verified (src/capi/message_write.cpp:370-476), the
// ENTIRE fixpp_msg_clone() body is one try/catch(...). After the dict-backed
// branch's `clone->owned_tv_ = h->view->membership_copy();`, exactly ONE
// further global-new call remains before the try block ends (the
// `std::make_unique<MessageView<Index>>(std::move(*parsed))` inside the
// `if (parsed)` arm). So membership_copy()'s own K allocations are positions
// [dict_total-K .. dict_total-1] -- position `dict_total - 1` is therefore
// ALWAYS the LAST allocation inside membership_copy()'s table_view copy ctor
// (as long as K>=1, confirmed by the T_dict>T_free sanity check below).
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

#include "fix/c_api/message.h"
#include "fix/c_api/error.h"

#include "capi_internal.hpp"  // engine-internal fixpp_msg (test-only access)

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/parser.hpp>

#include "support/fix44_dictionary.hpp"
#include "support/fix44_group_frame_bodies.hpp"

// ── Sanitizer gate (mirrors tests/session/test_business_messages_build.cpp,
// feedback_operator_new_witness_breaks_sanitizers). ────────────────────────
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

#if !FIXPP_SANITIZER_REPLACES_NEW
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
#endif  // !FIXPP_SANITIZER_REPLACES_NEW

namespace {

struct InboundHandle {
    fixpp_msg msg{};
    const fixpp_msg_t* ptr() const noexcept { return reinterpret_cast<const fixpp_msg_t*>(&msg); }
};

}  // namespace

#if !FIXPP_SANITIZER_REPLACES_NEW
TEST(CloneMembershipCopyOom, TableViewCopyOomYieldsCapiConfigInvalid) {
    auto dict = fixpp::test_support::make_fix44_dictionary();
    auto tv = dict->as_table_view();

    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto frame_bytes =
        fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/9, "SENDER", "TARGET");

    // ── Calibration pass 1: dict-FREE clone (no membership_copy() call) ─────
    std::pmr::monotonic_buffer_resource parse_arena_free;
    fixpp::wire::pmr_carry_buffer carry_free{frame_bytes.size(), &parse_arena_free};
    fixpp::wire::Framer framer_free{};
    fixpp::wire::frame_view fvs_free[1]{};
    auto framed_free = framer_free.feed(
        std::span<const std::byte>{frame_bytes.data(), frame_bytes.size()}, carry_free,
        std::span<fixpp::wire::frame_view>{fvs_free, 1});
    ASSERT_TRUE(framed_free.has_value());
    ASSERT_FALSE(framed_free->empty());
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser_free{};
    auto mv_free = parser_free.parse(fvs_free[0], &parse_arena_free);
    ASSERT_TRUE(mv_free.has_value());
    ASSERT_FALSE(mv_free->is_dict_backed());

    InboundHandle h_free;
    h_free.msg.view = &(*mv_free);
    g_alloc_count.store(0);
    g_fail_at.store(-1);
    fixpp_msg_t* clone_free = nullptr;
    fixpp_error_t const rc_free = fixpp_msg_clone(h_free.ptr(), &clone_free);
    long const t_free = g_alloc_count.load();
    ASSERT_EQ(rc_free, FIXPP_ERR_OK) << "calibration: dict-free clone must succeed";
    ASSERT_NE(clone_free, nullptr);
    EXPECT_EQ(fixpp_msg_destroy(clone_free), FIXPP_ERR_OK);

    // ── Calibration pass 2: dict-BACKED clone (calls membership_copy()) ─────
    std::pmr::monotonic_buffer_resource parse_arena_dict;
    fixpp::wire::pmr_carry_buffer carry_dict{frame_bytes.size(), &parse_arena_dict};
    fixpp::wire::Framer framer_dict{};
    fixpp::wire::frame_view fvs_dict[1]{};
    auto framed_dict = framer_dict.feed(
        std::span<const std::byte>{frame_bytes.data(), frame_bytes.size()}, carry_dict,
        std::span<fixpp::wire::frame_view>{fvs_dict, 1});
    ASSERT_TRUE(framed_dict.has_value());
    ASSERT_FALSE(framed_dict->empty());
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser_dict{tv};
    auto mv_dict = parser_dict.parse(fvs_dict[0], &parse_arena_dict);
    ASSERT_TRUE(mv_dict.has_value());
    ASSERT_TRUE(mv_dict->is_dict_backed());

    InboundHandle h_dict;
    h_dict.msg.view = &(*mv_dict);

    g_alloc_count.store(0);
    g_fail_at.store(-1);
    fixpp_msg_t* clone_dict_calib = nullptr;
    fixpp_error_t const rc_dict_calib = fixpp_msg_clone(h_dict.ptr(), &clone_dict_calib);
    long const t_dict = g_alloc_count.load();
    ASSERT_EQ(rc_dict_calib, FIXPP_ERR_OK) << "calibration: dict-backed clone must succeed";
    ASSERT_NE(clone_dict_calib, nullptr);
    EXPECT_EQ(fixpp_msg_destroy(clone_dict_calib), FIXPP_ERR_OK);

    ASSERT_GT(t_dict, t_free)
        << "sanity: a dict-backed clone must allocate MORE than a dict-free clone "
           "(membership_copy()'s table_view deep-copy) -- else the injected pass below "
           "cannot be attributed to membership_copy() specifically";

    // ── Injected pass: fail_at = t_dict - 1 (the LAST allocation of the
    // dict-backed call BEFORE the final make_unique<MessageView<Index>>).
    // Per the file-header derivation this position is guaranteed inside
    // membership_copy()'s table_view copy ctor. ─────────────────────────────
    g_alloc_count.store(0);
    g_fail_at.store(t_dict - 1);
    fixpp_msg_t* clone_injected = nullptr;
    bool threw = false;
    fixpp_error_t rc_injected = FIXPP_ERR_UNKNOWN;
    try {
        rc_injected = fixpp_msg_clone(h_dict.ptr(), &clone_injected);
    } catch (...) {
        threw = true;
    }
    g_fail_at.store(-1);  // disarm before any further allocation (test teardown)

    EXPECT_FALSE(threw)
        << "gate-b/r1 FQ-1: a bad_alloc during membership_copy()'s table_view deep-copy "
           "must NOT propagate out of fixpp_msg_clone() -- must be caught by its own "
           "catch(...) and translated to FIXPP_ERR_CAPI_CONFIG_INVALID. Propagation here "
           "means membership_copy()'s noexcept was NOT removed (or the catch regressed).";
    EXPECT_EQ(rc_injected, FIXPP_ERR_CAPI_CONFIG_INVALID)
        << "a bad_alloc thrown during membership_copy()'s table_view deep-copy must be "
           "caught by fixpp_msg_clone's catch(...) and translated to "
           "FIXPP_ERR_CAPI_CONFIG_INVALID -- NOT std::terminate.";
    EXPECT_EQ(clone_injected, nullptr);
}
#endif  // !FIXPP_SANITIZER_REPLACES_NEW
