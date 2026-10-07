// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/test_body_builder.cpp
//
// 061-typed-app-messages (061-slim) T005/T006 direct unit test — RED-first
// witness for wire::body_builder (the pivotal shared write primitive).
//
// Named tests (data-model.md §1 INV-2/3/4/5; contracts/builder-shape-oracle.md
// C1-C4):
//   FlatMessage_ByteExact                    — C1: "35=<mt>\x01<fields>\x01",
//                                               author order, byte-exact.
//   Inv2_FramingTagRejected                  — INV-2: field(8|9|34|49|52|56|10)
//                                               -> typed error.
//   Inv3_DecimalCanonicalBytes                — INV-3: decimal_t canonical bytes.
//   GroupCountPrecedence_TwoInstances         — C3: No<G>=N before N instances.
//   NestedTwoLevel_ByteExact                  — 2-level LIFO threading.
//   NestedThreeLevel_ByteExact                — 3-level LIFO threading (E shape).
//   CountZero_PresentButEmpty                 — C3: No<G>=0 present (mutation-
//                                               proven).
//   NeverOpened_NoTagEmitted                  — distinct from count-zero: no
//                                               No<G> tag at all.
//   EmptyInstance_Reject                      — INV-5 leg (a) (mutation-proven).
//   WrongDelimiterFirst_Reject                — INV-5 leg (b).
//   GroupStillOpen_Reject                     — INV-4: unbalanced LIFO.
//   OverCap_BufferUntouched                   — INV-4: > kBodyCap -> untouched.
//   UndersizedOut_BufferUntouched              -- INV-4: out too small -> untouched.
//   NoGlobalHeap_CountingNew                    -- 061-slim rework: zero global
//                                               `::operator new` across construction
//                                               + flat fields + 3-level nested group +
//                                               commit (honest gate -- a PMR-only
//                                               counter would false-pass a nested
//                                               container that escapes to global heap;
//                                               [[feedback_tracking_pmr_resource_false_pass]]).
//
// Anchors: specs/061-typed-app-messages/data-model.md §1;
//          specs/061-typed-app-messages/contracts/builder-shape-oracle.md.

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/body_builder.hpp>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/body_builder_test_helpers.hpp"
#include "support/frame_view_factory.hpp"

// -- Global operator new counter --------------------------------------------
// Honest zero-global-heap gate (mirrors
// tests/session/test_business_messages_build.cpp's Builder_NoHeap_CountingResource
// exactly -- [[feedback_tracking_pmr_resource_false_pass]]: a PMR-only
// counting_resource would false-pass a nested container that silently
// escapes to global ::operator new). Compiled out under ASan/TSan/MSan
// (replacement conflicts with the sanitizers' own operator new); mallocnesia
// LD_PRELOAD is the CI-tier cross-check.
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

std::atomic<long> g_bb_alloc_count{0};

}  // namespace

void* operator new(std::size_t size) {
    ++g_bb_alloc_count;
    void* p = std::malloc(size);
    if (!p) {
        throw std::bad_alloc{};
    }
    return p;
}

void* operator new[](std::size_t size) {
    ++g_bb_alloc_count;
    void* p = std::malloc(size);
    if (!p) {
        throw std::bad_alloc{};
    }
    return p;
}

// GCC's -Wmismatched-new-delete pairs std::free with the STANDARD operator new;
// it cannot see that the replacement operator new above allocates with
// std::malloc, so the matching std::free below is flagged although it is correct.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif  // !FIXPP_SANITIZER_REPLACES_NEW

namespace {

using fixpp::decimal_t;
using fixpp::wire::body_builder;
using namespace fixpp::test_support::body_builder_helpers;

// Parse a decimal_t from an ASCII literal (no string-literal ctor, matches
// tests/session/test_business_messages_build.cpp precedent).
decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

}  // namespace

// ── C1: flat message, byte-exact, author order ──────────────────────────────
TEST(BodyBuilder, FlatMessage_ByteExact) {
    body_builder bb{"X"};
    ASSERT_TRUE(bb.field(11, std::string_view{"CLORD1"}).has_value());
    ASSERT_TRUE(bb.field(54, '1').has_value());
    ASSERT_TRUE(bb.field(38, std::int64_t{100}).has_value());

    std::array<std::byte, kBufSize> buf{};
    auto r = bb.commit(std::span<std::byte>{buf});
    ASSERT_TRUE(r.has_value());

    std::string expected =
        "35=X\x01"
        "11=CLORD1\x01"
        "54=1\x01"
        "38=100\x01";
    EXPECT_EQ(bytes_to_string(*r), expected);
}

// ── INV-2: framing tags rejected at field() ─────────────────────────────────
TEST(BodyBuilder, Inv2_FramingTagRejected) {
    for (std::uint16_t tag : {8, 9, 34, 49, 52, 56, 10}) {
        body_builder bb{"X"};
        auto r = bb.field(tag, std::string_view{"whatever"});
        EXPECT_FALSE(r.has_value()) << "tag " << tag << " must be rejected";
    }
}

// ── C1/INV-2: MsgType(35) framing-injection rejected at commit() (gate-b/r2
// RC#2) ──────────────────────────────────────────────────────────────────────
// Every field VALUE routed through append_string_field gets the
// is_clean_field_value guard (see Inv2_FramingTagRejected above for the
// field()-level SOH/framing-tag reject); the MsgType(35) framing value itself
// -- copied verbatim from the ctor arg into "35=<msg_type_>\x01" at commit()
// -- was the one value-source that skipped it. A crafted msg_type containing
// SOH + a forged tag (`"X\x0149=EVIL"`) would splice `49=EVIL` in right after
// the MsgType, forging a framing tag past the body boundary (C1/INV-2).
TEST(BodyBuilder, Inv2_MsgTypeFramingInjectionRejected) {
    body_builder bb{
        "X\x01"
        "49=EVIL"};
    ASSERT_TRUE(bb.field(11, std::string_view{"CLORD1"}).has_value());

    std::array<std::byte, kBufSize> buf{};
    fill_sentinel(buf);
    auto r = bb.commit(std::span<std::byte>{buf});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::wire_field_value_out_of_range);
    EXPECT_TRUE(all_sentinel(buf)) << "INV-4: out must be untouched on failure";
}

// ── C1/INV-2: empty MsgType(35) rejected at commit() (gate-b/r2 RC#2) ───────
// is_clean_field_value("") is true (an empty range vacuously satisfies
// all_of), so the non-empty check is a SEPARATE leg from the clean-value
// check -- without it, an empty msg_type would emit the malformed "35=\x01".
TEST(BodyBuilder, Inv2_EmptyMsgTypeRejected) {
    body_builder bb{""};
    ASSERT_TRUE(bb.field(11, std::string_view{"CLORD1"}).has_value());

    std::array<std::byte, kBufSize> buf{};
    fill_sentinel(buf);
    auto r = bb.commit(std::span<std::byte>{buf});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::wire_field_value_out_of_range);
    EXPECT_TRUE(all_sentinel(buf)) << "INV-4: out must be untouched on failure";
}

// ── INV-3: decimal canonical bytes (direct byte compare, C2) ────────────────
// decimal_t canonicalizes at parse time (trailing zeros stripped: "190.50"
// and "190.5" store identically, tests/session/test_business_messages_build.cpp
// precedent), so the emitted bytes are the minimal canonical form "190.5",
// not an echo of the "190.50" input -- this pins the canonical *format*
// (INV-3) independently of any by-value comparison.
TEST(BodyBuilder, Inv3_DecimalCanonicalBytes) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto price = make_decimal("190.50", &arena);

    body_builder bb{"X"};
    ASSERT_TRUE(bb.field(44, price).has_value());

    std::array<std::byte, kBufSize> buf{};
    auto r = bb.commit(std::span<std::byte>{buf});
    ASSERT_TRUE(r.has_value());

    EXPECT_EQ(bytes_to_string(*r),
              "35=X\x01"
              "44=190.5\x01");
}

// ── C3: group count-precedence, 2 instances ─────────────────────────────────
TEST(BodyBuilder, GroupCountPrecedence_TwoInstances) {
    body_builder bb{"X"};
    auto g = bb.group_begin(453, 448);
    ASSERT_TRUE(g.has_value());

    auto e1 = g->add_entry();
    ASSERT_TRUE(e1.has_value());
    ASSERT_TRUE(e1->set_string(448, "P1").has_value());

    auto e2 = g->add_entry();
    ASSERT_TRUE(e2.has_value());
    ASSERT_TRUE(e2->set_string(448, "P2").has_value());

    ASSERT_TRUE(bb.group_end(*g).has_value());

    std::array<std::byte, kBufSize> buf{};
    auto r = bb.commit(std::span<std::byte>{buf});
    ASSERT_TRUE(r.has_value());

    EXPECT_EQ(bytes_to_string(*r),
              "35=X\x01"
              "453=2\x01"
              "448=P1\x01"
              "448=P2\x01");
}

