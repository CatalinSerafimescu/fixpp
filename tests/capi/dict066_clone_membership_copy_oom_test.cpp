// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/dict066_clone_membership_copy_oom_test.cpp
//
// Superseded in part by `.specify/495-493-486-dict-reify-copy.md` §2.3/§2.4 (fixpp#495).
//
// gate-b/r1 FQ-1 (PR #181 round 1, Finding 1) — OOM hardening witness for
// the borrowed-route table copy: MessageView::shared_membership() copies a
// borrowed-route source's table in place (include/fixpp/wire/parser.hpp), and
// that copy may throw: `fixpp_msg_clone()`'s production caller
// (`fixpp_msg_clone()`'s src/capi/message_write.cpp definition, inside the inner
// `catch (std::bad_alloc const&)` of its nested boundary) must translate a
// bad_alloc thrown during the table_view deep-copy into
// FIXPP_ERR_CAPI_CONFIG_INVALID, NOT std::terminate.
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
// Calibration: `fixpp_msg_clone()`'s construction body (src/capi/message_write.cpp)
// sits in an INNER try/catch(std::bad_alloc const&), itself inside an OUTER
// catch(...) that aborts (fixpp#458 D-3b). Premise, stated as a condition
// (fixpp#495, `.specify/495-493-486-dict-reify-copy.md` §10 T-16(b)): a
// BORROWED-route source makes the dict-backed branch copy the table in place
// through `MessageView::shared_membership()`, after which exactly ONE further
// global-new call remains before the inner try block ends — the
// `std::make_unique<MessageView<Index>>` of the parsed view (the re-parse draws
// from the clone's pre-seeded arena). So position `dict_total - 1` is the LAST
// allocation inside the table_view copy constructor (as long as K>=1, confirmed
// by the T_dict>T_free sanity check below). Re-derive the landing site with
// `gdb -batch -ex "catch throw" -ex run -ex bt` on this binary.
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "capi_internal.hpp"  // engine-internal fixpp_msg (test-only access)
#include "fix/c_api/error.h"
#include "fix/c_api/message.h"
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

// ── libstdc++ gate (gate-b CI-fix, PR #181 Tier 2 MSVC + Tier 3 libc++). ─────
// The fault-injection ordinal below (see file header "Calibration") is derived
// from a libstdc++-specific GLOBAL-allocation sequence: it assumes exactly ONE
// further global-new call remains after the table copy before the try ends.
// libc++ (Tier 3) and MSVC's STL (Tier 2) allocate a different number/order of
// internal blocks, so `t_dict - 1` no longer lands inside the table copy's
// table_view copy and the witness mis-fires. The behaviour it guards
// (the table copy is not noexcept + std::unique_ptr<fixpp_msg> RAII in
// fixpp_msg_clone) is a source-level guarantee independent of the STL and is
// mutation-proven on libstdc++ (and the RAII clone success-path is covered
// under libc++/MSVC by capi_dict066_clone_identity + the libc++-ASan lane), so
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
// gate-b/r2 FQ-1: a dedicated LIVE-object counter, distinct from
// g_alloc_count. g_alloc_count is bumped by the fault-injector BEFORE the
// fail_at check and BEFORE std::malloc, so the faulting call itself never
// allocates/frees anything -- a `g_alloc_count - g_free_count` net would
// carry a phantom +1 through both pre- and post-fix runs. g_live is
// incremented ONLY after a successful std::malloc (i.e. below the
// fail_at throw), so it tracks real live heap objects and must return to
// its pre-call baseline once the RAII fix deletes the leaked clone shell
// on unwind.
std::atomic<long> g_live{0};
}  // namespace

