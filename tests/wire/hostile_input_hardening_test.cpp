// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/hostile_input_hardening_test.cpp — Cluster-1 wire hostile-input
// hardening (wire-hostile-input-review.md W-P2-1 / W-P3-2). Discriminating
// witnesses for the Length+Data desync defects and the mix()-collision
// false-absent. Each behavioural case is RED on the pre-fix tree (asserted in
// the batch's RED gate) and GREEN after the fix.
//
// Covered here:
//   W-P2-1(a) — SOH not verified after a counted Data value (blind i=end+1):
//               a lying RawDataLength that lands mid-field must be REJECTED
//               (Index) / stop iteration (Iter), not silently desync-and-accept.
//   W-P2-1(b) — pending Length→Data state must be cleared when the immediately
//               following field is not the paired Data tag (no stale carry).
//   regression — a truthful RawDataLength (embedded SOH) still round-trips.
//   W-P3-2   — a per-process mix() seed defeats a precomputed 128-collision set
//               (present required field would otherwise read false-absent); the
//               kMaxBuildProbe skip mechanism is exercised deterministically.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// seam #1: complete table_view must precede parser.hpp (single-definition rule).
// clang-format off
#include "support/mock_dict_table.hpp"
// clang-format on
#include <fixpp/core/error.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>

#include "support/frame_view_factory.hpp"
#include "support/wire_test_hooks.hpp"  // detail::set_overlay_seed_for_testing (W-P3-2)