// ── Nested 2-level LIFO, byte-exact ──────────────────────────────────────────
TEST(BodyBuilder, NestedTwoLevel_ByteExact) {
    body_builder bb{"X"};
    auto parties = bb.group_begin(453, 448);
    ASSERT_TRUE(parties.has_value());

    auto party = parties->add_entry();
    ASSERT_TRUE(party.has_value());
    ASSERT_TRUE(party->set_string(448, "P1").has_value());

    auto subids = party->group_begin(802, 523);
    ASSERT_TRUE(subids.has_value());
    auto sub = subids->add_entry();
    ASSERT_TRUE(sub.has_value());
    ASSERT_TRUE(sub->set_string(523, "S1").has_value());
    ASSERT_TRUE(bb.group_end(*subids).has_value());

    ASSERT_TRUE(bb.group_end(*parties).has_value());

    std::array<std::byte, kBufSize> buf{};
    auto r = bb.commit(std::span<std::byte>{buf});
    ASSERT_TRUE(r.has_value());

    EXPECT_EQ(bytes_to_string(*r),
              "35=X\x01"
              "453=1\x01"
              "448=P1\x01"
              "802=1\x01"
              "523=S1\x01");
}

// ── Nested 3-level LIFO, byte-exact (E shape: 73->453->802) ─────────────────
TEST(BodyBuilder, NestedThreeLevel_ByteExact) {
    body_builder bb{"E"};
    auto orders = bb.group_begin(73, 11);
    ASSERT_TRUE(orders.has_value());

    auto order = orders->add_entry();
    ASSERT_TRUE(order.has_value());
    ASSERT_TRUE(order->set_string(11, "ORD1").has_value());

    auto parties = order->group_begin(453, 448);
    ASSERT_TRUE(parties.has_value());
    auto party = parties->add_entry();
    ASSERT_TRUE(party.has_value());
    ASSERT_TRUE(party->set_string(448, "PID1").has_value());

    auto subids = party->group_begin(802, 523);
    ASSERT_TRUE(subids.has_value());
    auto sub = subids->add_entry();
    ASSERT_TRUE(sub.has_value());
    ASSERT_TRUE(sub->set_string(523, "SUB1").has_value());

    // LIFO close: innermost first.
    ASSERT_TRUE(bb.group_end(*subids).has_value());
    ASSERT_TRUE(bb.group_end(*parties).has_value());
    ASSERT_TRUE(bb.group_end(*orders).has_value());

    std::array<std::byte, kBufSize> buf{};
    auto r = bb.commit(std::span<std::byte>{buf});
    ASSERT_TRUE(r.has_value());

    EXPECT_EQ(bytes_to_string(*r),
              "35=E\x01"
              "73=1\x01"
              "11=ORD1\x01"
              "453=1\x01"
              "448=PID1\x01"
              "802=1\x01"
              "523=SUB1\x01");
}

// ── Count-of-zero: present-but-empty group ──────────────────────────────────
// Mutation-proven: verified this assertion goes RED when the production
// serializer is changed to skip emitting a zero-instance group's No<G> tag
// (see body_builder.cpp::commit -> serialize_entries; manually verified
// during implementation, reverted before commit).
TEST(BodyBuilder, CountZero_PresentButEmpty) {
    body_builder bb{"X"};
    auto g = bb.group_begin(453, 448);
    ASSERT_TRUE(g.has_value());
    ASSERT_TRUE(bb.group_end(*g).has_value());

    std::array<std::byte, kBufSize> buf{};
    auto r = bb.commit(std::span<std::byte>{buf});
    ASSERT_TRUE(r.has_value());

    EXPECT_EQ(bytes_to_string(*r),
              "35=X\x01"
              "453=0\x01");
}

// ── Never-opened: distinct from count-zero — no tag emitted at all ─────────
TEST(BodyBuilder, NeverOpened_NoTagEmitted) {
    body_builder bb{"X"};
    ASSERT_TRUE(bb.field(11, std::string_view{"CLORD1"}).has_value());

    std::array<std::byte, kBufSize> buf{};
    auto r = bb.commit(std::span<std::byte>{buf});
    ASSERT_TRUE(r.has_value());

    std::string body = bytes_to_string(*r);
    EXPECT_EQ(body,
              "35=X\x01"
              "11=CLORD1\x01");
    EXPECT_EQ(body.find("453="), std::string::npos) << "optional group never opened -> no No-tag";
}

// ── INV-5 leg (a): empty instance rejected at commit() ──────────────────────
// Mutation-proven: verified RED when the `inst.fields.empty()` guard is
// removed from body_builder.cpp::validate_group_grammar (manually verified
// during implementation, reverted before commit).
TEST(BodyBuilder, EmptyInstance_Reject) {
    body_builder bb{"X"};
    auto g = bb.group_begin(453, 448);
    ASSERT_TRUE(g.has_value());

    auto e = g->add_entry();
    ASSERT_TRUE(e.has_value());
    // No set_* call on `e` -> empty instance.

    ASSERT_TRUE(bb.group_end(*g).has_value());

    std::array<std::byte, kBufSize> buf{};
    fill_sentinel(buf);
    auto r = bb.commit(std::span<std::byte>{buf});
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(all_sentinel(buf)) << "INV-4: out must be untouched on failure";
}

// ── INV-5 leg (b): wrong first field rejected at commit() ───────────────────
TEST(BodyBuilder, WrongDelimiterFirst_Reject) {
    body_builder bb{"X"};
    auto g = bb.group_begin(453, 448);
    ASSERT_TRUE(g.has_value());

    auto e = g->add_entry();
    ASSERT_TRUE(e.has_value());
    // First field set is 447 (PartyIDSource), NOT the delimiter 448.
    ASSERT_TRUE(e->set_string(447, "D").has_value());
    ASSERT_TRUE(e->set_string(448, "P1").has_value());

    ASSERT_TRUE(bb.group_end(*g).has_value());

    std::array<std::byte, kBufSize> buf{};
    fill_sentinel(buf);
    auto r = bb.commit(std::span<std::byte>{buf});
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(all_sentinel(buf)) << "INV-4: out must be untouched on failure";
}

// ── INV-4: commit() with a group still open ─────────────────────────────────
TEST(BodyBuilder, GroupStillOpen_Reject) {
    body_builder bb{"X"};
    auto g = bb.group_begin(453, 448);
    ASSERT_TRUE(g.has_value());
    auto e = g->add_entry();
    ASSERT_TRUE(e.has_value());
    ASSERT_TRUE(e->set_string(448, "P1").has_value());
    // group_end() intentionally never called.

    std::array<std::byte, kBufSize> buf{};
    fill_sentinel(buf);
    auto r = bb.commit(std::span<std::byte>{buf});
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(all_sentinel(buf)) << "INV-4: out must be untouched on failure";
}

// ── INV-4: over-cap (> kBodyCap) fails closed, out untouched ─────────────────
TEST(BodyBuilder, OverCap_BufferUntouched) {
    body_builder bb{"X"};
    // A single field whose value alone exceeds the 3800 B internal cap.
    std::string huge(4000, 'A');
    ASSERT_TRUE(bb.field(58, std::string_view{huge}).has_value());

    // `out` is generously sized (16 KiB) -- the cap is absolute, independent
    // of the caller's buffer size.
    std::array<std::byte, 16384> buf{};
    fill_sentinel(std::span<std::byte>{buf});
    auto r = bb.commit(std::span<std::byte>{buf});
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(all_sentinel(std::span<const std::byte>{buf}))
        << "INV-4: out must be untouched when the internal cap is exceeded";
}

// ── INV-4: undersized `out` fails closed, untouched ─────────────────────────
TEST(BodyBuilder, UndersizedOut_BufferUntouched) {
    body_builder bb{"X"};
    ASSERT_TRUE(bb.field(11, std::string_view{"CLORD1"}).has_value());
    ASSERT_TRUE(bb.field(54, '1').has_value());

    // The body needs 5 + 11 + 5 = 21 bytes ("35=X\x01" + "11=CLORD1\x01" +
    // "54=1\x01"); 4 bytes is deliberately too small.
    std::array<std::byte, 4> buf{};
    fill_sentinel(std::span<std::byte>{buf});
    auto r = bb.commit(std::span<std::byte>{buf});
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(all_sentinel(std::span<const std::byte>{buf}))
        << "INV-4: out must be untouched when undersized";
}

