// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_dict066_grouped_read_alloc_guard.cpp
//
// 066-dict-backed-inbound-parse T013 — allocation-discipline gate for the
// dict-backed inbound parse+read path (SC-004/FR-004).
//
// FR-004: "the inbound parse+read path MUST remain free of new GLOBAL-heap
// allocation: the `table_view` is built once at `open()`; per-message
// membership lookups and lazily-built nested sub-views allocate only from
// the existing per-message stack parse arena (as the dictionary-backed
// typed-read path already does)."
//
// MEASUREMENT SCOPE — measured at the `Parser<Index>{tv}` + `OffsetTable`
// level, NOT through `Session::on_inbound_frame` / `ctest -R alloc_guard_
// dispatch|alloc_guard_session`'s coroutine-wrapped window:
// tests/alloc_guard/CMakeLists.txt documents (item 13 comment,
// "DELIBERATELY NOT GATED") that a window wrapping `co_spawn`/`ioc.run()`
// allocates coroutine frames + asio run-loop handlers on the global heap —
// gating THAT window at max-allocs=0 would be false-red, independent of
// anything 066 changed. `Session::parse_and_dispatch_` itself
// (session.cpp:298-330) is a plain `noexcept` function, NOT a coroutine — it
// is called synchronously from inside the session's coroutines. This file
// mirrors `parse_and_dispatch_`'s EXACT construction (stack array +
// `monotonic_buffer_resource` with `fixpp::detail::arena_upstream()` as
// upstream, `Parser<access_mode::Index>{tv}`) directly, honestly isolating
// what 066 actually changed (WHICH `table_view`/`Parser` feeds the root
// `OffsetTable`) from the session/coroutine machinery around it (same
// idiom as `tests/capi/recv_alloc_guard_test.cpp`'s synchronous trampoline
// measurement, and `tests/codegen/group_entry_alloc_gate_test.cpp` /
// `tests/dictionary/group_context_lookup_alloc_gate_test.cpp`'s
// Parser/OffsetTable-level dual gate).
//
// SCOPE NOTE (nested sub-views): this file's Test 2 exercises ONE nested
// descent (MassQuote `NoQuoteSets(296)` -> `NoQuoteEntries(295)`, via the
// REAL FIX44 dict) to confirm the flipped session-inbound path still routes
// nested sub-view builds through the arena end-to-end. The nested-descent
// ALGORITHM itself (bounded build-then-cache, cache-hit-vs-miss PMR-call
// cost) is UNCHANGED by 066 (066 only changes WHICH `table_view`/`Parser`
// feeds the root `OffsetTable`, not `OffsetTable::build_nested_subview`/
// `nested_group_slices`) and is already exhaustively witnessed by
// `tests/codegen/group_entry_alloc_gate_test.cpp`
// (`NestedFirstDescentBoundedRepeatZero`, `NestedMultiEntryWalkZeroAlloc`) —
// this file does not re-litigate that cache-hit/miss distinction.
//
// Dual gate (both required — a single gate false-passes per
// feedback_tracking_pmr_resource_false_pass):
//  (a) a TU-local global `operator new`/`operator new[]` counter (same shape
//      as tests/dictionary/group_context_lookup_alloc_gate_test.cpp /
//      tests/codegen/group_entry_alloc_gate_test.cpp), guarded under
//      `FIXPP_SANITIZER_REPLACES_NEW` so it compiles out under
//      ASan/TSan/MSan (feedback_operator_new_witness_breaks_sanitizers).
//  (b) mallocnesia LD_PRELOAD (tools/mallocnesia/libmallocnesia.so via
//      tools/check_alloc.py, the `_mallocnesia` ctest below) — intercepts
//      malloc/calloc/realloc globally.
//
// Mutation-proof (performed manually during implementation, NOT shipped as
// a permanent second test — same discipline as the precedent files above): a
// `new int` was temporarily inserted inside each measured window; both (a)
// the TU-local counter assertion and (b) the mallocnesia ctest were
// confirmed to go RED, then the injection was reverted and both confirmed
// GREEN again.
//
// Anchors: tasks.md T013; spec.md FR-004/SC-004; contracts/inbound-parse.md
// C5; src/session/session.cpp:271-330 (parse_and_dispatch_, the construction
// mirrored here).

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <memory_resource>
#include <string>
#include <string_view>

