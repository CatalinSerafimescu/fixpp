// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/consume_group_nested_delim_test.cpp
//
// 083-group-delimiter-resolution T010 (W-1 / W-1a; FR-007, FR-008, FR-009,
// FR-021b; contracts/consume_group.md) — the RED witness for #208's B-2
// two-instance repro (quickstart.md §2):
//
//   35=X | 100=2 | 200=1 201=A | 200=1 201=B
//
// Outer group 100 (NoOuter) is delimited by 200 — which is ITSELF a nested
// group's (NoInner's) own count tag. `consume_group` (include/fixpp/wire/
// validator.hpp:357-406) consumes the instance-opening delimiter with a
// bare `++i` (:362) instead of descending into the nested group, so the
// second instance's opening tag (the second `200=1`) is never reached: the
// scanner treats the whole two-instance message as containing only one
// instance, `actual_count(1) != declared_count(2)`, and the message is
// REJECTED. The one-instance form of the SAME shape happens to pass by
// coincidence (`actual_count(1) == declared_count(1)`) without the
// descent ever running — which is exactly why this went unnoticed
// (contracts/consume_group.md "Problem").
//
// Two witnesses, per the gate rewritten at Gate A round 1
// (contracts/consume_group.md "Gate between the two"):
//
//   W-1  — the repro on a HAND-BUILT table_view. Hand-built fixtures never
//          populate `group_ctx_` (table_view.hpp:346-349), so this exercises
//          ONLY the bare/legacy fallback at table_view.hpp:364.
//   W-1a — `ConsumeGroupNestedDelim.NestedDelimiterDescendsOnPopulatedContextStore`.
//          The IDENTICAL shape, but with the outer group (NoOuter/100)
//          registered under a NON-EMPTY parent path ([900], via a real
//          `XmlLoader::load_from_string` + `as_table_view()`), so the
//          descend-at-delimiter probe resolves through `group_ctx_`
//          (table_view.hpp:360-364) rather than through the bare fallback.
//          W-1 alone is insufficient: an implementation that descends
//          correctly on the bare path and is broken on the context-keyed
//          one would pass W-1 and only fail once Phase 3 registers real
//          nested-delimiter contexts (232+30 of them) — at the most
//          expensive point to unwind.
//
// Both assertions below encode the POST-FIX target (both instance counts
// accepted) so this pin stays meaningful once T017 lands; TODAY the
// two-instance case is expected RED (rejected) and the one-instance case
// GREEN (already accepted) — the asymmetry itself is the defect signature.
//
// Anchors:
//   tasks:     specs/083-group-delimiter-resolution/tasks.md T010
//   quickstart: specs/083-group-delimiter-resolution/quickstart.md §2
//   contract:  specs/083-group-delimiter-resolution/contracts/consume_group.md
//              (Problem, C-4.1/C-4.2, W-1/W-1a)

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
using fixpp::dict::table_view;
using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

// ── Shared wire-frame helpers (mirrors tests/wire/delimiter_divergence_wire_test.cpp) ──
std::string make_checksum_field(unsigned chk) {
    std::string s = "10=";
    s.push_back(static_cast<char>('0' + ((chk / 100U) % 10U)));
    s.push_back(static_cast<char>('0' + ((chk / 10U) % 10U)));
    s.push_back(static_cast<char>('0' + (chk % 10U)));
    s.push_back('\x01');
    return s;
}

std::vector<std::byte> make_frame(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::string full = pre + make_checksum_field(sum % 256U);
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

MessageView<access_mode::Index> parse_index(std::vector<std::byte> const& buf,
                                            std::pmr::monotonic_buffer_resource& arena) {
    auto fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv.has_value()) {
        ADD_FAILURE() << "make_frame_view failed";
        return {};
    }
    Parser<access_mode::Index> parser{};
    auto mv = parser.parse(*fv, &arena);
    if (!mv.has_value()) {
        ADD_FAILURE() << "parser.parse failed";
        return {};
    }
    return std::move(*mv);
}