// -- 061-slim rework: zero global ::operator new across the accumulation
// path ---------------------------------------------------------------------
// Builds a REPRESENTATIVE message inside the zero-alloc window: flat fields
// AND a 3-level nested group (the E shape), exercising every accumulation
// container body_builder owns -- entries_, open_stack_, entry_node::value_bytes
// (scalar payload), entry_node::instances (group payload), and
// group_instance::fields (nested entries) -- so a nested container silently
// wired to the default global-heap-backed pmr resource cannot hide behind an
// untested path. body_builder itself is constructed INSIDE the window with a
// short (SSO) msg_type so construction stays zero-alloc too.
//
// A PMR-only counting_resource would false-pass a nested container that
// escapes to global heap ([[feedback_tracking_pmr_resource_false_pass]]) --
// this is the honest gate: intercept global ::operator new directly.
//
// RED->GREEN mutation proof (manually verified during implementation,
// reverted before commit): temporarily reverting entry_node::instances (the
// group-payload container, nested two levels deep in this test's shape) from
// std::pmr::vector<group_instance> back to a plain std::vector<group_instance>
// makes this test RED (nonzero alloc delta), proving the witness actually
// discriminates a scoped-allocator-propagation bug instead of false-passing.
TEST(BodyBuilder, NoGlobalHeap_CountingNew) {
#if FIXPP_SANITIZER_REPLACES_NEW
    GTEST_SKIP() << "global operator new replacement is incompatible with ASan "
                    "(alloc-dealloc-mismatch) and TSan (multiple-definition of operator new); "
                    "the zero-alloc witness runs in debug/release/ubsan, with mallocnesia "
                    "LD_PRELOAD as the CI-tier cross-check";
#elif defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
    // MSVC debug STL only (_ITERATOR_DEBUG_LEVEL != 0). Under iterator debugging
    // every std::pmr::vector heap-allocates a hidden _Container_proxy through
    // GLOBAL ::operator new at construction (not through its pmr resource), so
    // constructing body_builder's std::pmr::vector members bumps this global-new
    // counter by one per container -- an artifact of the debug STL, not a real
    // builder allocation. libstdc++/libc++ and MSVC RELEASE
    // (_ITERATOR_DEBUG_LEVEL == 0) allocate nothing in a pmr-vector ctor, so the
    // heap-free contract is proven there PLUS by the mallocnesia LD_PRELOAD
    // perf_*_alloc_guard cross-check on the CI tiers.
    // See tests/support/msvc_debug_arena_skip.hpp (same _Container_proxy cause).
    GTEST_SKIP() << "MSVC debug STL allocates a per-container _Container_proxy via global "
                    "operator new at pmr-vector construction (debug-STL artifact); the "
                    "heap-free builder contract is covered on all non-MSVC-debug lanes and "
                    "by the mallocnesia LD_PRELOAD cross-check";
#else
    std::array<std::byte, kBufSize> buf{};

    // -- Zero-alloc window: construction + flat fields + 3-level nested group + commit --
    long before = g_bb_alloc_count.load(std::memory_order_relaxed);

    body_builder bb{"E"};
    auto flat_r = bb.field(11, std::string_view{"ORD1"});

    auto orders = bb.group_begin(73, 11);
    auto order = orders->add_entry();
    auto order_field_r = order->set_string(11, "ORD1");

    auto parties = order->group_begin(453, 448);
    auto party = parties->add_entry();
    auto party_field_r = party->set_string(448, "PID1");

    auto subids = party->group_begin(802, 523);
    auto sub = subids->add_entry();
    auto sub_field_r = sub->set_string(523, "SUB1");

    auto end1 = bb.group_end(*subids);
    auto end2 = bb.group_end(*parties);
    auto end3 = bb.group_end(*orders);

    auto r = bb.commit(std::span<std::byte>{buf});

    long after = g_bb_alloc_count.load(std::memory_order_relaxed);
    // -- End zero-alloc window --

    ASSERT_TRUE(flat_r.has_value());
    ASSERT_TRUE(orders.has_value());
    ASSERT_TRUE(order.has_value());
    ASSERT_TRUE(order_field_r.has_value());
    ASSERT_TRUE(parties.has_value());
    ASSERT_TRUE(party.has_value());
    ASSERT_TRUE(party_field_r.has_value());
    ASSERT_TRUE(subids.has_value());
    ASSERT_TRUE(sub.has_value());
    ASSERT_TRUE(sub_field_r.has_value());
    ASSERT_TRUE(end1.has_value());
    ASSERT_TRUE(end2.has_value());
    ASSERT_TRUE(end3.has_value());
    ASSERT_TRUE(r.has_value()) << "commit must succeed for the alloc witness to be meaningful";

    EXPECT_EQ(after, before)
        << "body_builder construction + field()/group_begin()/add_entry()/set_string()/"
           "group_end()/commit() must not call global ::operator new (alloc delta = "
        << (after - before) << "); mallocnesia LD_PRELOAD is the CI-tier cross-check";
#endif  // FIXPP_SANITIZER_REPLACES_NEW
}

// -- 061-slim rework: null-upstream arena exhaustion fails closed -----------
// Not in the brief's named-test list, but directly validates the design's
// central promise (kArenaCap's null upstream ⇒ std::bad_alloc ⇒ typed
// wire_frame_too_large, never a global-heap fallback, never a noexcept
// std::terminate) -- otherwise the catch(const std::bad_alloc&) blocks added
// to field()/group_begin()/add_entry()/group_begin() have zero test coverage.
// Feeds field() calls (distinct non-framing tags, tiny values) well past
// kArenaCap until one fails; asserts it fails closed with the typed error,
// the process does not terminate, and the builder is still safely usable
// afterward (a further commit() call also fails, cleanly).
TEST(BodyBuilder, ArenaExhaustion_FailsClosedNoTerminate) {
    body_builder bb{"X"};

    bool saw_failure = false;
    fixpp::core::error failure_error{};
    // 1..4000 comfortably exceeds kArenaCap (16384 B) worth of entry_node
    // overhead + value bytes; framing tags (8,9,34,49,52,56,10) are skipped.
    for (std::uint16_t tag = 1; tag <= 4000 && !saw_failure; ++tag) {
        if (tag == 8 || tag == 9 || tag == 34 || tag == 49 || tag == 52 || tag == 56 || tag == 10)
            continue;
        auto r = bb.field(tag, std::string_view{"1"});
        if (!r.has_value()) {
            saw_failure = true;
            failure_error = r.error();
        }
    }

    ASSERT_TRUE(saw_failure) << "expected arena exhaustion to fail-close within 4000 fields";
    EXPECT_EQ(failure_error, fixpp::core::error::wire_frame_too_large);

    // Builder remains safely usable post-exhaustion: a further commit() call
    // completes cleanly (no crash, no UB) -- whether it succeeds (the
    // successfully-accumulated fields serialize under kBodyCap) or fails
    // closed (kBodyCap exceeded) depends on how many fields fit before arena
    // exhaustion; either outcome is INV-4-consistent, so this only asserts
    // the call itself is safe to make.
    std::array<std::byte, kBufSize> buf{};
    fill_sentinel(std::span<std::byte>{buf});
    (void)bb.commit(std::span<std::byte>{buf});
}

// -- gate-b/r1 RC#2: default (null-owner) handles fail-closed, not UB ------
// group_handle/entry_handle have PUBLIC default ctors (owner_ == nullptr).
// Every forwarding op must reject rather than deref the null owner_.
// Mutation-proof: reverting either `owner_ == nullptr` guard back out
// reintroduces a null-pointer dereference (UB/crash under ASan), not a typed
// error -- this test would fail to compile-time-detect that regression via
// assertion alone, but a local revert-and-rerun under ASan crashes instead of
// returning EXPECT_FALSE, confirming the guard is load-bearing.
TEST(BodyBuilder, DefaultHandle_NullOwner_FailsClosed) {
    fixpp::wire::group_handle default_group{};
    auto add_r = default_group.add_entry();
    EXPECT_FALSE(add_r.has_value());
    EXPECT_EQ(add_r.error(), fixpp::core::error::wire_invalid_field_format);

    fixpp::wire::entry_handle default_entry{};
    auto set_str_r = default_entry.set_string(11, "x");
    EXPECT_FALSE(set_str_r.has_value());
    EXPECT_EQ(set_str_r.error(), fixpp::core::error::wire_invalid_field_format);

    auto set_char_r = default_entry.set_char(11, 'x');
    EXPECT_FALSE(set_char_r.has_value());
    EXPECT_EQ(set_char_r.error(), fixpp::core::error::wire_invalid_field_format);

    auto set_int_r = default_entry.set_int(11, 42);
    EXPECT_FALSE(set_int_r.has_value());
    EXPECT_EQ(set_int_r.error(), fixpp::core::error::wire_invalid_field_format);

    decimal_t px = make_decimal("1.5", std::pmr::get_default_resource());
    auto set_dec_r = default_entry.set_decimal(44, px);
    EXPECT_FALSE(set_dec_r.has_value());
    EXPECT_EQ(set_dec_r.error(), fixpp::core::error::wire_invalid_field_format);

    auto nested_r = default_entry.group_begin(802, 523);
    EXPECT_FALSE(nested_r.has_value());
    EXPECT_EQ(nested_r.error(), fixpp::core::error::wire_invalid_field_format);
}