#include <fixpp/core/pmr_arena_upstream.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/parser.hpp>

#include "support/app_message_read_scaffold.hpp"  // fixpp_test_support::make_frame
#include "support/fix44_dictionary.hpp"
#include "support/fix44_group_frame_bodies.hpp"
#include "support/msvc_debug_arena_skip.hpp"  // FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_HEAP_GUARD

// mallocnesia replaces these weak no-ops with its interceptor scope markers.
#include "support/alloc_guard_markers.hpp"

// ── (a) TU-local global operator-new counter ────────────────────────────
// Same shape as tests/dictionary/group_context_lookup_alloc_gate_test.cpp.
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
}  // namespace

void* operator new(std::size_t size) {
    ++g_alloc_count;
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc{};
    return p;
}
void* operator new[](std::size_t size) {
    ++g_alloc_count;
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

using fixpp::wire::access_mode;
using fixpp::wire::Framer;
using fixpp::wire::frame_view;
using fixpp::wire::Parser;
using fixpp::wire::pmr_carry_buffer;

// Mirrors src/session/session.cpp:293-294/302-311 exactly.
constexpr std::size_t kInboundParseArena = 16384;

bool slice_has_tag(fixpp::wire::group_slice const& s, std::uint16_t tag) {
    std::string_view sv{reinterpret_cast<char const*>(s.data), s.len};
    std::string const needle = std::to_string(tag) + "=";
    if (sv.size() >= needle.size() && sv.substr(0, needle.size()) == needle) return true;
    std::string const soh_needle = std::string("\x01") + needle;
    return sv.find(soh_needle) != std::string_view::npos;
}

// One parse+read pass, mirroring parse_and_dispatch_'s exact arena shape.
// Returns true iff the frame parsed and the caller-supplied read callback's
// own correctness assertions (via ADD_FAILURE inside `read`) all held.
template <class ReadFn>
bool parse_and_read(fixpp::dict::table_view const& tv, std::vector<std::byte> const& raw,
                    ReadFn&& read) {
    std::array<std::byte, kInboundParseArena> pa_buf{};
    std::pmr::monotonic_buffer_resource pa_mr{pa_buf.data(), pa_buf.size(),
                                              ::fixpp::detail::arena_upstream()};
    std::array<std::byte, 512> carry_store{};
    std::pmr::monotonic_buffer_resource carry_mr{carry_store.data(), carry_store.size(),
                                                 ::fixpp::detail::arena_upstream()};
    pmr_carry_buffer carry{carry_store.size(), &carry_mr};
    Framer framer;
    std::array<frame_view, 1> out{};
    auto feed_r = framer.feed(std::span<const std::byte>{raw}, carry, std::span{out});
    if (!feed_r.has_value() || feed_r->empty()) return false;

    Parser<access_mode::Index> parser{tv};
    auto mv_r = parser.parse(out[0], &pa_mr);
    if (!mv_r.has_value()) return false;

    read(*mv_r);
    return true;
}

}  // namespace

// ── Test 1: top-level group (NoLegs) parse+read, zero GLOBAL heap ──────────
TEST(Dict066GroupedReadAllocGuard, TopLevelGroupParseAndReadZeroGlobalHeap) {
    FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_HEAP_GUARD();
    auto dict = fixpp::test_support::make_fix44_dictionary();
    auto tv = dict->as_table_view();

    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto raw = fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/2, "TW", "ISLD");

    auto do_read = [](auto const& mv) {
        auto slices = mv.offsets().group_slices(555);
        ASSERT_EQ(slices.size(), 2U);
        EXPECT_TRUE(slice_has_tag(slices[0], 600));
        EXPECT_TRUE(slice_has_tag(slices[1], 600));
        EXPECT_FALSE(slice_has_tag(slices[1], 60))
            << "trailing outer field must not be absorbed (066 correctness, "
               "unaffected by the alloc gate)";
    };

    // Warm-up (outside the window): prime any first-call lazy-init state.
    ASSERT_TRUE(parse_and_read(tv, raw, do_read));

