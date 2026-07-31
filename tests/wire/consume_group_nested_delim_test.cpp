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