// -- gate-b/r3: a closed group_handle must not append another instance -------
// Mutation-proven: removing the add_entry_impl() innermost-open guard turns
// this RED->GREEN regression back on; the stale add_entry() succeeds after
// group_end(), creating an empty post-close instance that later fails only at
// commit() grammar validation.
TEST(BodyBuilder, ClosedGroupHandle_AddEntryRejected) {
    body_builder bb{"X"};
    auto g = bb.group_begin(453, 448);
    ASSERT_TRUE(g.has_value());

    auto e1 = g->add_entry();
    ASSERT_TRUE(e1.has_value());
    ASSERT_TRUE(e1->set_string(448, "P1").has_value());
    ASSERT_TRUE(bb.group_end(*g).has_value());

    auto stale_add = g->add_entry();
    EXPECT_FALSE(stale_add.has_value());
    EXPECT_EQ(stale_add.error(), fixpp::core::error::wire_invalid_field_format);
}

// -- gate-b/r3: a closed entry_handle must not append fields after close -----
// Load-bearing leg from triage: grammar does NOT catch this one. Without the
// new innermost-open guard, the stale set_string() succeeds, commit() stays
// GREEN, and the already-closed instance silently serializes the extra field.
TEST(BodyBuilder, ClosedEntryHandle_SetStringRejectedAndCommitUntouched) {
    body_builder bb{"X"};
    auto g = bb.group_begin(453, 448);
    ASSERT_TRUE(g.has_value());

    auto e1 = g->add_entry();
    ASSERT_TRUE(e1.has_value());
    ASSERT_TRUE(e1->set_string(448, "P1").has_value());
    ASSERT_TRUE(bb.group_end(*g).has_value());

    auto stale_set = e1->set_string(447, "D");
    EXPECT_FALSE(stale_set.has_value());
    EXPECT_EQ(stale_set.error(), fixpp::core::error::wire_invalid_field_format);

    std::array<std::byte, kBufSize> buf{};
    fill_sentinel(buf);
    auto commit_r = bb.commit(std::span<std::byte>{buf});
    ASSERT_TRUE(commit_r.has_value());
    EXPECT_FALSE(all_sentinel(buf))
        << "commit() should still serialize the pre-close fields after rejecting the stale write";
}

// -- gate-b/r3: STRICT innermost-open, not weaker present-anywhere ----------
// While a nested group is still open, the outer entry handle is stale for
// mutation purposes and must fail closed; a weaker \"present anywhere in the
// stack\" check would wrongly accept this interleaving.
TEST(BodyBuilder, OuterEntryMutationRejectedWhileInnerGroupOpen) {
    body_builder bb{"X"};
    auto orders_g = bb.group_begin(73, 11);
    ASSERT_TRUE(orders_g.has_value());

    auto order_e = orders_g->add_entry();
    ASSERT_TRUE(order_e.has_value());
    ASSERT_TRUE(order_e->set_string(11, "ORD1").has_value());

    auto parties_g = order_e->group_begin(453, 448);
    ASSERT_TRUE(parties_g.has_value());

    auto stale_outer_set = order_e->set_string(55, "AAPL");
    EXPECT_FALSE(stale_outer_set.has_value());
    EXPECT_EQ(stale_outer_set.error(), fixpp::core::error::wire_invalid_field_format);

    auto party_e = parties_g->add_entry();
    ASSERT_TRUE(party_e.has_value());
    ASSERT_TRUE(party_e->set_string(448, "P1").has_value());
    ASSERT_TRUE(bb.group_end(*parties_g).has_value());
    ASSERT_TRUE(order_e->set_string(55, "AAPL").has_value());
    ASSERT_TRUE(bb.group_end(*orders_g).has_value());
}

// -- gate-b/r1 RC#2: cross-builder group_end() must reject, not corrupt -----
// Every body_builder starts next_open_seq_ at 1, so builder A's FIRST handle
// (open_seq_ == 1) numerically collides with builder B's FIRST handle
// (open_seq_ == 1) -- Codex's counter-test. Before the `handle.owner_ ==
// this` guard, `b.group_end(*ga)` succeeded (popped B's open_stack_ using A's
// handle), silently closing the wrong builder's group.
TEST(BodyBuilder, CrossBuilder_GroupEnd_Rejected) {
    body_builder a{"A"};
    body_builder b{"B"};

    auto ga = a.group_begin(453, 448);
    ASSERT_TRUE(ga.has_value());
    auto gb = b.group_begin(73, 11);
    ASSERT_TRUE(gb.has_value());

    // Sanity: both handles' open_seq_ is 1 (the collision precondition).
    auto end_wrong = b.group_end(*ga);
    EXPECT_FALSE(end_wrong.has_value())
        << "builder B must reject builder A's handle, even with a colliding open_seq_";
    EXPECT_EQ(end_wrong.error(), fixpp::core::error::wire_invalid_field_format);

    // B's own group is still open (untouched by the rejected cross-builder call).
    EXPECT_TRUE(b.group_end(*gb).has_value());
    // A's own group is still open too.
    EXPECT_TRUE(a.group_end(*ga).has_value());
}

// ═════════════════════════════════════════════════════════════════════════
// 091-data-field-bytes (fixpp#418) — contract C-1: body_builder::field_data,
// entry_handle::set_data, and commit()'s Length+Data pair check (INV-6).
// Clause text: specs/091-data-field-bytes/contracts/body-builder-data.md.
// Refusal codes: spec.md FR-004a.
//
// Oracle for every refusal and rollback case (C-1.4, C-1.4b, C-1.5, C-1.6):
// the subject builder commits after the refused call, an identical twin that
// never made the call commits too, and the two bodies are byte-identical. The
// three conditions are asserted unconditionally, so a stray node left by a
// failed call shows either as a refused subject commit or as a byte difference.
//
// Group tags used by the arrangements carry no Length or Data half:
// NoAllocs(78) with delimiter AllocAccount(79), and NoPartyIDs(453) with
// delimiter PartyID(448) nested inside it.
// ═════════════════════════════════════════════════════════════════════════

