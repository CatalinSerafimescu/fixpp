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

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/body_builder.hpp>
#include <memory_resource>
#include <new>
#include <span>
#include <string>
#include <string_view>

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

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif  // !FIXPP_SANITIZER_REPLACES_NEW

namespace {

using fixpp::decimal_t;
using fixpp::wire::body_builder;

constexpr std::size_t kBufSize = 8192;

// Parse a decimal_t from an ASCII literal (no string-literal ctor, matches
// tests/session/test_business_messages_build.cpp precedent).
decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

std::string bytes_to_string(std::span<const std::byte> b) {
    return std::string{reinterpret_cast<const char*>(b.data()), b.size()};
}

// Fill a byte span with a sentinel pattern so INV-4 "untouched on failure"
// can be asserted by comparing before/after.
void fill_sentinel(std::span<std::byte> s) {
    for (auto& b : s) b = std::byte{0xABU};
}

bool all_sentinel(std::span<const std::byte> s) {
    return std::all_of(s.begin(), s.end(), [](std::byte b) { return b == std::byte{0xABU}; });
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

    std::string expected = "35=X\x01"
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

    EXPECT_EQ(bytes_to_string(*r), "35=X\x01"
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

    EXPECT_EQ(bytes_to_string(*r), "35=X\x01"
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

    EXPECT_EQ(bytes_to_string(*r), "35=X\x01"
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

    EXPECT_EQ(bytes_to_string(*r), "35=E\x01"
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

    EXPECT_EQ(bytes_to_string(*r), "35=X\x01"
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
    EXPECT_EQ(body, "35=X\x01"
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
        << (after - before)
        << "); mallocnesia LD_PRELOAD is the CI-tier cross-check";
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
