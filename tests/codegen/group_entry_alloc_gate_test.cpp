// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/group_entry_alloc_gate_test.cpp — 062 T019 [US3]
//
// FR-004 / FR-004a / FR-004b / SC-002: allocation-discipline gate for the
// GENERATED group-entry typed-read path, over GENERATED flyweights (not a
// hand-written stub).
//
// Two witnesses:
//  - OneLevelScalarZeroAlloc: one-level scalar entry reads (NewOrderList
//    `orders()` / G_73, same message as tests/codegen/group_entry_read_test.cpp)
//    must NOT allocate at all (no per-entry sub-index build).
//  - NestedFirstDescentBoundedRepeatZero: the FIRST descent into a nested
//    group (MassQuote `NoQuoteSets(296) -> NoQuoteEntries(295)`, single-entry
//    nesting only — multi-entry nested reads are blocked by the pre-existing
//    Defect B, deferred to 063, see tests/codegen/nested_group_read_test.cpp)
//    may perform its bounded arena/PMR sub-view build; a REPEAT descent into
//    the SAME outer occurrence must allocate ZERO (cache hit).
//
// Dual gate (both required — a single gate false-passes per the
// tracking-PMR-resource lesson, feedback_tracking_pmr_resource_false_pass):
//  (a) counting: a `counting_resource` PMR wrapper (same shape as
//      tests/perf/test_store_alloc_guard.cpp) around a stack-backed
//      std::pmr::monotonic_buffer_resource with a NULL upstream (any overrun
//      is a hard failure — mirrors tests/alloc_guard/wire_alloc_guard_test.cpp).
//  (b) global-malloc interception: mallocnesia via
//      tools/mallocnesia/libmallocnesia.so + tools/check_alloc.py, gated by
//      the `_mallocnesia` ctest registered in tests/codegen/CMakeLists.txt
//      (skipped if the .so is absent).
//
// The measured window (between alloc_guard_start()/alloc_guard_end()) is
// placed strictly around the READ path; the frame + arenas + top-level
// group-slice materialization (`orders()`/`quote_sets()`) happen BEFORE the
// window so only entry reads / nested descent are measured. Expected decimal
// values are pre-computed from an UNTRACKED arena so the comparison itself
// never pollutes the measured counting_resource.
//
// 063 T023 — NestedMultiEntryWalkZeroAlloc (below) proves the Defect-B
// multi-entry nested extent walk (OffsetTable::consume_group_extent, 063
// T021 — pure stack-only index recursion, [const §VIII.5]) allocates ZERO
// GLOBAL heap. Per the task brief this witness uses the TU-local global
// operator-new counter + mallocnesia markers (NOT a tracking-PMR resource) —
// same dual-gate shape as
// tests/dictionary/group_context_lookup_alloc_gate_test.cpp — since the
// concern here is any escape to the global heap, not PMR-routed accounting
// (the descent's own bounded PMR sub-view build is expected/allowed, FR-004b).
//
// Mutation-proof (performed manually during implementation, NOT shipped as a
// permanent second test): a `new int` was temporarily inserted inside
// NestedMultiEntryWalkZeroAlloc's measured window; both (a) the TU-local
// counter assertion and (b) the mallocnesia ctest
// (codegen_group_entry_alloc_gate_test_mallocnesia) were confirmed to go
// RED, then the injection was reverted and both confirmed GREEN again.

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// mallocnesia replaces these weak no-ops with its interceptor scope markers.
#include <utility>

#include "support/alloc_guard_markers.hpp"
#include "support/group73_table_view.hpp"

// ── 063 T023: TU-local global operator-new counter ──────────────────────
// Same shape as tests/dictionary/group_context_lookup_alloc_gate_test.cpp —
// guarded under FIXPP_SANITIZER_REPLACES_NEW so it compiles out under
// ASan/TSan/MSan (which ship their own strong operator new).
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
std::atomic<long> g_nested_extent_alloc_count{0};
}  // namespace