#if !FIXPP_SANITIZER_REPLACES_NEW
    g_alloc_count.store(0, std::memory_order_relaxed);
#endif
    if (alloc_guard_start) alloc_guard_start();

    bool ok = parse_and_read(tv, raw, do_read);

    if (alloc_guard_end) alloc_guard_end();

    EXPECT_TRUE(ok);
#if !FIXPP_SANITIZER_REPLACES_NEW
    EXPECT_EQ(g_alloc_count.load(std::memory_order_relaxed), 0)
        << "top-level dict-backed group parse+read must not touch the global heap "
           "(FR-004: table_view built once, per-message reads from the stack arena)";
#endif
}

// Minimal hand-written stand-in for a generated `G_296` entry (NOT full
// codegen — avoids pulling the codegen build-tree dependency into
// tests/alloc_guard purely to prove a nested descent's global-heap
// discipline). Meets exactly the contract `group_view<GroupT>::operator[]`
// requires (constructible from `entry_context`, group_view.hpp:143-150) and
// exposes ONE nested-descent accessor via the same public
// `OffsetTable::nested_group_slices` entry point a generated `quote_
// sets()[i].quote_entries()` accessor would call.
struct MinimalQuoteSetEntry {
    fixpp::wire::entry_context ctx;
    explicit MinimalQuoteSetEntry(fixpp::wire::entry_context c) noexcept : ctx(c) {}

    [[nodiscard]] std::span<fixpp::wire::group_slice const> quote_entries() const noexcept {
        return ctx.parent_cache_owner->nested_group_slices(
            ctx.span.data(), ctx.span.size(), /*nested_no_tag=*/295, ctx.opaque_dict,
            ctx.group_member_fn, ctx.gen, ctx.group_ctx);
    }
};

// ── Test 2: nested descent (MassQuote NoQuoteSets->NoQuoteEntries) via the
// SAME REAL FIX44 dict, zero GLOBAL heap end-to-end through the flipped path.
// See file header SCOPE NOTE — this does not re-litigate cache-hit/miss cost,
// only that 066's flip doesn't introduce a global-heap escape on this path.
TEST(Dict066GroupedReadAllocGuard, NestedGroupParseAndReadZeroGlobalHeap) {
    FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_HEAP_GUARD();
    auto dict = fixpp::test_support::make_fix44_dictionary();
    auto tv = dict->as_table_view();

    std::string body =
        "35=i\x01"
        "117=Q1\x01"
        "296=1\x01"
        "302=SET0\x01"
        "295=1\x01"
        "299=E0\x01"
        "132=10.10\x01"
        "133=10.20\x01";
    auto raw = fixpp_test_support::make_frame("FIX.4.4", body);

    auto do_read = [](auto const& mv) {
        auto sets = mv.template group<296, MinimalQuoteSetEntry>();
        ASSERT_EQ(sets.size(), 1U);
        auto entry = sets[0];
        auto inner = entry.quote_entries();
        ASSERT_EQ(inner.size(), 1U);
        EXPECT_TRUE(slice_has_tag(inner[0], 299));
    };

    // Warm-up (outside the window).
    ASSERT_TRUE(parse_and_read(tv, raw, do_read));

#if !FIXPP_SANITIZER_REPLACES_NEW
    g_alloc_count.store(0, std::memory_order_relaxed);
#endif
    if (alloc_guard_start) alloc_guard_start();

    bool ok = parse_and_read(tv, raw, do_read);

    if (alloc_guard_end) alloc_guard_end();

    EXPECT_TRUE(ok);
#if !FIXPP_SANITIZER_REPLACES_NEW
    EXPECT_EQ(g_alloc_count.load(std::memory_order_relaxed), 0)
        << "nested descent through the flipped dict-backed session-inbound path "
           "must not touch the global heap (sub-views draw only from the arena)";
#endif
}