// ── W-1 fixture: hand-built table_view, no context store population ────────
// msg_type "X". Outer group NoOuter(100), delimiter NoInner(200) — itself the
// nested group NoInner's own count tag, delimiter InnerField(201). This is
// EXACTLY the shape `set_group_first` mirrors #208's B-2: the outer group's
// delimiter is registered AND added as a member of the outer group (as today's
// resolution would see it), while 201 is registered only as NoInner's member
// — never as a member of NoOuter itself, matching contract C-4.1's "the tags
// [inside the nested group] are not members of the outer group" framing.
table_view make_bare_nested_delim_dict() {
    table_view tv;
    for (std::uint16_t const t : {std::uint16_t{8}, std::uint16_t{9}, std::uint16_t{10},
                                  std::uint16_t{35}, std::uint16_t{100}, std::uint16_t{200},
                                  std::uint16_t{201}}) {
        tv.add_valid("X", t);
    }
    tv.set_group_first(100, 200);  // NoOuter: delimiter = NoInner's own count tag
    tv.set_group_first(200, 201);  // NoInner: delimiter = InnerField
    return tv;
}

// ── W-1a fixture: a real loaded dictionary, so NoOuter registers under a
// NON-EMPTY parent path ([900]) in group_ctx_ ──────────────────────────────
// msg_type "Y". A wrapper group NoWrap(900), delimiter Wrapped(901), whose
// SECOND member is the outer group NoOuter(100) — so NoOuter's context path
// is [900], not []. NoOuter's own first child is the nested group NoInner
// (200) directly (no intervening scalar field), so NoOuter's delimiter is
// itself NoInner's count tag — the IDENTICAL #208 B-2 shape as W-1, but
// resolved via `group_ctx_` (a non-empty parent path) rather than the bare
// fallback.
constexpr std::string_view kNestedDelimContextXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='900' name='NoWrap' type='NUMINGROUP'/>)"
    R"(<field number='901' name='Wrapped' type='STRING'/>)"
    R"(<field number='100' name='NoOuter' type='NUMINGROUP'/>)"
    R"(<field number='200' name='NoInner' type='NUMINGROUP'/>)"
    R"(<field number='201' name='InnerField' type='STRING'/>)"
    R"(</fields>)"
    R"(<messages>)"
    R"(<message name='YMsg' msgtype='Y' msgcat='app'>)"
    R"(<field name='BeginString' required='N'/>)"
    R"(<field name='BodyLength' required='N'/>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='CheckSum' required='N'/>)"
    R"(<group name='NoWrap' required='N'>)"
    R"(<field name='Wrapped' required='N'/>)"
    R"(<group name='NoOuter' required='N'>)"
    R"(<group name='NoInner' required='N'>)"
    R"(<field name='InnerField' required='N'/>)"
    R"(</group></group></group>)"
    R"(</message>)"
    R"(</messages></fix>)";

// ── W-3 fixture: a depth-parameterised delimiter CHAIN (FR-009, US2 scenario
// 4; contracts/consume_group.md "W-3") ──────────────────────────────────────
// Generalises the W-1 shape across many nesting levels instead of one: group
// tag T_i's delimiter is ALSO group tag T_{i+1}'s own count tag
// (`set_group_first(T_i, T_{i+1})`), chained `total_groups` levels deep and
// terminated by a plain scalar leaf tag T_{total_groups}. msg_type "Z". Tag
// numbering starts at kChainBaseTag, well clear of W-1/W-1a's tags above
// (each fixture below builds its own table_view, so there is no actual
// collision risk — kept visually distinct only).
constexpr std::uint16_t kChainBaseTag = 1000;

table_view make_chain_dict(std::uint16_t base_tag, std::size_t total_groups) {
    table_view tv;
    for (std::uint16_t const t : {std::uint16_t{8}, std::uint16_t{9}, std::uint16_t{10},
                                  std::uint16_t{35}}) {
        tv.add_valid("Z", t);
    }
    // total_groups group-count tags (base_tag .. base_tag+total_groups-1)
    // plus one terminal leaf scalar (base_tag+total_groups).
    for (std::size_t i = 0; i <= total_groups; ++i) {
        tv.add_valid("Z", static_cast<std::uint16_t>(base_tag + i));
    }
    for (std::size_t i = 0; i < total_groups; ++i) {
        tv.set_group_first(static_cast<std::uint16_t>(base_tag + i),
                           static_cast<std::uint16_t>(base_tag + i + 1));
    }
    return tv;
}