namespace {

using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::wire::entry_handle;
using fixpp::wire::group_handle;

constexpr std::uint16_t kOuterNo = 78;
constexpr std::uint16_t kOuterDelim = 79;
constexpr std::uint16_t kInnerNo = 453;
constexpr std::uint16_t kInnerDelim = 448;
constexpr std::uint16_t kTextTag = 58;

// The handles an arrangement leaves behind for the call under test.
struct scene {
    group_handle outer;
    entry_handle outer_entry;
    group_handle inner;
    entry_handle inner_entry;
};

// Top-level arrangement: a field before the call and a field after it.
void arrange_top(body_builder& b, scene&) {
    ASSERT_TRUE(b.field(11, std::string_view{"ORD1"}).has_value());
}
void finish_top(body_builder& b, scene&) {
    ASSERT_TRUE(b.field(kTextTag, std::string_view{"END"}).has_value());
}

// Live innermost entry: a group opened, an entry added and its delimiter set,
// so the instance is non-empty and delimiter-first and the no-call twin commits.
void arrange_live_entry(body_builder& b, scene& s) {
    auto g = b.group_begin(kOuterNo, kOuterDelim);
    ASSERT_TRUE(g.has_value());
    s.outer = *g;
    auto e = s.outer.add_entry();
    ASSERT_TRUE(e.has_value());
    s.outer_entry = *e;
    ASSERT_TRUE(s.outer_entry.set_string(kOuterDelim, "A1").has_value());
}
void finish_live_entry(body_builder& b, scene& s) {
    ASSERT_TRUE(s.outer_entry.set_string(kTextTag, "END").has_value());
    ASSERT_TRUE(b.group_end(s.outer).has_value());
}

// Runs `arrange` then `finish` on two identical builders; between them the
// subject alone makes `call`, which must be refused with `want`. Both builders
// must then commit, to the same bytes.
template <class Arrange, class Call, class Finish>
void expect_refused_and_rolled_back(Arrange arrange, Call call, Finish finish, error want,
                                    std::string_view what) {
    SCOPED_TRACE(std::string{what});
    body_builder subject{"X"};
    body_builder twin{"X"};
    scene subject_scene;
    scene twin_scene;
    arrange(subject, subject_scene);
    arrange(twin, twin_scene);
    if (::testing::Test::HasFatalFailure()) return;

    auto const r = call(subject, subject_scene);
    ASSERT_FALSE(r.has_value()) << "the call was accepted";
    EXPECT_EQ(r.error(), want);

    finish(subject, subject_scene);
    finish(twin, twin_scene);
    if (::testing::Test::HasFatalFailure()) return;

    auto const twin_body = commit_body(twin);
    ASSERT_TRUE(twin_body.has_value())
        << "the no-call twin must commit; error " << static_cast<int>(twin_body.error());
    auto const subject_body = commit_body(subject);
    ASSERT_TRUE(subject_body.has_value())
        << "the subject must commit after the refused call; error "
        << static_cast<int>(subject_body.error());
    EXPECT_EQ(*subject_body, *twin_body);
}

// Frames `body` (8=, 9=, and a fixed 10=000, which the test frame factory does
// not verify) and returns every field the dictionary-free streaming parser reads.
std::vector<std::pair<std::uint16_t, std::string>> reparse(std::string const& body) {
    std::string const full =
        "8=FIX.4.4\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body + "10=000\x01";
    std::vector<std::byte> buf(full.size());
    std::memcpy(buf.data(), full.data(), full.size());
    std::vector<std::pair<std::uint16_t, std::string>> fields;
    auto fv = fixpp::wire::test::make_frame_view(buf);
    EXPECT_TRUE(fv.has_value()) << "make_frame_view failed";
    if (!fv.has_value()) return fields;
    fixpp::wire::Parser<fixpp::wire::access_mode::Iter> parser{};
    auto mv = parser.parse_iter(*fv);
    EXPECT_TRUE(mv.has_value()) << "parse_iter failed";
    if (!mv.has_value()) return fields;
    for (auto it = mv->begin(); !(it == mv->end()); ++it) {
        auto const& f = *it;
        fields.emplace_back(f.tag, bytes_to_string(f.value));
    }
    return fields;
}

// Every value the re-parse read for `tag`, in order.
std::vector<std::string> values_of(std::vector<std::pair<std::uint16_t, std::string>> const& fields,
                                   std::uint16_t tag) {
    std::vector<std::string> out;
    for (auto const& [t, v] : fields) {
        if (t == tag) out.push_back(v);
    }
    return out;
}

// The pre-fill for C-1.5: `filler` bytes in one Text(58) field, then `count`
// one-byte Text(58) fields, at the top level or (nested) in a live innermost
// entry that stays open until close_prefill.
struct prefill_plan {
    bool nested = false;
    std::size_t count = 0;
    std::size_t filler = 0;
};

expected_t<void> put_string(body_builder& b, bool nested, scene& s, std::uint16_t tag,
                            std::string_view v) {
    return nested ? s.outer_entry.set_string(tag, v) : b.field(tag, v);
}

expected_t<void> put_int(body_builder& b, bool nested, scene& s, std::uint16_t tag,
                         std::int64_t v) {
    return nested ? s.outer_entry.set_int(tag, v) : b.field(tag, v);
}

bool apply_prefill(body_builder& b, prefill_plan const& p, scene& s) {
    if (p.nested) {
        auto g = b.group_begin(kOuterNo, kOuterDelim);
        if (!g.has_value()) return false;
        s.outer = *g;
        auto e = s.outer.add_entry();
        if (!e.has_value()) return false;
        s.outer_entry = *e;
        if (!s.outer_entry.set_string(kOuterDelim, "A1").has_value()) return false;
    }
    if (p.filler > 0 &&
        !put_string(b, p.nested, s, kTextTag, std::string(p.filler, 'f')).has_value()) {
        return false;
    }
    for (std::size_t i = 0; i < p.count; ++i) {
        if (!put_string(b, p.nested, s, kTextTag, "f").has_value()) return false;
    }
    return true;
}

bool close_prefill(body_builder& b, prefill_plan const& p, scene& s) {
    return !p.nested || b.group_end(s.outer).has_value();
}

// The no-call twin of a pre-fill: the pre-fill alone, closed and committed.
bool prefill_commits(prefill_plan const& p) {
    body_builder b{"X"};
    scene s;
    if (!apply_prefill(b, p, s) || !close_prefill(b, p, s)) return false;
    return commit_body(b).has_value();
}

// The largest value a lone Text(58) field can carry and still commit. Found by
// commit; the body cap is TU-local to src/wire/body_builder.cpp.
std::size_t largest_committing_text_value() {
    std::size_t n = 1;
    for (; n <= kBufSize; ++n) {
        body_builder b{"X"};
        if (!b.field(kTextTag, std::string_view{std::string(n, 'v')}).has_value()) break;
        if (!commit_body(b).has_value()) break;
    }
    return n - 1;
}

// Searches for a pre-fill whose no-call twin commits and after which a Length
// half of value `n` is accepted while an `n`-byte second field is refused for
// arena exhaustion. The probe is hand-written (field/set_int + a plain Text-style
// append on the Data tag); the operation under test is never the search signal.
// For each count the filler is the largest one whose no-call twin commits.
std::optional<prefill_plan> find_exhausting_prefill(bool nested, std::size_t n) {
    for (std::size_t count = 0; count <= kBufSize; ++count) {
        if (!prefill_commits({.nested = nested, .count = count, .filler = 0})) break;
        std::size_t lo = 0;
        std::size_t hi = n;
        while (lo < hi) {
            std::size_t const mid = lo + ((hi - lo + 1) / 2);
            if (prefill_commits({.nested = nested, .count = count, .filler = mid})) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        prefill_plan const p{.nested = nested, .count = count, .filler = lo};
        body_builder probe{"X"};
        scene s;
        if (!apply_prefill(probe, p, s)) continue;
        if (!put_int(probe, nested, s, 354, static_cast<std::int64_t>(n)).has_value()) continue;
        auto const data = put_string(probe, nested, s, 355, std::string(n, 'd'));
        if (!data.has_value() && data.error() == error::wire_frame_too_large) return p;
    }
    return std::nullopt;
}

// Searches for a pre-fill whose no-call twin commits and after which a
// hand-written Length half of value `n` is itself refused for arena exhaustion.
// That Length append is the same call the operation makes first, so a plan
// found here makes the operation fail on its Length half, before any Data node.
// For each count the filler is the largest one whose no-call twin commits.
std::optional<prefill_plan> find_length_exhausting_prefill(bool nested, std::size_t n) {
    for (std::size_t count = 0; count <= kBufSize; ++count) {
        if (!prefill_commits({.nested = nested, .count = count, .filler = 0})) break;
        std::size_t lo = 0;
        std::size_t hi = kBufSize;
        while (lo < hi) {
            std::size_t const mid = lo + ((hi - lo + 1) / 2);
            if (prefill_commits({.nested = nested, .count = count, .filler = mid})) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        prefill_plan const p{.nested = nested, .count = count, .filler = lo};
        body_builder probe{"X"};
        scene s;
        if (!apply_prefill(probe, p, s)) continue;
        auto const length = put_int(probe, nested, s, 354, static_cast<std::int64_t>(n));
        if (!length.has_value() && length.error() == error::wire_frame_too_large) return p;
    }
    return std::nullopt;
}

}  // namespace

// ── C-1.1 success: the pair lands at the position of the call ──────────────
TEST(BodyBuilderDataField, C1_1_PairAppendedLengthFirstAtCallPosition) {
    body_builder bb{"X"};
    ASSERT_TRUE(bb.field(11, std::string_view{"A"}).has_value());
    std::array<std::byte, 3> const v{std::byte{0x41}, std::byte{0x01}, std::byte{0x42}};
    ASSERT_TRUE(bb.field_data(355, std::span<const std::byte>{v}).has_value());
    ASSERT_TRUE(bb.field(kTextTag, std::string_view{"Z"}).has_value());
    auto const body = expect_commit_ok(bb);
    EXPECT_EQ(body, std::string{"35=X\x01"
                                "11=A\x01"
                                "354=3\x01"
                                "355=A\x01"
                                "B\x01"
                                "58=Z\x01"});
}

// ── C-1.1 variants: Data tag below its Length tag; a non-adjacent pair ─────
TEST(BodyBuilderDataField, C1_1_InvertedAndNonAdjacentPairsEmitLengthFirst) {
    {
        body_builder bb{"X"};
        ASSERT_TRUE(bb.field_data(89, octets("sig")).has_value());
        auto const body = expect_commit_ok(bb);
        EXPECT_EQ(body, std::string{"35=X\x01"
                                    "93=3\x01"
                                    "89=sig\x01"});
    }
    {
        body_builder bb{"X"};
        ASSERT_TRUE(bb.field_data(1527, octets("doc")).has_value());
        auto const body = expect_commit_ok(bb);
        EXPECT_EQ(body, std::string{"35=X\x01"
                                    "1525=3\x01"
                                    "1527=doc\x01"});
    }
}

// ── C-1.2 every octet, both surfaces, recovered by the inbound parser ──────
TEST(BodyBuilderDataField, C1_2_EveryOctetRoundTripsThroughTheParser) {
    for (unsigned o = 0; o <= 0xFFU; ++o) {
        SCOPED_TRACE("octet " + std::to_string(o));
        std::array<std::byte, 1> const v{static_cast<std::byte>(o)};
        std::string const want(1, static_cast<char>(o));

        body_builder top{"X"};
        ASSERT_TRUE(top.field_data(355, std::span<const std::byte>{v}).has_value());
        ASSERT_TRUE(top.field(kTextTag, std::string_view{"END"}).has_value());
        auto const top_body = expect_commit_ok(top);
        auto const top_fields = reparse(top_body);
        EXPECT_EQ(values_of(top_fields, 354), std::vector<std::string>{"1"});
        EXPECT_EQ(values_of(top_fields, 355), std::vector<std::string>{want});
        EXPECT_EQ(values_of(top_fields, kTextTag), std::vector<std::string>{"END"});

        body_builder nested{"X"};
        scene s;
        arrange_live_entry(nested, s);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        ASSERT_TRUE(s.outer_entry.set_data(355, std::span<const std::byte>{v}).has_value());
        finish_live_entry(nested, s);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        auto const nested_body = expect_commit_ok(nested);
        auto const nested_fields = reparse(nested_body);
        EXPECT_EQ(values_of(nested_fields, 354), std::vector<std::string>{"1"});
        EXPECT_EQ(values_of(nested_fields, 355), std::vector<std::string>{want});
        EXPECT_EQ(values_of(nested_fields, kTextTag), std::vector<std::string>{"END"});
    }
}

// ── C-1.3 multi-digit Length, up to the largest N that commits ─────────────
// The boundary is found by commit: N + 1 must be refused wire_frame_too_large.
TEST(BodyBuilderDataField, C1_3_MultiDigitLengthUpToTheCommitBoundary) {
    {
        body_builder bb{"X"};
        std::string const v(10, 'v');
        ASSERT_TRUE(bb.field_data(355, octets(v)).has_value());
        auto const body = expect_commit_ok(bb);
        EXPECT_EQ(body,
                  "35=X\x01"
                  "354=10\x01"
                  "355=" +
                      v + "\x01");
    }

    std::size_t largest = 0;
    bool refused = false;
    for (std::size_t n = 10; n <= kBufSize && !refused; ++n) {
        body_builder bb{"X"};
        std::string const v(n, 'v');
        auto const set = bb.field_data(355, octets(v));
        ASSERT_TRUE(set.has_value()) << "field_data refused an " << n << "-octet value";
        auto const body = commit_body(bb);
        if (!body.has_value()) {
            EXPECT_EQ(body.error(), error::wire_frame_too_large) << "N + 1 = " << n;
            refused = true;
            continue;
        }
        largest = n;
    }
    ASSERT_TRUE(refused) << "no N up to the test buffer size was refused at commit";
    ASSERT_GE(largest, 10U);

    body_builder bb{"X"};
    std::string const v(largest, 'v');
    ASSERT_TRUE(bb.field_data(355, octets(v)).has_value());
    auto const body = expect_commit_ok(bb);
    EXPECT_EQ(body,
              "35=X\x01"
              "354=" +
                  std::to_string(largest) +
                  "\x01"
                  "355=" +
                  v + "\x01");
}

// ── C-1.4 refusals, on both surfaces, with FR-004a's codes ─────────────────
TEST(BodyBuilderDataField, C1_4_RefusalsOnBothSurfacesRollBack) {
    struct refusal_case {
        std::uint16_t tag;
        std::string_view value;
        error want;
        std::string_view what;
    };
    refusal_case const cases[] = {
        {.tag = 11,
         .value = std::string_view{"A\x01"
                                   "1=EVIL"},
         .want = error::wire_unexpected_tag,
         .what = "non-Data tag 11"},
        {.tag = 354,
         .value = std::string_view{"abc"},
         .want = error::wire_unexpected_tag,
         .what = "Length half 354"},
        {.tag = 8,
         .value = std::string_view{"abc"},
         .want = error::wire_field_value_out_of_range,
         .what = "framing tag 8"},
        {.tag = 355,
         .value = std::string_view{},
         .want = error::wire_field_value_out_of_range,
         .what = "empty value"},
    };
    for (auto const& c : cases) {
        expect_refused_and_rolled_back(
            arrange_top,
            [&c](body_builder& b, scene&) { return b.field_data(c.tag, octets(c.value)); },
            finish_top, c.want, std::string{"field_data: "} + std::string{c.what});
        expect_refused_and_rolled_back(
            arrange_live_entry,
            [&c](body_builder&, scene& s) {
                return s.outer_entry.set_data(c.tag, octets(c.value));
            },
            finish_live_entry, c.want, std::string{"set_data: "} + std::string{c.what});
    }
}

// ── C-1.4b set_data handle checks ───────────────────────────────────────────
TEST(BodyBuilderDataField, C1_4b_SetDataHandleChecksRollBack) {
    // A default-constructed entry_handle has no owner.
    expect_refused_and_rolled_back(
        arrange_top,
        [](body_builder&, scene&) { return entry_handle{}.set_data(355, octets("abc")); },
        finish_top, error::wire_invalid_field_format, "default-constructed entry_handle");

    // The handle of an entry whose group was already closed.
    expect_refused_and_rolled_back(
        [](body_builder& b, scene& s) {
            arrange_live_entry(b, s);
            ASSERT_TRUE(b.group_end(s.outer).has_value());
        },
        [](body_builder&, scene& s) { return s.outer_entry.set_data(355, octets("abc")); },
        finish_top, error::wire_invalid_field_format, "entry of a closed group");

    // An outer entry's handle while a nested group's entry is innermost.
    expect_refused_and_rolled_back(
        [](body_builder& b, scene& s) {
            arrange_live_entry(b, s);
            if (::testing::Test::HasFatalFailure()) return;
            auto g = s.outer_entry.group_begin(kInnerNo, kInnerDelim);
            ASSERT_TRUE(g.has_value());
            s.inner = *g;
            auto e = s.inner.add_entry();
            ASSERT_TRUE(e.has_value());
            s.inner_entry = *e;
            ASSERT_TRUE(s.inner_entry.set_string(kInnerDelim, "P1").has_value());
        },
        [](body_builder&, scene& s) { return s.outer_entry.set_data(355, octets("abc")); },
        [](body_builder& b, scene& s) {
            ASSERT_TRUE(b.group_end(s.inner).has_value());
            ASSERT_TRUE(b.group_end(s.outer).has_value());
        },
        error::wire_invalid_field_format, "outer entry while a nested entry is innermost");
}

// ── C-1.5 rollback when the Data half exhausts the arena ───────────────────
// The pre-fill is searched for, not written as a literal (arena and body-cap
// sizes are private, and outer-vector regrowth differs by standard library).
// No plan found means no pre-fill both exhausts the arena and keeps the body
// under the body cap: that is a contract change for C-1.5, reported, not
// worked around.
TEST(BodyBuilderDataField, C1_5_ArenaExhaustionOnTheDataHalfRollsBackBothSurfaces) {
    std::size_t const n = largest_committing_text_value();
    ASSERT_GT(n, 0U);
    {
        // The Data value is below the body cap: a lone field of n octets commits.
        body_builder below_cap{"X"};
        ASSERT_TRUE(below_cap.field(kTextTag, std::string_view{std::string(n, 'v')}).has_value());
        ASSERT_TRUE(commit_body(below_cap).has_value());
    }
    std::string const value(n, 'd');

    for (bool const nested : {false, true}) {
        SCOPED_TRACE(nested ? "set_data in a live innermost entry" : "field_data at the top level");
        auto const plan = find_exhausting_prefill(nested, n);
        if (!plan.has_value()) {
            GTEST_FAIL()
                << "no pre-fill both exhausts the arena and keeps the body under the body cap "
                   "(C-1.5 contract change)";
        }

        // Arrangement witness: the Length half fits after the identical pre-fill.
        {
            body_builder witness{"X"};
            scene s;
            ASSERT_TRUE(apply_prefill(witness, *plan, s));
            EXPECT_TRUE(put_int(witness, nested, s, 354, static_cast<std::int64_t>(n)).has_value())
                << "the Length half must fit, so the failure is on the Data half";
        }

        // The no-call twin commits.
        body_builder twin{"X"};
        scene twin_scene;
        ASSERT_TRUE(apply_prefill(twin, *plan, twin_scene));
        ASSERT_TRUE(close_prefill(twin, *plan, twin_scene));
        auto const twin_body = commit_body(twin);
        ASSERT_TRUE(twin_body.has_value()) << "the no-call twin must commit";

        body_builder subject{"X"};
        scene subject_scene;
        ASSERT_TRUE(apply_prefill(subject, *plan, subject_scene));
        auto const r = nested ? subject_scene.outer_entry.set_data(355, octets(value))
                              : subject.field_data(355, octets(value));
        ASSERT_FALSE(r.has_value()) << "the Data half was accepted";
        EXPECT_EQ(r.error(), error::wire_frame_too_large);
        ASSERT_TRUE(close_prefill(subject, *plan, subject_scene));
        auto const subject_body = commit_body(subject);
        ASSERT_TRUE(subject_body.has_value())
            << "the subject must commit; error " << static_cast<int>(subject_body.error());
        EXPECT_EQ(*subject_body, *twin_body);
    }
}

// ── C-1.5 rollback when the Length half exhausts the arena (FR-006) ────────
// The operation appends its Length node first; when that append runs the arena
// out, no Data node is attempted and the builder is left as it was. The
// pre-fill is searched for, as in the Data-half cell above.
TEST(BodyBuilderDataField, C1_5_ArenaExhaustionOnTheLengthHalfRollsBackBothSurfaces) {
    std::string const value = "abc";

    for (bool const nested : {false, true}) {
        SCOPED_TRACE(nested ? "set_data in a live innermost entry" : "field_data at the top level");
        auto const plan = find_length_exhausting_prefill(nested, value.size());
        if (!plan.has_value()) {
            GTEST_FAIL() << "no committing pre-fill makes the Length half exhaust the arena";
        }

        // Arrangement witness: after the identical pre-fill the Length half alone
        // is refused, so the operation's failure is on its Length half.
        {
            body_builder witness{"X"};
            scene s;
            ASSERT_TRUE(apply_prefill(witness, *plan, s));
            auto const length =
                put_int(witness, nested, s, 354, static_cast<std::int64_t>(value.size()));
            ASSERT_FALSE(length.has_value())
                << "the Length half must not fit, so the failure is on the Length half";
            EXPECT_EQ(length.error(), error::wire_frame_too_large);
        }

        // The no-call twin commits.
        body_builder twin{"X"};
        scene twin_scene;
        ASSERT_TRUE(apply_prefill(twin, *plan, twin_scene));
        ASSERT_TRUE(close_prefill(twin, *plan, twin_scene));
        auto const twin_body = commit_body(twin);
        ASSERT_TRUE(twin_body.has_value()) << "the no-call twin must commit";

        body_builder subject{"X"};
        scene subject_scene;
        ASSERT_TRUE(apply_prefill(subject, *plan, subject_scene));
        auto const r = nested ? subject_scene.outer_entry.set_data(355, octets(value))
                              : subject.field_data(355, octets(value));
        ASSERT_FALSE(r.has_value()) << "the operation was accepted";
        EXPECT_EQ(r.error(), error::wire_frame_too_large);
        ASSERT_TRUE(close_prefill(subject, *plan, subject_scene));
        auto const subject_body = commit_body(subject);
        ASSERT_TRUE(subject_body.has_value())
            << "the subject must commit; error " << static_cast<int>(subject_body.error());
        EXPECT_EQ(*subject_body, *twin_body);
    }
}

// ── C-1.5 an over-cap total is refused at commit, repeatably ────────────────
TEST(BodyBuilderDataField, C1_5_OverCapAtCommitLeavesOutUntouchedAndRepeats) {
    std::size_t const n = largest_committing_text_value();
    ASSERT_GT(n, 0U);
    body_builder bb{"X"};
    // n octets of Data plus its Length field exceed what a lone n-octet field fits.
    ASSERT_TRUE(bb.field_data(355, octets(std::string(n, 'd'))).has_value())
        << "the arena must hold the value, so the refusal is the body cap's";
    expect_commit_refused(bb, error::wire_frame_too_large);
    expect_commit_refused(bb, error::wire_frame_too_large);
}

// ── C-1.6 the string path on a Data tag keeps the content guard ─────────────
TEST(BodyBuilderDataField, C1_6_StringFieldOnADataTagStillRefusesSoh) {
    expect_refused_and_rolled_back(
        arrange_top,
        [](body_builder& b, scene&) {
            return b.field(355, std::string_view{"A\x01"
                                                 "B"});
        },
        finish_top, error::wire_field_value_out_of_range, "field(355, A<SOH>B)");
}

// ── C-1.7 commit pair check (INV-6) ─────────────────────────────────────────
TEST(BodyBuilderDataField, C1_7_MalformedHandWrittenPairsAreRefusedAtCommit) {
    {
        SCOPED_TRACE("354 with no following 355");
        body_builder bb{"X"};
        ASSERT_TRUE(bb.field(354, std::int64_t{3}).has_value());
        expect_commit_refused(bb, error::wire_invalid_field_format);
    }
    {
        SCOPED_TRACE("355 with no preceding 354");
        body_builder bb{"X"};
        ASSERT_TRUE(bb.field(355, std::string_view{"abc"}).has_value());
        expect_commit_refused(bb, error::wire_invalid_field_format);
    }
    {
        SCOPED_TRACE("354=4 with a 3-octet 355");
        body_builder bb{"X"};
        ASSERT_TRUE(bb.field(354, std::int64_t{4}).has_value());
        ASSERT_TRUE(bb.field(355, std::string_view{"abc"}).has_value());
        expect_commit_refused(bb, error::wire_invalid_field_format);
    }
    {
        SCOPED_TRACE("354=0");
        body_builder bb{"X"};
        ASSERT_TRUE(bb.field(354, std::string_view{"0"}).has_value());
        ASSERT_TRUE(bb.field(355, std::string_view{"x"}).has_value());
        expect_commit_refused(bb, error::wire_invalid_field_format);
    }
}

TEST(BodyBuilderDataField, C1_7_GroupNodeBetweenLengthAndData) {
    body_builder subject{"X"};
    ASSERT_TRUE(subject.field(354, std::int64_t{3}).has_value());
    add_populated_group(subject, kOuterNo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(subject.field(355, std::string_view{"abc"}).has_value());
    expect_commit_refused(subject, error::wire_invalid_field_format);

    // Committing twin: the same populated group after a well-formed pair.
    body_builder twin{"X"};
    ASSERT_TRUE(twin.field(354, std::int64_t{3}).has_value());
    ASSERT_TRUE(twin.field(355, std::string_view{"abc"}).has_value());
    add_populated_group(twin, kOuterNo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    expect_commit_ok(twin);
}

// The pair check reads a group node with an EMPTY value (R-4): fed its count digits
// "1", the group 354 would pair with the one-byte sibling 355 and pass.
TEST(BodyBuilderDataField, C1_7_GroupWhoseNoTagIsTheLengthTag) {
    body_builder subject{"X"};
    add_populated_group(subject, 354);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(subject.field(355, std::string_view{"x"}).has_value());
    expect_commit_refused(subject, error::wire_invalid_field_format);

    // Committing twin: no_tag changed to a non-pair tag, the sibling a well-formed pair.
    body_builder twin{"X"};
    add_populated_group(twin, kOuterNo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(twin.field(354, std::int64_t{1}).has_value());
    ASSERT_TRUE(twin.field(355, std::string_view{"x"}).has_value());
    expect_commit_ok(twin);
}

TEST(BodyBuilderDataField, C1_7_GroupWhoseNoTagIsTheDataTag) {
    body_builder subject{"X"};
    add_populated_group(subject, 355);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    expect_commit_refused(subject, error::wire_invalid_field_format);

    // Committing twin: no_tag changed to a non-pair tag.
    body_builder twin{"X"};
    add_populated_group(twin, kOuterNo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    expect_commit_ok(twin);
}

// The same refusal for a group opened inside an entry, where the nested no_tag is
// the only pair tag anywhere in the message.
TEST(BodyBuilderDataField, C1_7_NestedGroupWhoseNoTagIsTheDataTag) {
    body_builder subject{"X"};
    scene s;
    arrange_live_entry(subject, s);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    auto ig = s.outer_entry.group_begin(355, kInnerDelim);
    ASSERT_TRUE(ig.has_value());
    auto ie = ig->add_entry();
    ASSERT_TRUE(ie.has_value());
    ASSERT_TRUE(ie->set_string(kInnerDelim, "P1").has_value());
    ASSERT_TRUE(subject.group_end(*ig).has_value());
    ASSERT_TRUE(subject.group_end(s.outer).has_value());
    expect_commit_refused(subject, error::wire_invalid_field_format);

    // Committing twin: the nested no_tag changed to a non-pair tag.
    body_builder twin{"X"};
    scene ts;
    arrange_live_entry(twin, ts);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    auto tig = ts.outer_entry.group_begin(kInnerNo, kInnerDelim);
    ASSERT_TRUE(tig.has_value());
    auto tie = tig->add_entry();
    ASSERT_TRUE(tie.has_value());
    ASSERT_TRUE(tie->set_string(kInnerDelim, "P1").has_value());
    ASSERT_TRUE(twin.group_end(*tig).has_value());
    ASSERT_TRUE(twin.group_end(ts.outer).has_value());
    auto const body = expect_commit_ok(twin);
    EXPECT_EQ(body, std::string{"35=X\x01"
                                "78=1\x01"
                                "79=A1\x01"
                                "453=1\x01"
                                "448=P1\x01"});
}

TEST(BodyBuilderDataField, C1_7_HandWrittenWellFormedPairCommits) {
    body_builder bb{"X"};
    ASSERT_TRUE(bb.field(354, std::int64_t{3}).has_value());
    ASSERT_TRUE(bb.field(355, std::string_view{"abc"}).has_value());
    auto const body = expect_commit_ok(bb);
    EXPECT_EQ(body, std::string{"35=X\x01"
                                "354=3\x01"
                                "355=abc\x01"});
}

// ── C-1.8 the pair check runs per container ─────────────────────────────────
// Each subject serializes to exactly its twin's bytes; they differ only in
// which container holds the 355. A walk over the serialized order would accept
// both; the per-container walk refuses the subject.
TEST(BodyBuilderDataField, C1_8_LengthEndingAnInstanceDoesNotPairAcrossContainers) {
    {
        SCOPED_TRACE("Length ends a top-level group's instance; Data is the next top-level field");
        body_builder subject{"X"};
        auto g = subject.group_begin(kOuterNo, kOuterDelim);
        ASSERT_TRUE(g.has_value());
        auto e = g->add_entry();
        ASSERT_TRUE(e.has_value());
        ASSERT_TRUE(e->set_string(kOuterDelim, "A1").has_value());
        ASSERT_TRUE(e->set_int(354, 1).has_value());
        ASSERT_TRUE(subject.group_end(*g).has_value());
        ASSERT_TRUE(subject.field(355, std::string_view{"x"}).has_value());
        expect_commit_refused(subject, error::wire_invalid_field_format);

        body_builder twin{"X"};
        auto tg = twin.group_begin(kOuterNo, kOuterDelim);
        ASSERT_TRUE(tg.has_value());
        auto te = tg->add_entry();
        ASSERT_TRUE(te.has_value());
        ASSERT_TRUE(te->set_string(kOuterDelim, "A1").has_value());
        ASSERT_TRUE(te->set_int(354, 1).has_value());
        ASSERT_TRUE(te->set_string(355, "x").has_value());
        ASSERT_TRUE(twin.group_end(*tg).has_value());
        auto const body = expect_commit_ok(twin);
        EXPECT_EQ(body, std::string{"35=X\x01"
                                    "78=1\x01"
                                    "79=A1\x01"
                                    "354=1\x01"
                                    "355=x\x01"});
    }
    {
        SCOPED_TRACE("Length ends a nested group's instance; Data is the outer entry's next field");
        body_builder subject{"X"};
        scene s;
        arrange_live_entry(subject, s);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        auto ig = s.outer_entry.group_begin(kInnerNo, kInnerDelim);
        ASSERT_TRUE(ig.has_value());
        auto ie = ig->add_entry();
        ASSERT_TRUE(ie.has_value());
        ASSERT_TRUE(ie->set_string(kInnerDelim, "P1").has_value());
        ASSERT_TRUE(ie->set_int(354, 1).has_value());
        ASSERT_TRUE(subject.group_end(*ig).has_value());
        ASSERT_TRUE(s.outer_entry.set_string(355, "x").has_value());
        ASSERT_TRUE(subject.group_end(s.outer).has_value());
        expect_commit_refused(subject, error::wire_invalid_field_format);

        body_builder twin{"X"};
        scene ts;
        arrange_live_entry(twin, ts);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        auto tig = ts.outer_entry.group_begin(kInnerNo, kInnerDelim);
        ASSERT_TRUE(tig.has_value());
        auto tie = tig->add_entry();
        ASSERT_TRUE(tie.has_value());
        ASSERT_TRUE(tie->set_string(kInnerDelim, "P1").has_value());
        ASSERT_TRUE(tie->set_int(354, 1).has_value());
        ASSERT_TRUE(tie->set_string(355, "x").has_value());
        ASSERT_TRUE(twin.group_end(*tig).has_value());
        ASSERT_TRUE(twin.group_end(ts.outer).has_value());
        auto const body = expect_commit_ok(twin);
        EXPECT_EQ(body, std::string{"35=X\x01"
                                    "78=1\x01"
                                    "79=A1\x01"
                                    "453=1\x01"
                                    "448=P1\x01"
                                    "354=1\x01"
                                    "355=x\x01"});
    }
}

// ── C-1.10 a Length-delimited entry commits ─────────────────────────────────
TEST(BodyBuilderDataField, C1_10_LengthDelimitedEntryCommits) {
    body_builder bb{"X"};
    auto g = bb.group_begin(kOuterNo, 43109);
    ASSERT_TRUE(g.has_value());
    auto e = g->add_entry();
    ASSERT_TRUE(e.has_value());
    ASSERT_TRUE(e->set_data(42684, octets("f(x)")).has_value());
    ASSERT_TRUE(bb.group_end(*g).has_value());
    auto const body = expect_commit_ok(bb);
    EXPECT_EQ(body, std::string{"35=X\x01"
                                "78=1\x01"
                                "43109=4\x01"
                                "42684=f(x)\x01"});
}

// ── C-1.11 a second call appends a second pair ──────────────────────────────
TEST(BodyBuilderDataField, C1_11_TwoCallsAppendTwoPairs) {
    body_builder bb{"X"};
    ASSERT_TRUE(bb.field_data(355, octets("ab")).has_value());
    ASSERT_TRUE(bb.field_data(355, octets("cde")).has_value());
    auto const body = expect_commit_ok(bb);
    EXPECT_EQ(body, std::string{"35=X\x01"
                                "354=2\x01"
                                "355=ab\x01"
                                "354=3\x01"
                                "355=cde\x01"});
}

// ── T024 zero global ::operator new on the new paths ────────────────────────
// Same gate as NoGlobalHeap_CountingNew, over construction with a dict_hooks
// argument, field_data, a group entry's set_data, and a commit that runs the
// INV-6 pair walk. The hooks value is built before the window. This binary links
// no dictionary and does not use the test-only raw-constructor seam, so none() is
// the bundle it builds; the builder copies whichever bundle it is given by value.
TEST(BodyBuilderDataField, NoGlobalHeap_FieldDataSetDataCommit) {
#if FIXPP_SANITIZER_REPLACES_NEW
    GTEST_SKIP() << "global operator new replacement is incompatible with ASan "
                    "(alloc-dealloc-mismatch) and TSan (multiple-definition of operator new); "
                    "the zero-alloc witness runs in debug/release/ubsan, with mallocnesia "
                    "LD_PRELOAD as the CI-tier cross-check";
#elif defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
    // MSVC debug STL only: see NoGlobalHeap_CountingNew for the _Container_proxy cause.
    GTEST_SKIP() << "MSVC debug STL allocates a per-container _Container_proxy via global "
                    "operator new at pmr-vector construction (debug-STL artifact); the "
                    "heap-free builder contract is covered on all non-MSVC-debug lanes and "
                    "by the mallocnesia LD_PRELOAD cross-check";
#else
    std::array<std::byte, kBufSize> buf{};
    fixpp::wire::dict_hooks const hooks = fixpp::wire::dict_hooks::none();
    std::array<std::byte, 3> const top{std::byte{0x41}, std::byte{0x01}, std::byte{0xFF}};
    std::array<std::byte, 2> const nested{std::byte{0x00}, std::byte{0x80}};

    // -- Zero-alloc window --
    long const before = g_bb_alloc_count.load(std::memory_order_relaxed);

    body_builder bb{"X", hooks};
    auto const top_r = bb.field_data(355, std::span<const std::byte>{top});
    auto g = bb.group_begin(kOuterNo, kOuterDelim);
    auto e = g.has_value() ? g->add_entry() : expected_t<entry_handle>{std::unexpected(g.error())};
    auto const delim_r = e.has_value() ? e->set_string(kOuterDelim, "A1")
                                       : expected_t<void>{std::unexpected(e.error())};
    auto const nested_r = e.has_value() ? e->set_data(91, std::span<const std::byte>{nested})
                                        : expected_t<void>{std::unexpected(e.error())};
    auto const end_r =
        g.has_value() ? bb.group_end(*g) : expected_t<void>{std::unexpected(g.error())};
    auto const r = bb.commit(std::span<std::byte>{buf});

    long const after = g_bb_alloc_count.load(std::memory_order_relaxed);
    // -- End zero-alloc window --

    ASSERT_TRUE(top_r.has_value());
    ASSERT_TRUE(g.has_value());
    ASSERT_TRUE(e.has_value());
    ASSERT_TRUE(delim_r.has_value());
    ASSERT_TRUE(nested_r.has_value());
    ASSERT_TRUE(end_r.has_value());
    ASSERT_TRUE(r.has_value()) << "commit must succeed for the alloc witness to be meaningful";
    EXPECT_EQ(bytes_to_string(*r), std::string("35=X\x01"
                                               "354=3\x01"
                                               "355=A\x01\xFF\x01"
                                               "78=1\x01"
                                               "79=A1\x01"
                                               "90=2\x01"
                                               "91=\x00\x80\x01",
                                               41));
    EXPECT_EQ(after, before)
        << "construction with dict_hooks + field_data()/set_data()/commit() must not call "
           "global ::operator new (alloc delta = "
        << (after - before) << ")";
#endif  // FIXPP_SANITIZER_REPLACES_NEW
}