namespace {

using fixpp::core::error;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;
using fixpp::wire::OffsetTable;

// Build a raw FIX frame over `body` (structural 8=/9=/10= markers only; the
// frame_view factory does not require checksum correctness — same helper shape
// as offset_table_error_path_test.cpp).
std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

std::string_view as_sv(std::span<std::byte const> v) {
    return {reinterpret_cast<char const*>(v.data()), v.size()};
}

// ── W-P2-1(a): counted Data whose declared length lands MID-FIELD ─────────────
// 95=3 (RawDataLength) declares a 3-byte RawData(96); the true value is "xy"
// (2 bytes). Reading 3 bytes lands the boundary at '7' (start of "77="), NOT a
// SOH. The pre-fix scanner blindly does i=end+1, desyncs into "7=A", and ACCEPTS
// a corrupt field stream (tag 77 vanishes). The fix verifies buf[end]==SOH and
// REJECTS with wire_invalid_field_format (Index path).
TEST(HostileInputHardening, CountedDataNonSohBoundaryRejectedIndex) {
    auto buf = make_raw_frame(
        "95=3\x01"
        "96=xy\x01"
        "77=A\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};

    auto s = t.build_status();
    ASSERT_FALSE(s.has_value())
        << "a counted Data value whose declared length lands mid-field (buf[end] "
           "!= SOH) must be rejected, not silently absorbed";
    EXPECT_EQ(s.error(), error::wire_invalid_field_format);
}

// ── Finding 1 (Gate B PR #166 round 1): counted Data whose declared length
// lands EXACTLY at frame end, swallowing the trailing checksum ───────────────
// 95=8 (RawDataLength) declares an 8-byte RawData(96) whose value spans one
// filler byte plus the entire remainder of the frame — the real bytes there
// are the appended "10=000\x01" checksum field. end == n exactly, so the
// pre-fix `end < n && buf[end] != SOH` guard is vacuously false (end < n
// fails) and the frame is silently ACCEPTED with tag 10 swallowed into 96's
// value (find(10) would report absent-because-swallowed, not
// absent-because-rejected). Per the whole-frame scanner contract a legitimate
// counted value can never reach `n` (a Framer-validated frame always has a
// trailing checksum field after the body), so `end == n` is always malformed.
// The fix rejects it with wire_invalid_field_format.
TEST(HostileInputHardening, CountedDataExactFrameEndSwallowsChecksumRejectedIndex) {
    auto buf = make_raw_frame(
        "95=8\x01"
        "96=\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};

    auto s = t.build_status();
    ASSERT_FALSE(s.has_value())
        << "a counted Data value whose declared length lands EXACTLY at frame "
           "end (swallowing the trailing checksum) must be rejected";
    EXPECT_EQ(s.error(), error::wire_invalid_field_format);
}

// ── #221 (1): counted Data whose declared length runs PAST the frame end ──────
// One byte beyond the sibling case above: the same frame with `95=9` instead of
// `95=8` leaves the frame byte-identical in length (n = 30, val_start = 22, so
// `n - val_start` = 8) but declares 9, so the SUBTRACTION bound
// `carry_len > n - val_start` rejects BEFORE `val_start + carry_len` is ever
// formed. That ordering is the W-P2-1a defence: on a 32-bit size_t the sum would
// be the wrapping expression, and the later `end >= n` guard would then be
// evaluated on a wrapped value.
//
// NOTE — this is a COVERAGE test, not a mutation-proof pin. Both this guard and
// the `end >= n` guard below it return wire_invalid_field_format, and `end >= n`
// subsumes every case this one catches on a 64-bit size_t, so deleting the
// subtraction guard leaves this assertion green. What it pins is the boundary
// (8 accepted-into-the-next-guard vs 9 rejected here) and the fact that the path
// executes at all; the wrap it defends against is unobservable on this host.
TEST(HostileInputHardening, CountedDataDeclaredLengthPastFrameEndRejectedIndex) {
    auto buf = make_raw_frame(
        "95=9\x01"
        "96=\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};

    auto s = t.build_status();
    ASSERT_FALSE(s.has_value())
        << "a counted Data value whose declared length runs past the frame end "
           "must be rejected";
    EXPECT_EQ(s.error(), error::wire_invalid_field_format);
}

// ── #221 (1b): a SATURATED declared length ────────────────────────────────────
// 4294967295 exceeds the uint32 accumulator's cap, which accumulate_bounded
// saturates to the frame size `n` rather than wrapping (W-P2-1c / W-P3-1). n is
// always > n - val_start (val_start >= 1 for any field), so a saturated length
// lands on the subtraction guard deterministically — this is the composition of
// the two defences, not just the guard in isolation.
TEST(HostileInputHardening, CountedDataSaturatedDeclaredLengthRejectedIndex) {
    auto buf = make_raw_frame(
        "95=4294967295\x01"
        "96=\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};

    auto s = t.build_status();
    ASSERT_FALSE(s.has_value())
        << "a declared length that saturates the uint32 accumulator must be "
           "rejected, never wrap into an in-bounds offset";
    EXPECT_EQ(s.error(), error::wire_invalid_field_format);
}

// ── regression: a TRUTHFUL RawDataLength (value carries an embedded SOH) ───────
// 96 declared as 5 bytes = "a\x01b\x01c"; the trailing byte after those 5 IS a
// SOH, so the field is well-formed. Must still parse: 96 findable with the full
// 5-byte value, and the following field (77) independently findable. Guards the
// fix against over-rejection.
TEST(HostileInputHardening, TruthfulCountedDataWithEmbeddedSohRoundTrips) {
    auto buf = make_raw_frame(
        "95=5\x01"
        "96=a\x01"
        "b\x01"
        "c\x01"      // 96's value is the 5 bytes "a<SOH>b<SOH>c"
        "77=Z\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};

    ASSERT_TRUE(t.build_status().has_value())
        << "a truthful counted Data value with embedded SOH must parse";

    auto d = t.find(96);
    ASSERT_TRUE(d.has_value()) << "RawData (96) must be present";
    EXPECT_EQ(d->length, 5U) << "counted value must be exactly the declared 5 bytes";

    auto z = t.find(77);
    ASSERT_TRUE(z.has_value()) << "the field after a truthful counted Data must stay findable";
}

// ── W-P2-1(b): stale Length→Data carry across a non-adjacent field ────────────
// 95=5 sets a pending Data length for tag 96, but the immediately-following
// field is 58 (not 96). The pre-fix scanner leaves the pending state intact, so
// a later 96 is (mis)read as a fixed 5-byte counted value, absorbing "43="; tag
// 43 vanishes. The fix clears pending when the next field is not the paired Data
// tag, so 96 is a normal SOH-delimited field and 43 stays findable.
TEST(HostileInputHardening, StalePendingClearedWhenNextFieldNotDataTag) {
    auto buf = make_raw_frame(
        "95=5\x01"
        "58=hello\x01"
        "96=x\x01"
        "43=Z\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};

    ASSERT_TRUE(t.build_status().has_value());

    auto d = t.find(96);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length, 1U)
        << "96 must be a normal SOH-delimited field (value \"x\"), not a stale "
           "5-byte counted read";

    auto z = t.find(43);
    EXPECT_TRUE(z.has_value())
        << "a field after a stale pending Length must not be absorbed — tag 43 "
           "must stay independently findable";
}

// ── W-P2-1 Iter path: stale-carry parallel (no error channel → truncation) ────
// Same stale-carry frame via the Iter API. Pre-fix: 96 read as a 5-byte counted
// value absorbs "43=", tag 43 never yielded. Fixed: pending cleared, 43 yielded.
TEST(HostileInputHardening, StalePendingClearedIterYields43) {
    auto buf = make_raw_frame(
        "95=5\x01"
        "58=hello\x01"
        "96=x\x01"
        "43=Z\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    MessageView<access_mode::Iter> mv{*fv};
    bool saw_43 = false;
    std::string_view v96;
    bool saw_96 = false;
    // Safety cap: the pre-fix advance() sets done_=true on a malformed stop
    // WITHOUT advancing pos_ to buf.size(), so `it == end()` never holds and a
    // naive range-for infinite-loops (a latent Iter-API DoS the fix also
    // closes). Cap iterations so the RED case fails cleanly instead of hanging.
    std::size_t guard = 0;
    for (auto it = mv.begin(); !(it == mv.end()) && guard < 1000; ++it, ++guard) {
        auto const& f = *it;
        if (f.tag == 96) {
            saw_96 = true;
            v96 = as_sv(f.value);
        }
        if (f.tag == 43) {
            saw_43 = true;
        }
    }
    ASSERT_LT(guard, 1000U) << "Iter range-for must terminate on a malformed frame (no hang)";
    ASSERT_TRUE(saw_96);
    EXPECT_EQ(v96, "x") << "Iter: 96 must be a normal SOH-delimited value after clear-pending";
    EXPECT_TRUE(saw_43) << "Iter: tag 43 must be yielded, not absorbed by a stale pending Length";
}

// ── W-P3-2: predictable mix() lets a crafted collision set force a present ─────
// required field to read false-absent; a per-process seed defeats it. ──────────
//
// mix() and overlay_cap_for() are replicated here so the witness can compute a
// slot-colliding tag set for a KNOWN seed (installed via the test hook). This
// both exercises the kMaxBuildProbe skip branch deterministically (discharging
// the standing coverage waiver at offset_table_error_path_test.cpp:11) and
// proves the seed makes a precomputed collision set useless under any other
// seed.

std::uint32_t mix_ref(std::uint16_t tag, std::uint32_t seed) {
    std::uint32_t h = 2166136261U ^ seed;
    h = (h ^ (static_cast<std::uint32_t>(tag) & 0xFFU)) * 16777619U;
    h = (h ^ ((static_cast<std::uint32_t>(tag) >> 8U) & 0xFFU)) * 16777619U;
    return h;
}
std::size_t overlay_cap_ref(std::size_t n) {
    std::size_t want = ((n * 5U) / 4U) + 1U;
    std::size_t cap = 8U;
    while (cap < want) {
        cap <<= 1U;
    }
    return cap;
}
// Collect `k` distinct tags whose mix()&mask lands on `target`'s slot for `seed`.
std::vector<std::uint16_t> colliding_tags(std::uint16_t target, std::uint32_t seed,
                                          std::uint32_t mask, std::size_t k) {
    std::uint32_t const target_slot = mix_ref(target, seed) & mask;
    std::vector<std::uint16_t> out;
    for (std::uint32_t t = 1; t <= 65535U && out.size() < k; ++t) {
        auto tag = static_cast<std::uint16_t>(t);
        if (tag == 8 || tag == 9 || tag == 10 || tag == target) {
            continue;  // reserved envelope tags + the target itself
        }
        if ((mix_ref(tag, seed) & mask) == target_slot) {
            out.push_back(tag);
        }
    }
    return out;
}
// Build a frame whose body is `k` collision tags (each "<tag>=x") followed by
// the target ("<target>=V"). Envelope (8/9/10) + k + target = k+4 entries → the
// overlay caps at 256 (mask 255) for k=150, matching the mask used to collide.
std::vector<std::byte> make_collision_frame(std::vector<std::uint16_t> const& tags,
                                            std::uint16_t target) {
    std::string body;
    for (auto t : tags) {
        body += std::to_string(t);
        body += "=x\x01";
    }
    body += std::to_string(target);
    body += "=V\x01";
    return make_raw_frame(body);
}

constexpr std::uint32_t kSeedA = 0x9E3779B9U;
constexpr std::uint32_t kSeedB = 0x12345678U;
constexpr std::uint16_t kTarget = 100;
constexpr std::size_t kCollisions = 150;  // > kMaxBuildProbe(128), margin for envelope

TEST(HostileInputHardening, CraftedCollisionSetSkipsTargetUnderMatchingSeed) {
    fixpp::wire::detail::set_overlay_seed_for_testing(kSeedA);
    auto tags = colliding_tags(kTarget, kSeedA, /*mask=*/255U, kCollisions);
    ASSERT_EQ(tags.size(), kCollisions) << "test setup: not enough colliding tags found";

    auto buf = make_collision_frame(tags, kTarget);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    ASSERT_TRUE(t.build_status().has_value());
    ASSERT_EQ(overlay_cap_ref(t.size()), 256U)
        << "test setup: overlay cap must be 256 so mask=255 matches the collision set";

    // The target IS present in document order (entries_)...
    bool present_in_entries = false;
    for (auto const& e : t.entries()) {
        if (e.tag == kTarget) {
            present_in_entries = true;
        }
    }
    ASSERT_TRUE(present_in_entries) << "target field must be in the eager entry list";

    // ...but the crafted collisions push it past kMaxBuildProbe, so find() (the
    // O(1) overlay path) reports it ABSENT — the availability defect this test
    // exercises (kMaxBuildProbe skip branch, coverage waiver discharged).
    auto found = t.find(kTarget);
    EXPECT_FALSE(found.has_value())
        << "under the matching seed, the crafted collision set forces the present "
           "target field to read false-absent (kMaxBuildProbe skip)";
}

TEST(HostileInputHardening, CraftedCollisionSetDefeatedByDifferentSeed) {
    // The attacker precomputes a collision set for seed A...
    auto tags = colliding_tags(kTarget, kSeedA, /*mask=*/255U, kCollisions);
    ASSERT_EQ(tags.size(), kCollisions);
    auto buf = make_collision_frame(tags, kTarget);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    // ...but the process actually runs under seed B: the tags scatter, no
    // 128-probe cluster forms at the target's slot, and the target is findable.
    fixpp::wire::detail::set_overlay_seed_for_testing(kSeedB);
    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    ASSERT_TRUE(t.build_status().has_value());

    auto found = t.find(kTarget);
    EXPECT_TRUE(found.has_value())
        << "a collision set precomputed for a different seed must NOT force the "
           "target absent — the per-process seed defeats predictable HashDoS";
    if (found.has_value()) {
        EXPECT_EQ(found->tag, kTarget);
    }
}

// ── #221 (2): an inflated group count must not inflate the reserve bound ──────
// group_slices_reserve_bound() sums the DECLARED instance count of every
// top-level group count-field to size the one-shot group_slices_ reservation.
// A hostile frame can declare an arbitrarily large count (453=999 here) while
// carrying a single instance; without the clamp that number is what gets
// reserved out of the fixed inbound parse arena — the arena_fit exhaustion mode
// (PR #181), reachable from the wire. The clamp holds it at entries_.size(),
// which is a valid upper bound because every instance needs at least one entry.
//
// Mutation-proof: with the clamp removed the bound is 999, not the entry count.
TEST(HostileInputHardening, InflatedGroupCountClampedToEntryCountInReserveBound) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .set_group_first(453, 448)
        .add_group_member(453, 447);

    // NoPartyIDs(453) declares 999 instances; exactly one is present.
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=999\x01"
        "448=PA\x01"
        "447=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    fixpp::wire::Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());
    auto const& t = mv->offsets();

    // Setup precondition: the declared count must exceed the entry count, or the
    // clamp is not the thing being exercised.
    ASSERT_LT(t.size(), 999U) << "test setup: declared count must exceed the entry count";

    EXPECT_EQ(fixpp::wire::reserve_bound_access_for_testing::get(t),
              static_cast<std::uint32_t>(t.size()))
        << "a malicious declared instance count must be clamped to the entry "
           "count, not reserved verbatim out of the fixed parse arena";
}

}  // namespace