// One instance's worth of the nested chain BELOW the outermost group (tags
// base_tag+1 .. base_tag+total_groups-1, each declaring count=1, one
// instance each), terminated by the leaf scalar base_tag+total_groups =
// `leaf_value`. The outermost group's OWN count field (base_tag=<declared
// count>) is prepended by the caller once, not per-instance.
std::string chain_tail(std::uint16_t base_tag, std::size_t total_groups,
                       std::string_view leaf_value) {
    std::string body;
    for (std::size_t i = 1; i < total_groups; ++i) {
        body += std::to_string(static_cast<unsigned>(base_tag + i)) + "=1\x01";
    }
    body += std::to_string(static_cast<unsigned>(base_tag + total_groups)) + "=" +
           std::string(leaf_value) + "\x01";
    return body;
}

}  // namespace

// ============================================================================
// W-1 — #208 B-2 repro on a hand-built table_view (bare fallback).
//
// Records BOTH halves of the asymmetry the contract names: the two-instance
// form (the witness) and the one-instance form (the regression guard) —
// contracts/consume_group.md's W-1 table, rows 1-2.
// ============================================================================
TEST(ConsumeGroupNestedDelim, NestedDelimiterTwoInstanceRejectedTodayOneInstanceAcceptedAsymmetry) {
    // Two instances: outer group 100 declares count=2, each instance opened
    // by its delimiter 200 (itself NoInner's count tag, with one nested
    // InnerField(201) member). POST-FIX TARGET: accepted with 2 instances.
    // TODAY: the un-descended `++i` at validator.hpp:362 consumes only the
    // FIRST instance's delimiter without entering NoInner, so the scan sees
    // one non-member tag (201) immediately after, counts actual_count=1
    // against declared_count=2, and rejects.
    {
        auto tv = make_bare_nested_delim_dict();
        dictionary_driven_validator v{std::move(tv)};
        auto buf = make_frame("35=X\x01"
                              "100=2\x01"
                              "200=1\x01" "201=A\x01"
                              "200=1\x01" "201=B\x01");
        std::pmr::monotonic_buffer_resource arena;
        auto mv = parse_index(buf, arena);
        std::array<std::byte, 2048> scratch_buf{};
        std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                       std::pmr::null_memory_resource()};
        auto result = v.validate(mv, &scratch_mr, nullptr);
        EXPECT_TRUE(result.has_value())
            << "POST-FIX TARGET (W-1, C-4.1): the two-instance #208 B-2 shape — outer group "
               "100's delimiter (200) is itself nested group 200's own count tag — must be "
               "ACCEPTED with an instance count of 2. TODAY this is expected REJECTED because "
               "consume_group's instance-opening `++i` (validator.hpp:362) does not descend "
               "into the nested group, so the second instance is never reached. error="
            << (result.has_value() ? 0 : static_cast<int>(result.error()));
    }

    // One instance of the SAME shape: today ACCEPTED "by accident"
    // (actual_count(1) happens to equal declared_count(1) even though the
    // scan never verified 201=A as belonging to anything) — the regression
    // guard half of the asymmetry (contracts/consume_group.md W-1 row 2:
    // "same, 1 instance | ACCEPTED | ACCEPTED (must not regress)").
    {
        auto tv = make_bare_nested_delim_dict();
        dictionary_driven_validator v{std::move(tv)};
        auto buf = make_frame("35=X\x01"
                              "100=1\x01"
                              "200=1\x01" "201=A\x01");
        std::pmr::monotonic_buffer_resource arena;
        auto mv = parse_index(buf, arena);
        std::array<std::byte, 2048> scratch_buf{};
        std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                       std::pmr::null_memory_resource()};
        auto result = v.validate(mv, &scratch_mr, nullptr);
        EXPECT_TRUE(result.has_value())
            << "REGRESSION GUARD (W-1 row 2): the one-instance form of the SAME shape is "
               "ACCEPTED both before and after the fix (today by coincidence — actual_count(1) "
               "== declared_count(1) without the descent ever running; after the fix, by "
               "correctly consuming the single instance). error="
            << (result.has_value() ? 0 : static_cast<int>(result.error()));
    }
}