void* operator new(std::size_t size) {
    ++g_nested_extent_alloc_count;
    void* p = std::malloc(size);
    if (!p) {
        throw std::bad_alloc{};
    }
    return p;
}

void* operator new[](std::size_t size) {
    ++g_nested_extent_alloc_count;
    void* p = std::malloc(size);
    if (!p) {
        throw std::bad_alloc{};
    }
    return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif  // !FIXPP_SANITIZER_REPLACES_NEW

namespace {

using fixpp::decimal_t;

// Same shape as tests/perf/test_store_alloc_guard.cpp's counting_resource:
// forwards every allocate()/deallocate() to an upstream resource while
// counting allocate() calls. Combined with a stack-backed, NULL-upstream
// monotonic_buffer_resource as the forwarding target, an over-budget
// allocation is a hard failure (bad_alloc) in addition to being counted.
class counting_resource final : public std::pmr::memory_resource {
public:
    explicit counting_resource(std::pmr::memory_resource* upstream) noexcept : upstream_(upstream) {}

    [[nodiscard]] long long allocate_count() const noexcept {
        return count_.load(std::memory_order_relaxed);
    }
    void reset_count() noexcept { count_.store(0, std::memory_order_relaxed); }

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        count_.fetch_add(1, std::memory_order_relaxed);
        return upstream_->allocate(bytes, align);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) noexcept override {
        upstream_->deallocate(p, bytes, align);
    }
    [[nodiscard]] bool do_is_equal(std::pmr::memory_resource const& other) const noexcept override {
        return this == &other;
    }
    std::pmr::memory_resource* upstream_;
    mutable std::atomic<long long> count_{0};
};

// Build a well-formed FIX frame: "8=FIX.4.4<SOH> 9=<len><SOH> <body> 10=<chk><SOH>"
// body must already begin with "35=X<SOH>" and contain SOH-delimited fields.
std::vector<std::byte> make_frame(std::string_view body) {
    std::string pre =
        "8=FIX.4.4\x01" + std::string("9=") + std::to_string(body.size()) + "\x01" +
        std::string(body);
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    char checksum[16]{};
    std::snprintf(checksum, sizeof(checksum), "10=%03u\x01", sum % 256U);
    std::string full = pre + checksum;
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

// Parse a raw FIX frame into a MessageView using the production Framer.
//
// 220: DICT-AWARE. The old comment here said "dict-free — same as production
// session.cpp Parser<Index> pd_parser{}", which was already stale (066
// dict-backed the session's inbound parse) and is now also unusable: group()
// is a dictionary-only operation, so a dict-free parse yields no typed group
// and OneLevelScalarZeroAlloc's orders() would be empty. The shared table_view
// is built ONCE in a function-local static, outside any alloc-gated region.
MV parse_frame(std::vector<std::byte> const& buf, std::pmr::memory_resource* mr) {
    fixpp::wire::pmr_carry_buffer carry{buf.size(), mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs, 1});
    EXPECT_TRUE(framed.has_value()) << "Framer::feed failed";
    EXPECT_FALSE(framed->empty()) << "Framer produced no frames";
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{
        fixpp_test_support::group73_table_view()};
    auto mv = parser.parse((*framed)[0], mr);
    if (!mv.has_value()) {
        ADD_FAILURE() << "dict-aware parse failed";
        return MV{(*framed)[0], mr};
    }
    return std::move(*mv);
}

decimal_t parse_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto r = decimal_t::parse(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()}, mr);
    EXPECT_TRUE(r.has_value()) << "parse_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