void* operator new(std::size_t size) {
    long const n = ++g_alloc_count;
    if (n == g_fail_at.load(std::memory_order_relaxed)) {
        throw std::bad_alloc{};
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc{};
    ++g_live;
    return p;
}
void* operator new[](std::size_t size) {
    long const n = ++g_alloc_count;
    if (n == g_fail_at.load(std::memory_order_relaxed)) {
        throw std::bad_alloc{};
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc{};
    ++g_live;
    return p;
}
void operator delete(void* p) noexcept {
    if (p != nullptr) --g_live;
    std::free(p);
}
void operator delete[](void* p) noexcept {
    if (p != nullptr) --g_live;
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    if (p != nullptr) --g_live;
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    if (p != nullptr) --g_live;
    std::free(p);
}

// fixpp#458 (090-capi-refusals) T049: the ALIGNED overloads. Measured (not
// assumed): libstdc++'s std::pmr::monotonic_buffer_resource routes its
// upstream fallback (new_delete_resource()) through
// `::operator new(size, std::align_val_t)` UNCONDITIONALLY, never the plain
// overload above -- so without these, the entries_/overlay_ pmr::vector
// growth this file's NEW OOM arm (CloneReparseOom) needs to inject into is
// invisible to g_alloc_count/g_fail_at entirely, and the injected ordinal
// can only ever land inside the table copy's (plain-new) allocations.
// NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)
// A replaceable global operator new/delete must obtain and release raw storage
// itself; RAII and gsl::owner<> do not apply to the allocator's own definition.
void* operator new(std::size_t size, std::align_val_t al) {
    long const n = ++g_alloc_count;
    if (n == g_fail_at.load(std::memory_order_relaxed)) {
        throw std::bad_alloc{};
    }
    std::size_t const alignment = static_cast<std::size_t>(al);
    std::size_t const rounded = ((size + alignment - 1) / alignment) * alignment;
    void* p = std::aligned_alloc(alignment, rounded);
    if (!p) throw std::bad_alloc{};
    ++g_live;
    return p;
}
void* operator new[](std::size_t size, std::align_val_t al) {
    long const n = ++g_alloc_count;
    if (n == g_fail_at.load(std::memory_order_relaxed)) {
        throw std::bad_alloc{};
    }
    std::size_t const alignment = static_cast<std::size_t>(al);
    std::size_t const rounded = ((size + alignment - 1) / alignment) * alignment;
    void* p = std::aligned_alloc(alignment, rounded);
    if (!p) throw std::bad_alloc{};
    ++g_live;
    return p;
}
void operator delete(void* p, std::align_val_t) noexcept {
    if (p != nullptr) --g_live;
    std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    if (p != nullptr) --g_live;
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    if (p != nullptr) --g_live;
    std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    if (p != nullptr) --g_live;
    std::free(p);
}
// NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)
#endif  // FIXPP_OOM_WITNESS_ENABLED

namespace {

struct InboundHandle {
    fixpp_msg msg{};
    const fixpp_msg_t* ptr() const noexcept { return reinterpret_cast<const fixpp_msg_t*>(&msg); }
};

}  // namespace

#if FIXPP_OOM_WITNESS_ENABLED
TEST(CloneMembershipCopyOom, TableViewCopyOomYieldsCapiConfigInvalid) {
    auto dict = fixpp::test_support::make_fix44_dictionary();
    auto tv = dict->as_table_view();

    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto frame_bytes =
        fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/9, "SENDER", "TARGET");

    // ── Calibration pass 1: dict-FREE clone (no table copy) ─────
    std::pmr::monotonic_buffer_resource parse_arena_free;
    fixpp::wire::pmr_carry_buffer carry_free{frame_bytes.size(), &parse_arena_free};
    fixpp::wire::Framer framer_free{};
    fixpp::wire::frame_view fvs_free[1]{};
    auto framed_free =
        framer_free.feed(std::span<const std::byte>{frame_bytes.data(), frame_bytes.size()},
                         carry_free, std::span<fixpp::wire::frame_view>{fvs_free, 1});
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

    // ── Calibration pass 2: dict-BACKED clone (copies the table) ─────
    std::pmr::monotonic_buffer_resource parse_arena_dict;
    fixpp::wire::pmr_carry_buffer carry_dict{frame_bytes.size(), &parse_arena_dict};
    fixpp::wire::Framer framer_dict{};
    fixpp::wire::frame_view fvs_dict[1]{};
    auto framed_dict =
        framer_dict.feed(std::span<const std::byte>{frame_bytes.data(), frame_bytes.size()},
                         carry_dict, std::span<fixpp::wire::frame_view>{fvs_dict, 1});
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
           "(the table_view deep-copy) -- else the injected pass below "
           "cannot be attributed to the table copy specifically";

    // ── Injected pass: fail_at = t_dict - 1 (the LAST allocation of the
    // dict-backed call BEFORE the final make_unique<MessageView<Index>>).
    // Per the file-header derivation this position is guaranteed inside
    // the table_view copy ctor. ─────────────────────────────
    g_alloc_count.store(0);
    g_fail_at.store(t_dict - 1);
    long const live_before = g_live.load();
    fixpp_msg_t* clone_injected = nullptr;
    bool threw = false;
    fixpp_error_t rc_injected = FIXPP_ERR_UNKNOWN;
    try {
        rc_injected = fixpp_msg_clone(h_dict.ptr(), &clone_injected);
    } catch (...) {
        threw = true;
    }
    g_fail_at.store(-1);  // disarm before any further allocation (test teardown)
    long const live_after = g_live.load();

    // gate-b/r2 FQ-1: the OOM path must not leak the partially-built clone
    // shell (or its arena_buf_ / arena_resource_ members). Pre-fix (raw
    // `new fixpp_msg{}` with no delete on the catch(...) unwind), live_after
    // is strictly greater than live_before. Post-fix (RAII
    // std::unique_ptr<fixpp_msg>), the implicit destructor runs on unwind
    // and live_after == live_before.
    EXPECT_EQ(live_after, live_before)
        << "gate-b/r2 FQ-1: fixpp_msg_clone() leaked " << (live_after - live_before)
        << " live heap object(s) on the table-copy OOM path -- the clone shell "
           "(and/or its arena_buf_/arena_resource_ members) must be freed via RAII on "
           "the catch(...) unwind.";

    EXPECT_FALSE(threw)
        << "gate-b/r1 FQ-1: a bad_alloc during the table_view deep-copy "
           "must NOT propagate out of fixpp_msg_clone() -- must be caught by its inner "
           "catch(std::bad_alloc const&) and translated to FIXPP_ERR_CAPI_CONFIG_INVALID. "
           "Propagation here means the copy path became noexcept (or the catch regressed).";
    EXPECT_EQ(rc_injected, FIXPP_ERR_CAPI_CONFIG_INVALID)
        << "a bad_alloc thrown during the table_view deep-copy must be "
           "caught by fixpp_msg_clone's inner catch(std::bad_alloc const&) and translated to "
           "FIXPP_ERR_CAPI_CONFIG_INVALID -- NOT std::terminate.";
    EXPECT_EQ(clone_injected, nullptr);
}

// ── fixpp#458 (090-capi-refusals) T049 (part 2 of 2 — see
// tests/capi/message_write_test.cpp for the raised-cap and malformed-field
// arms): EC-3's "failing-allocator route" ──────────────────────────────────
//
// A bad_alloc during the CLONE's OWN dict-backed re-parse (Parser::parse ->
// OffsetTable::build, allocating from the clone's per-message arena,
// `clone_mr`) is caught INTERNALLY by OffsetTable::build's own
// `catch (std::bad_alloc const&)` (src/wire/offset_table.cpp) and degrades to
// `status_ = fail(core::error::out_of_memory)` -- a DIFFERENT catch site from
// the arm above (which catches a bad_alloc that ESCAPES all the way to
// clone's own boundary, inside the table copy). translate() then maps
// core::error::out_of_memory -> FIXPP_ERR_UNKNOWN (documented v1.0 behaviour,
// L-049-2 -- spec.md FR-006/FR-007).
//
// Mechanism: `clone_mr` is a monotonic_buffer_resource sized
// `frame_len + 4096` with upstream `new_delete_resource()`, so ordinary frame
// content can never starve it outright (the upstream is effectively
// unbounded) -- the injection targets the SAME global-operator-new override
// this file already installs, at an ordinal INSIDE OffsetTable::build()'s own
// allocations (the entries_/overlay_ pmr::vector growth reallocations that
// spill past the arena's initial block once it is exhausted), not at
// the table copy's tail (that ordinal produces EC-4/CAPI_CONFIG_INVALID
// instead -- the discriminator assertion below tells the two apart).
//
// A LARGE frame (3000 repeats of a plain field) forces this spillover:
// entries_ alone needs ~3000*12=36000B at capacity, well past the ~16KB
// initial arena, so several of its growth reallocations request fresh blocks
// from upstream (global operator new) -- unlike the small calibration frame
// above (2 legs, ~20 fields), which never spills.
TEST(CloneReparseOom, OffsetTableBuildOomYieldsUnknown) {
    auto dict = fixpp::test_support::make_fix44_dictionary();
    auto tv = dict->as_table_view();

    std::string big_suffix;
    constexpr int kRepeats = 3000;
    big_suffix.reserve(static_cast<std::size_t>(kRepeats) * 4);
    for (int i = 0; i < kRepeats; ++i) {
        big_suffix += "58=x\x01";
    }
    auto frame_bytes =
        fixpp_test_support::make_execution_report_frame(big_suffix, /*seq=*/9, "SENDER", "TARGET");

    // ── Calibration pass: dict-backed clone of the BIG frame, no injection ──
    std::pmr::monotonic_buffer_resource parse_arena;
    fixpp::wire::pmr_carry_buffer carry{frame_bytes.size(), &parse_arena};
    fixpp::wire::Framer framer{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = framer.feed(std::span<const std::byte>{frame_bytes.data(), frame_bytes.size()},
                              carry, std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{tv};
    auto mv = parser.parse(fvs[0], &parse_arena);
    ASSERT_TRUE(mv.has_value());
    ASSERT_TRUE(mv->is_dict_backed());

    InboundHandle h;
    h.msg.view = &(*mv);

    // gate-b/r1 (G-4): FR-005/B-458-1's source-usable postcondition, on THIS
    // route.
    auto assert_source_intact = [&] {
        const char* mt = nullptr;
        size_t mt_len = 0;
        ASSERT_EQ(fixpp_msg_get_msg_type(h.ptr(), &mt, &mt_len), FIXPP_ERR_OK);
        ASSERT_NE(mt, nullptr);
        EXPECT_EQ(std::string_view(mt, mt_len), "8");

        const char* sv = nullptr;
        size_t sv_len = 0;
        ASSERT_EQ(fixpp_msg_get_string(h.ptr(), 49, &sv, &sv_len), FIXPP_ERR_OK);
        ASSERT_NE(sv, nullptr);
        EXPECT_EQ(std::string_view(sv, sv_len), "SENDER");
    };
    assert_source_intact();  // pre-condition: the lookup succeeds BEFORE either clone call

    g_alloc_count.store(0);
    g_fail_at.store(-1);
    fixpp_msg_t* clone_calib = nullptr;
    fixpp_error_t const rc_calib = fixpp_msg_clone(h.ptr(), &clone_calib);
    long const t_big = g_alloc_count.load();
    ASSERT_EQ(rc_calib, FIXPP_ERR_OK)
        << "calibration: a clean dict-backed clone of the big frame must succeed";
    ASSERT_NE(clone_calib, nullptr);
    EXPECT_EQ(fixpp_msg_destroy(clone_calib), FIXPP_ERR_OK);

    // ── Injected pass: fail at the LAST allocation before the final
    // make_unique<MessageView<Index>> (same recipe as the calibration above);
    // for THIS big frame that ordinal lands inside OffsetTable::build()'s own
    // spillover allocations, not the table copy's tail. ──────────────────
    g_alloc_count.store(0);
    g_fail_at.store(t_big - 1);
    fixpp_msg_t* clone_injected = nullptr;
    bool threw = false;
    fixpp_error_t rc_injected = FIXPP_ERR_OK;
    try {
        rc_injected = fixpp_msg_clone(h.ptr(), &clone_injected);
    } catch (...) {
        threw = true;
    }
    g_fail_at.store(-1);  // disarm before any further allocation (test teardown)

    assert_source_intact();  // FR-005: source unchanged and still usable AFTER the refusal

    EXPECT_FALSE(threw) << "a bad_alloc inside OffsetTable::build() must be caught INTERNALLY "
                           "(OffsetTable::build's own catch) and never propagate";
    // Discriminator: FIXPP_ERR_UNKNOWN confirms the injected ordinal landed
    // inside the re-parse's OffsetTable::build (EC-3's failing-allocator
    // route); FIXPP_ERR_CAPI_CONFIG_INVALID would mean it landed in
    // the table copy instead (EC-4) -- the frame would need to be bigger.
    // ⚠️ RED on the unfixed tree is NOT "wrong catch site" — it is the SAME
    // fail-open fallback T047/T049's sibling cells hit: OffsetTable::build's
    // internal catch degrades `parsed` to `!has_value()`, and the unfixed
    // `if (!clone_view) { ... }` arm treats that identically to a dict-free
    // source, silently returning a dict-free clone with FIXPP_ERR_OK.
    EXPECT_EQ(rc_injected, FIXPP_ERR_UNKNOWN)
        << "a bad_alloc during the clone's OWN dict-backed re-parse (OffsetTable::build, "
           "clone_mr's spillover to global operator new) must be caught internally by "
           "OffsetTable::build and surfaced as translate(out_of_memory) == FIXPP_ERR_UNKNOWN "
           "(EC-3 / FR-006 / SC-005) -- got "
        << static_cast<int>(rc_injected)
        << " (CAPI_CONFIG_INVALID==10 would mean the injected ordinal landed in "
           "the table copy instead; the frame needs to be bigger).";
    EXPECT_EQ(clone_injected, nullptr);
    if (clone_injected != nullptr) {
        EXPECT_EQ(fixpp_msg_destroy(clone_injected), FIXPP_ERR_OK);
    }
}
#endif  // FIXPP_OOM_WITNESS_ENABLED