// ============================================================================
// FAIL-CLOSED propagation at the DELIMITER position (validator.hpp:390-391).
//
// Added at /speckit-verify Step 4, which found this leg uncovered. C-4.1's
// descent is a symmetry repair: the post-delimiter MEMBER loop already had a
// nested descent with a `if (!nested) return nested;` failure propagation
// (validator.hpp:415-416), and T017 added the twin at the instance-opening
// DELIMITER position. Coverage showed the member-position propagation firing
// (`[True: 1, False: 14]`) while the new delimiter-position one read
// `[True: 0, False: 74]` — the fix was made at both sites, but only one site
// had a test driving its failure arm. That is the same half-restructure shape
// this feature's own comments call out, surfacing one level up in the TEST
// dimension: symmetric code needs symmetric coverage, or the untested half is
// a fail-closed path no one has ever watched fail.
//
// The shape: outer NoOuter(100) declares ONE instance and opens with delimiter
// 200, which IS NoInner's own count tag (FR-021 class (c) — the whole point of
// 083). NoInner declares one instance, but the frame ends before its delimiter
// 201 ever appears, so the NESTED consume_group hits its "first instance must
// open with the delimiter" guard (validator.hpp:287-290) and returns
// unexpected. The outer call must PROPAGATE that failure rather than swallow
// it and continue scanning — a swallow here would silently accept a malformed
// nested group sitting at the delimiter position.
TEST(ConsumeGroupNestedDelim, NestedFailureAtDelimiterPositionPropagatesFailClosed) {
    auto tv = make_bare_nested_delim_dict();
    dictionary_driven_validator v{std::move(tv)};
    auto buf = make_frame("35=X\x01"
                          "100=1\x01"
                          "200=1\x01");
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, arena);
    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value())
        << "FAIL-CLOSED (validator.hpp:390-391): NoInner(200) declares one instance but the "
           "frame ends before its delimiter 201, so the nested consume_group invoked from the "
           "DELIMITER position fails. The outer consume_group must propagate that failure. "
           "Accepting here would mean a malformed nested group at the instance-opening "
           "delimiter is silently swallowed — the exact silent-accept this arm exists to "
           "prevent.";
    EXPECT_EQ(result.error(), error::wire_required_field_missing)
        << "the propagated slot must be the nested call's own error, unmodified";
    EXPECT_EQ(ref_tag, 201)
        << "ref_tag must name the NESTED group's missing delimiter (201), proving the error "
           "came from the nested consume_group and was propagated — not re-manufactured by the "
           "outer frame, which would have reported the outer delimiter 200 instead.";
}

// ============================================================================
// W-1a — the IDENTICAL shape on a POPULATED context store (Gate A round 1's
// rewritten gate; contracts/consume_group.md "the real Phase-2 exit
// witness"). NoOuter(100) registers under parent path [900] (NoWrap), so
// C-4.1's descent probe (`dict_.group_first_field(ctx.msg_type, child_path,
// t)`) must resolve through `group_ctx_`, not the bare fallback.
// ============================================================================
TEST(ConsumeGroupNestedDelim, NestedDelimiterDescendsOnPopulatedContextStore) {
    std::vector<std::byte> dict_buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource dict_mr{dict_buf.data(), dict_buf.size()};
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kNestedDelimContextXml, &dict_mr);
    auto tv = dict.as_table_view();

    constexpr std::uint16_t kNoOuter = 100;
    std::array<std::uint16_t, 1> const path900{900};

    // Discriminator (required by T010's brief; mirrors T006's lookup-miss
    // check, contracts/group_ctx_delims.md): the context-keyed
    // `group_member_tags("Y", [900], 100)` MUST return a DIFFERENT span than
    // the bare `group_member_tags(100)` — i.e. it must genuinely hit
    // `group_ctx_` (table_view.hpp:373-377), not silently fall through to
    // the SAME bare storage the 1-arg overload returns (table_view.hpp:377).
    // Equal pointers here would mean this witness exercises only the same
    // bare path as W-1, defeating the entire point of W-1a.
    ASSERT_NE(tv.group_member_tags("Y", path900, kNoOuter).data(),
             tv.group_member_tags(kNoOuter).data())
        << "W-1a requires the context-keyed group_member_tags(\"Y\", [900], 100) lookup to HIT "
           "group_ctx_ under NoOuter's real (non-empty) parent path, not fall through to the "
           "bare/global fallback — otherwise this case would silently degrade into a second "
           "copy of W-1's bare-fallback witness instead of exercising the context-keyed leg of "
           "C-4.1 that the Gate A round 1 rewrite exists to close.";

    dictionary_driven_validator v{std::move(tv)};

    // Same #208 B-2 two-instance shape as W-1, wrapped one level deeper so
    // NoOuter(100) sits under parent path [900] rather than at the root.
    auto buf = make_frame("35=Y\x01"
                          "900=1\x01" "901=W1\x01"
                          "100=2\x01"
                          "200=1\x01" "201=A\x01"
                          "200=1\x01" "201=B\x01");
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, arena);
    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    auto result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << "POST-FIX TARGET (W-1a, the real Phase-2 exit witness per contracts/consume_group.md): "
           "the IDENTICAL #208 B-2 shape as W-1, but with NoOuter(100) registered under a "
           "non-empty parent path ([900], resolved through group_ctx_ rather than the bare "
           "fallback), must be ACCEPTED with an instance count of 2. TODAY this is expected "
           "REJECTED for the same reason as W-1: consume_group's instance-opening `++i` does "
           "not descend into the nested group regardless of which store resolves the delimiter. "
           "error="
        << (result.has_value() ? 0 : static_cast<int>(result.error()));
}