// Hand-built MassQuote group membership for a SINGLE-entry-per-occurrence
// nesting (NoQuoteSets(296) -> NoQuoteEntries(295)) — same FIX44.xml
// component defs as tests/codegen/nested_group_read_test.cpp's
// make_correct_massquote_dict(), trimmed to only what this one-level-of-
// nesting witness needs (no Legs(555) descent required here).
fixpp::dict::table_view make_massquote_dict() {
    fixpp::dict::table_view dict;
    dict.add_group_member(296, 302)   // QuoteSetID
        .add_group_member(296, 295)   // NoQuoteEntries (nested group's own count field)
        .add_group_member(296, 299)   // QuoteEntryID — transitively under 296
        .add_group_member(296, 132)   // BidPx — transitively under 296
        .add_group_member(296, 133)   // OfferPx — transitively under 296
        .add_group_member(295, 299)   // QuoteEntryID — NoQuoteEntries' own delimiter
        .add_group_member(295, 132)   // BidPx — direct member of 295
        .add_group_member(295, 133);  // OfferPx — direct member of 295
    return dict;
}

}  // namespace

// FR-004 / FR-004a — one-level scalar entry reads over a GENERATED flyweight
// (NewOrderList orders() / G_73) must not allocate at all. Two entries with
// DISTINCT values (a per-instance bug that always reads entry 0 would fail
// the value assertions, which stay live regardless of the alloc gate).
TEST(GroupEntryAllocGate, OneLevelScalarZeroAlloc) {
    std::string body =
        "35=E\x01"
        "73=2\x01"
        "11=CLORD-0\x01"
        "37=ORDID-0\x01"
        "38=100.5\x01"
        "54=1\x01"
        "11=CLORD-1\x01"
        "37=ORDID-1\x01"
        "38=200.25\x01"
        "54=2\x01";

    // Untracked arena for the "expected" decimal values, computed BEFORE the
    // measured window so the comparison never touches the counted resource.
    std::pmr::monotonic_buffer_resource exp_arena{4096};
    decimal_t const qty0_expected = parse_decimal("100.5", &exp_arena);
    decimal_t const qty1_expected = parse_decimal("200.25", &exp_arena);

    std::array<std::byte, 8192> storage{};
    std::pmr::monotonic_buffer_resource backing{storage.data(), storage.size(),
                                                std::pmr::null_memory_resource()};
    counting_resource counted{&backing};

    auto buf = make_frame(body);
    auto mv = parse_frame(buf, &counted);

    fixpp::v44::NewOrderList nol{mv};
    auto orders = nol.orders();  // materializes group_slices(73) — BEFORE the window
    ASSERT_EQ(orders.size(), 2U);
    auto entry0 = orders[0];
    auto entry1 = orders[1];

    counted.reset_count();  // only the read path below is measured

    if (alloc_guard_start) alloc_guard_start();

    auto cl0 = entry0.cl_ord_id();
    ASSERT_TRUE(cl0.has_value());
    EXPECT_EQ(*cl0, "CLORD-0");
    auto oid0 = entry0.order_id();
    ASSERT_TRUE(oid0.has_value());
    EXPECT_EQ(*oid0, "ORDID-0");
    auto sd0 = entry0.side();
    ASSERT_TRUE(sd0.has_value());
    EXPECT_EQ(*sd0, '1');
    auto qty0 = entry0.order_qty(&counted);
    ASSERT_TRUE(qty0.has_value());
    EXPECT_EQ(*qty0, qty0_expected);

    auto cl1 = entry1.cl_ord_id();
    ASSERT_TRUE(cl1.has_value());
    EXPECT_EQ(*cl1, "CLORD-1");
    auto oid1 = entry1.order_id();
    ASSERT_TRUE(oid1.has_value());
    EXPECT_EQ(*oid1, "ORDID-1");
    auto sd1 = entry1.side();
    ASSERT_TRUE(sd1.has_value());
    EXPECT_EQ(*sd1, '2');
    auto qty1 = entry1.order_qty(&counted);
    ASSERT_TRUE(qty1.has_value());
    EXPECT_EQ(*qty1, qty1_expected);

    if (alloc_guard_end) alloc_guard_end();

    EXPECT_EQ(counted.allocate_count(), 0)
        << "one-level scalar entry reads must not allocate and must not build "
           "a per-entry sub-index (FR-004/FR-004a)";
}

// FR-004b / SC-002 — first nested descent (MassQuote NoQuoteSets ->
// NoQuoteEntries, single-entry-per-occurrence, per the 062 US2 scope
// boundary) may perform ONE bounded arena/PMR build; a repeat descent into
// the SAME outer occurrence must allocate ZERO (cache hit). Field values are
// asserted on BOTH the first and the repeat descent so the cache-hit path is
// proven functionally correct, not merely cheap.
TEST(GroupEntryAllocGate, NestedFirstDescentBoundedRepeatZero) {
    std::string body =
        "35=i\x01"
        "296=1\x01"
        "302=SET0\x01"
        "295=1\x01"
        "299=E0\x01"
        "132=10.10\x01"
        "133=10.20\x01";

    auto dict = make_massquote_dict();

    std::pmr::monotonic_buffer_resource exp_arena{4096};
    decimal_t const bid_expected = parse_decimal("10.10", &exp_arena);
    decimal_t const offer_expected = parse_decimal("10.20", &exp_arena);

    std::array<std::byte, 8192> storage{};
    std::pmr::monotonic_buffer_resource backing{storage.data(), storage.size(),
                                                std::pmr::null_memory_resource()};
    counting_resource counted{&backing};

    auto buf = make_frame(body);
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &counted};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());

    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{dict};
    auto mv_exp = parser.parse((*framed)[0], &counted);
    ASSERT_TRUE(mv_exp.has_value());

    fixpp::v44::MassQuote mq{*mv_exp};
    auto sets = mq.quote_sets();  // materializes group_slices(296) — BEFORE the window
    ASSERT_EQ(sets.size(), 1U);
    auto quote_set0 = sets[0];

    counted.reset_count();  // only the descent below is measured

    if (alloc_guard_start) alloc_guard_start();

    // FIRST descent: builds + caches the nested sub-OffsetTable for
    // (quote_set0's slice, 295).
    auto entries_first = quote_set0.quote_entries();
    ASSERT_EQ(entries_first.size(), 1U);
    auto entry_first = entries_first[0];
    auto bid_first = entry_first.bid_px(&counted);
    ASSERT_TRUE(bid_first.has_value());
    EXPECT_EQ(*bid_first, bid_expected);
    auto offer_first = entry_first.offer_px(&counted);
    ASSERT_TRUE(offer_first.has_value());
    EXPECT_EQ(*offer_first, offer_expected);

    long long const after_first = counted.allocate_count();

    // REPEAT descent on the SAME outer occurrence: must be a cache hit.
    auto entries_repeat = quote_set0.quote_entries();
    ASSERT_EQ(entries_repeat.size(), 1U);
    auto entry_repeat = entries_repeat[0];
    auto bid_repeat = entry_repeat.bid_px(&counted);
    ASSERT_TRUE(bid_repeat.has_value());
    EXPECT_EQ(*bid_repeat, bid_expected);
    auto offer_repeat = entry_repeat.offer_px(&counted);
    ASSERT_TRUE(offer_repeat.has_value());
    EXPECT_EQ(*offer_repeat, offer_expected);

    if (alloc_guard_end) alloc_guard_end();

    EXPECT_GT(after_first, 0)
        << "first nested descent is expected to perform its bounded arena/PMR "
           "build against the current build_nested_subview() implementation "
           "(FR-004b) — informative, not a hard spec requirement (FR-004b "
           "says the build MAY happen)";
    EXPECT_EQ(counted.allocate_count(), after_first)
        << "repeat nested descent on the SAME outer occurrence must allocate "
           "ZERO beyond the first descent (cache hit, FR-004b/SC-002)";
}