// ============================================================================
// W-3 — Depth: a shape at the nesting cap is bounded and well-defined, not
// unbounded recursion (FR-009, US2 scenario 4; contracts/consume_group.md
// "W-3": "Build the W-1 shape nested to the engine's supported limit and
// assert the validator returns a defined rejection rather than recursing
// without bound, and that the previously-passing behaviour at depths below
// the cap is unchanged.").
//
// C-4.3: the delimiter-descend T017 will add reuses the EXISTING
// `can_descend`/K=16 guard — `group_context::parent_path`, a fixed
// `std::array<std::uint16_t, 16>` (group_view.hpp:45); `pushed()` clamps at
// depth 16 rather than overflow the array (group_view.hpp:56-63) — already
// used for post-delimiter member descent (validator.hpp:302-306). "No new
// branch is introduced by W-3" (contract footnote): this case therefore
// CANNOT discriminate "rejected by the depth cap" from "rejected because
// descent never happens" by error code — both dispositions return the SAME
// `wire_required_field_missing` `consume_group` already returns for every
// declared/actual instance-count mismatch (see the PRE-IMPLEMENTATION
// DISPOSITION comment in the first block below for the honest limitation
// this implies). What this case DOES pin: a maximally-deep chain terminates
// with a DEFINED rejection (never a crash/hang/unbounded recursion), and the
// identical mechanism kept one level SHORTER (so every level fits within the
// K=16 cap) is correctly ACCEPTED once T017 lands — the "unchanged below the
// cap" regression guard.
//
// Depth accounting — group_view.hpp's `group_context` is the validator's OWN
// depth authority (C-4.2 "reuses the existing depth guard"), numerically
// identical to but a DISTINCT mechanism from offset_table.hpp:340's
// `kMaxGroupDepth` (which bounds the UNRELATED typed-read extent walk,
// pinned separately by W-10a per this file's contract footnote — "Its
// offset-table twin ... is a distinct hazard"). K=16 either way. Processing
// group G_k (the k-th chain link, 0-indexed from the outermost) runs with
// `ctx.depth == k`; its `pushed()` call (forming the child context used to
// decide whether to descend into G_{k+1}) succeeds — produces depth k+1 —
// for k = 0..15, and CLAMPS (stays at depth 16, `can_descend` false) at
// k == 16. So a chain of 17 group levels (G_0..G_16 — the outermost plus 16
// further nested delimiter-group levels) descends fully within the cap; an
// 18th level (G_17) is the first the cap prevents descending INTO.
TEST(ConsumeGroupNestedDelim, NestedDelimiterAtDepthCapIsBoundedNotUnbounded) {
    // ── AT THE CAP: 18 group levels (G_0..G_17) — G_16's own recursive call
    // (ctx.depth == 16) cannot descend into G_17, even though G_17 IS a
    // registered nested group. The outer group (G_0, declared count=2, the
    // SAME two-instance witness shape as W-1) must still terminate with a
    // DEFINED rejection rather than recursing without bound.
    {
        constexpr std::size_t kAtCapGroups = 18;
        auto tv = make_chain_dict(kChainBaseTag, kAtCapGroups);
        dictionary_driven_validator v{std::move(tv)};
        std::string body = "35=Z\x01" + std::to_string(kChainBaseTag) + "=2\x01" +
                           chain_tail(kChainBaseTag, kAtCapGroups, "A") +
                           chain_tail(kChainBaseTag, kAtCapGroups, "B");
        auto buf = make_frame(body);
        std::pmr::monotonic_buffer_resource arena;
        auto mv = parse_index(buf, arena);
        std::array<std::byte, 4096> scratch_buf{};
        std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                       std::pmr::null_memory_resource()};
        auto result = v.validate(mv, &scratch_mr, nullptr);

        // PRE-IMPLEMENTATION DISPOSITION (recorded, not the RED target):
        // TODAY, with no delimiter descent at ANY level, this shape is
        // ALREADY rejected — for the WRONG reason. `consume_group` never
        // attempts to descend into G_0's own delimiter (base_tag+1), so the
        // scan never reaches G_16 or the depth cap at all; it fails at the
        // very first comparison after G_0's bare-consumed delimiter, exactly
        // like W-1's un-descended two-instance case. Post-T017, the SAME
        // shape reaches the cap genuinely and is STILL rejected — via the
        // ordinary actual-count-vs-declared-count mismatch this function
        // already uses everywhere, because G_16 cannot descend into G_17 and
        // G_17's members are left dangling, which desynchronises every
        // enclosing level's instance boundary. NEITHER disposition assigns a
        // distinct error code to "hit the cap", so the assertion below is
        // honestly non-discriminating between "rejected by the cap" and
        // "rejected because descent never happens": it pins only that the
        // result is a DEFINED rejection with THIS exact error, both before
        // and after T017 — never a crash/hang/unbounded recursion.
        ASSERT_FALSE(result.has_value())
            << "W-3 (at cap): an 18-group-level delimiter chain — one level "
               "beyond the K=16 cap — must still be REJECTED, not recurse "
               "without bound.";
        EXPECT_EQ(result.error(), error::wire_required_field_missing)
            << "W-3 (at cap): the depth-cap disposition is the SAME "
               "wire_required_field_missing consume_group already returns for "
               "every declared/actual instance-count mismatch (C-4.3 — no new "
               "error branch is introduced by the cap).";
    }

    // ── BELOW THE CAP: 17 group levels (G_0..G_16) — the deepest chain that
    // fits ENTIRELY within the K=16 cap (G_16's own delimiter is the plain
    // leaf scalar, never itself requiring further descent). POST-FIX TARGET:
    // ACCEPTED with 2 instances — the same asymmetry W-1/W-1a demonstrate at
    // shallower depth, i.e. "the previously-passing behaviour at depths
    // below the cap is unchanged" (contract W-3) by construction, since this
    // is the identical mechanism, just deeper. TODAY: rejected, for the same
    // reason as the at-cap case above (no descent at any level yet) — the
    // asymmetry itself (REJECTED today, ACCEPTED after T017) is what makes
    // this half of the case non-vacuous.
    {
        constexpr std::size_t kBelowCapGroups = 17;
        auto tv = make_chain_dict(kChainBaseTag, kBelowCapGroups);
        dictionary_driven_validator v{std::move(tv)};
        std::string body = "35=Z\x01" + std::to_string(kChainBaseTag) + "=2\x01" +
                           chain_tail(kChainBaseTag, kBelowCapGroups, "A") +
                           chain_tail(kChainBaseTag, kBelowCapGroups, "B");
        auto buf = make_frame(body);
        std::pmr::monotonic_buffer_resource arena;
        auto mv = parse_index(buf, arena);
        std::array<std::byte, 4096> scratch_buf{};
        std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                       std::pmr::null_memory_resource()};
        auto result = v.validate(mv, &scratch_mr, nullptr);
        EXPECT_TRUE(result.has_value())
            << "W-3 (below cap, C-4.3 regression guard): a 17-group-level "
               "chain — the deepest that fits entirely within the K=16 cap — "
               "must be ACCEPTED with 2 instances once T017 lands (identical "
               "mechanism to W-1/W-1a, just deeper); behaviour below the cap "
               "must be unchanged by the cap's existence. TODAY expected "
               "REJECTED (no descent at any level yet). error="
            << (result.has_value() ? 0 : static_cast<int>(result.error()));
    }
}