// 063 T023 — Defect-B multi-entry nested extent walk (consume_group_extent)
// must perform ZERO global-heap allocation. Same frame shape as the
// pre-063-escalated tests/codegen/nested_group_read_test.cpp
// `NestedQuoteEntriesPerInstancePrices` case (now correctly handled): ONE
// QuoteSet holding TWO QuoteEntries (nested count 295=2). Dual gate per the
// file header: TU-local global operator-new counter (NOT a tracking-PMR
// resource) + the whole-binary mallocnesia re-run
// (codegen_group_entry_alloc_gate_test_mallocnesia).
TEST(GroupEntryAllocGate, NestedMultiEntryWalkZeroAlloc) {
    std::string body =
        "35=i\x01"
        "296=1\x01"
        "302=SET0\x01"
        "295=2\x01"
        "299=E0\x01"
        "132=10.10\x01"
        "133=10.20\x01"
        "299=E1\x01"
        "132=20.10\x01"
        "133=20.20\x01";

    auto dict = make_massquote_dict();

    // Untracked arena for expected decimal values, computed BEFORE the
    // measured window.
    std::pmr::monotonic_buffer_resource exp_arena{4096};
    decimal_t const bid0_expected = parse_decimal("10.10", &exp_arena);
    decimal_t const offer0_expected = parse_decimal("10.20", &exp_arena);
    decimal_t const bid1_expected = parse_decimal("20.10", &exp_arena);
    decimal_t const offer1_expected = parse_decimal("20.20", &exp_arena);

    // Stack-backed, NULL-upstream arena for the message itself — any overrun
    // is a hard bad_alloc failure, so a passing test also guarantees no
    // PMR-side overflow to the global heap.
    std::array<std::byte, 8192> storage{};
    std::pmr::monotonic_buffer_resource backing{storage.data(), storage.size(),
                                                std::pmr::null_memory_resource()};

    auto buf = make_frame(body);
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &backing};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());

    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{dict};
    auto mv_exp = parser.parse((*framed)[0], &backing);
    ASSERT_TRUE(mv_exp.has_value());

    fixpp::v44::MassQuote mq{*mv_exp};
    auto sets = mq.quote_sets();  // materializes group_slices(296) — BEFORE the window
    ASSERT_EQ(sets.size(), 1U);
    auto quote_set0 = sets[0];

#if !FIXPP_SANITIZER_REPLACES_NEW
    g_nested_extent_alloc_count.store(0, std::memory_order_relaxed);
#endif
    if (alloc_guard_start) alloc_guard_start();

    // Exercises the multi-entry nested extent walk (consume_group_extent) —
    // the outer occurrence's extent must enclose BOTH QuoteEntries.
    auto entries = quote_set0.quote_entries();
    ASSERT_EQ(entries.size(), 2U) << "063 Defect-B fix: the nesting-aware extent walk must not "
                                     "truncate the 2nd QuoteEntry";

    auto entry0 = entries[0];
    auto bid0 = entry0.bid_px(&backing);
    ASSERT_TRUE(bid0.has_value());
    EXPECT_EQ(*bid0, bid0_expected);
    auto offer0 = entry0.offer_px(&backing);
    ASSERT_TRUE(offer0.has_value());
    EXPECT_EQ(*offer0, offer0_expected);

    auto entry1 = entries[1];
    auto bid1 = entry1.bid_px(&backing);
    ASSERT_TRUE(bid1.has_value());
    EXPECT_EQ(*bid1, bid1_expected);
    auto offer1 = entry1.offer_px(&backing);
    ASSERT_TRUE(offer1.has_value());
    EXPECT_EQ(*offer1, offer1_expected);

    if (alloc_guard_end) alloc_guard_end();

#if !FIXPP_SANITIZER_REPLACES_NEW
    EXPECT_EQ(g_nested_extent_alloc_count.load(std::memory_order_relaxed), 0)
        << "multi-entry nested extent walk (consume_group_extent) must be alloc-free "
           "(stack-only index recursion, [const §VIII.5]) — zero GLOBAL heap escape";
#endif
}
