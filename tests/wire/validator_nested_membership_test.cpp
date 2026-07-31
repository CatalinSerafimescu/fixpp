// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/validator_nested_membership_test.cpp — 072-nested-group-hardening (FR-010 / L-063-3)
//
// The strict `dictionary_driven_validator` Step-3 group walk was FLAT: it
// queried every group at the ROOT context (`{msg_type, path=[]}`), so a
// genuinely NESTED reused group whose members/delimiter are registered only
// under its real parent path missed the context store and degraded to the
// bare-`no_tag` (first-seen) resolution — one level worse than the typed
// accessor (which at least pushed once). This witness pins the fix: a
// nesting-aware, query-before-push recursive descent that resolves each group
// under its real parent path, so typed read and strict validation agree on
// depth-≥2 membership.
//
// Discriminator: grandchild group 555 is registered with the CORRECT delimiter
// (602) ONLY under its real context path ("i",[296,295],555); its bare/first-seen
// registration carries a DIFFERENT delimiter (299). A valid MassQuote-shaped
// message (296->295->555, delimiter 602) is:
//   - REJECTED by the flat walk (it queries 555 at root -> bare delimiter 299 ->
//     the wire's 602 after 555= looks like a missing delimiter) -> RED.
//   - ACCEPTED by the nesting-aware walk (it queries 555 under [296,295] ->
//     context delimiter 602 -> matches) -> GREEN.

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

std::vector<std::byte> make_frame(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) sum += c;
    char chk[16]{};
    std::snprintf(chk, sizeof(chk), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk;
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

// Hand-built table_view: correct membership under real context paths; grandchild
// 555's bare (first-seen) delimiter DIVERGES from its context delimiter, so the
// flat root-context walk mis-validates it.
table_view make_nested_membership_dict() {
    table_view tv;
    for (std::uint16_t const t :
         {std::uint16_t{8}, std::uint16_t{9}, std::uint16_t{10}, std::uint16_t{35},
          std::uint16_t{296}, std::uint16_t{302}, std::uint16_t{295}, std::uint16_t{299},
          std::uint16_t{132}, std::uint16_t{133}, std::uint16_t{555}, std::uint16_t{602},
          std::uint16_t{603}}) {
        tv.add_valid("i", t);
    }

    std::array<std::uint16_t, 0> const root{};
    std::array<std::uint16_t, 1> const p296{296};
    std::array<std::uint16_t, 2> const p296_295{296, 295};

    // Context store — correct membership at each real path.
    tv.set_group_first_ctx("i", root, 296, 302);
    for (std::uint16_t const m : {295, 299, 132, 133, 555, 602, 603}) {
        tv.add_group_member_ctx("i", root, 296, m);
    }
    tv.set_group_first_ctx("i", p296, 295, 299);
    for (std::uint16_t const m : {132, 133, 555, 602, 603}) {
        tv.add_group_member_ctx("i", p296, 295, m);
    }
    tv.set_group_first_ctx("i", p296_295, 555, 602);  // CORRECT grandchild delimiter
    tv.add_group_member_ctx("i", p296_295, 555, 603);

    // Bare/first-seen store — 296/295 correct, but 555's delimiter WRONG (299),
    // the divergence the nesting-aware walk must resolve via context.
    tv.set_group_first(296, 302);
    for (std::uint16_t const m : {295, 299, 132, 133, 555, 602, 603}) tv.add_group_member(296, m);
    tv.set_group_first(295, 299);
    for (std::uint16_t const m : {132, 133, 555, 602, 603}) tv.add_group_member(295, m);
    tv.set_group_first(555, 299);  // WRONG bare delimiter (first-seen variant)
    tv.add_group_member(555, 602).add_group_member(555, 603);
    return tv;
}

}  // namespace

// FR-010 (L-063-3): a valid depth-3 nested message validates ONLY when the
// validator resolves each nested group under its real parent path. Mutation-
// proven RED on the flat root-context walk, GREEN after the nesting-aware
// recursive rewrite.
TEST(ValidatorNestedMembership, Depth2ContextMissUnderFlatWalk) {
    auto tv = make_nested_membership_dict();
    dictionary_driven_validator v{std::move(tv)};

    // Valid MassQuote-shaped message: one QuoteSet, one QuoteEntry, one Leg.
    auto frame = make_frame(
        "35=i\x01"
        "296=1\x01"
        "302=SET0\x01"
        "295=1\x01"
        "299=E0\x01"
        "132=10.10\x01"
        "133=10.20\x01"
        "555=1\x01"
        "602=LEGX\x01"
        "603=SRCX\x01");

    std::array<std::byte, 8192> stack{};
    std::pmr::monotonic_buffer_resource arena{stack.data(), stack.size(),
                                              std::pmr::null_memory_resource()};
    auto mv = parse_index(frame, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    auto const result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << "a valid depth-3 nested message must validate; the flat root-context walk queries "
           "grandchild 555 at root -> bare delimiter 299 -> false-rejects the real 602-delimited "
           "leg. Nesting-aware query-before-push resolves 555 under [296,295] -> delimiter 602. "
           "slot="
        << (result.has_value() ? -1 : static_cast<int>(result.error()));
}

// FR-010 Gate B r2 fix: `validate_group_level` was a LEVEL SCANNER
// (`while (i < end)` over the whole remaining range) but was ALSO invoked as
// the "consume this ONE nested group" routine from a parent instance's body.
// After consuming the nested group's own declared instances, the recursive
// call kept scanning to `end` -- over-running into sibling fields, the next
// parent delimiter, and LATER PARENT INSTANCES. A multi-instance parent
// (296=2) where EACH instance contains a nested group (295) reproduces this:
// the first instance's 295-recursion swallows BOTH instances' 295 groups and
// returns i=end, so 296's own actual_count stays 1 against declared_count=2
// -> false-reject (wire_required_field_missing, slot 38). Mutation-proven RED
// on the pre-split validator.hpp, GREEN after the consume_group/
// validate_group_level split.
TEST(ValidatorNestedMembership, MultiInstanceParentWithNestedGroupNotFalseRejected) {
    auto tv = make_nested_membership_dict();
    dictionary_driven_validator v{std::move(tv)};

    // Valid MassQuote-shaped message: TWO QuoteSet instances, each with its
    // own QuoteEntry+Leg nested groups fully self-contained.
    auto frame = make_frame(
        "35=i\x01"
        "296=2\x01"
        "302=SET0\x01"
        "295=1\x01"
        "299=E0\x01"
        "132=10.10\x01"
        "133=10.20\x01"
        "555=1\x01"
        "602=LEGX\x01"
        "603=SRCX\x01"
        "302=SET1\x01"
        "295=1\x01"
        "299=E1\x01"
        "132=20.10\x01"
        "133=20.20\x01"
        "555=1\x01"
        "602=LEGY\x01"
        "603=SRCY\x01");

    std::array<std::byte, 8192> stack{};
    std::pmr::monotonic_buffer_resource arena{stack.data(), stack.size(),
                                              std::pmr::null_memory_resource()};
    auto mv = parse_index(frame, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    auto const result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << "a valid 296=2 message (each instance with a self-contained nested 295/555 group) "
           "must validate; the pre-split validate_group_level over-runs past the first "
           "instance's nested-295 recursion into the second instance, under-counting 296's "
           "actual_count against declared_count=2. slot="
        << (result.has_value() ? -1 : static_cast<int>(result.error()));
}

// Sibling reproduction at a DIFFERENT nesting shape: a single top-level
// parent (296=1) whose nested group itself has MULTIPLE instances (295=2),
// where only the FIRST instance contains a deeper nested group (555). The
// pre-split recursion into 555 over-runs past 295's own remaining instances,
// under-counting 295's actual_count against declared_count=2.
TEST(ValidatorNestedMembership, MultiEntryNestedGroupFirstEntryDeeperNestedNotFalseRejected) {
    auto tv = make_nested_membership_dict();
    dictionary_driven_validator v{std::move(tv)};

    auto frame = make_frame(
        "35=i\x01"
        "296=1\x01"
        "302=SET0\x01"
        "295=2\x01"
        "299=E0\x01"
        "132=10.10\x01"
        "133=10.20\x01"
        "555=1\x01"
        "602=LEGX\x01"
        "603=SRCX\x01"
        "299=E1\x01"
        "132=20.10\x01"
        "133=20.20\x01");

    std::array<std::byte, 8192> stack{};
    std::pmr::monotonic_buffer_resource arena{stack.data(), stack.size(),
                                              std::pmr::null_memory_resource()};
    auto mv = parse_index(frame, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    auto const result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << "a valid 295=2 nested group (only the first entry containing a deeper 555 group) "
           "must validate; the pre-split validate_group_level over-runs past the first entry's "
           "555-recursion into 295's second entry, under-counting 295's actual_count against "
           "declared_count=2. slot="
        << (result.has_value() ? -1 : static_cast<int>(result.error()));
}

namespace {

// Minimal synthetic dialect reproducing L-063-3 residual (b): a NumInGroup
// tag (600) reused across TWO different top-level messages with GENUINELY
// DIFFERENT declared delimiters. Message "U1" (lexically first in
// <messages>, so first-seen) declares NoX(600) with delimiter FieldA(610).
// Message "U2" declares the SAME NoX(600) with delimiter FieldB(620). Per
// `Dictionary::as_table_view()` (dictionary.cpp :421), the per-context
// `group_first` is populated from `group_first_field(no_tag)` — the
// dictionary's single GLOBAL, first-seen `GroupRef.first_field_tag` — so
// BOTH contexts' stored delimiter is 610 (U1's), even though U2's real
// declared delimiter is 620. No nested group is involved, so the 072
// nested==parent delimiter load guard (finalize(), parent_group_no_tag==0
// here) does not fire and this dialect loads cleanly.
constexpr std::string_view kReusedTagDivergentDelimXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    // 083 T033a: the three framing tags were MISSING from this fixture. Every
    // frame `make_frame` builds carries BeginString(8)/BodyLength(9)/
    // CheckSum(10), and validate()'s Step 1 checks EVERY field against
    // `valid_tags_for(mt)` before the group walk ever runs — so an undeclared
    // framing tag rejects with wire_unexpected_tag(42) regardless of the
    // delimiter. The omission was invisible while this case was GTEST_SKIPped:
    // the fixture was authored but never executed. (Sibling synthetic dialects
    // that DO run — e.g. consume_group_nested_delim_test.cpp's — declare all
    // three.)
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='600' name='NoX' type='NUMINGROUP'/>)"
    R"(<field number='610' name='FieldA' type='STRING'/>)"
    R"(<field number='620' name='FieldB' type='STRING'/>)"
    R"(</fields>)"
    R"(<messages>)"
    R"(<message name='U1Msg' msgtype='U1' msgcat='app'>)"
    R"(<field name='BeginString' required='N'/>)"
    R"(<field name='BodyLength' required='N'/>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='CheckSum' required='N'/>)"
    R"(<group name='NoX' required='N'>)"
    R"(<field name='FieldA' required='N'/>)"  // U1's delimiter = 610 (first-seen globally)
    R"(</group></message>)"
    R"(<message name='U2Msg' msgtype='U2' msgcat='app'>)"
    R"(<field name='BeginString' required='N'/>)"
    R"(<field name='BodyLength' required='N'/>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='CheckSum' required='N'/>)"
    R"(<group name='NoX' required='N'>)"
    R"(<field name='FieldB' required='N'/>)"  // U2's REAL delimiter = 620, corrupted to 610
    R"(</group></message>)"
    R"(</messages></fix>)";

}  // namespace

// L-063-3 residual (b) — GTEST_SKIP pending witness, un-skip lifecycle mirrors
// L-063-2 (spec/behaviors-and-limitations.md). 072 fixed ONLY the flat-walk
// residual (a); the global-delimiter residual (b) is UNCHANGED and reachable
// on shipped dictionaries (FIX44 NoMDEntries(268): 269 in MDFullGrp/35=W vs
// 279 in MDIncGrp/35=X). This synthetic dialect reproduces the same class at
// minimum scale via a real `XmlLoader::load_from_string` + `as_table_view()`
// (not a hand-built table_view, per Codex's original observation that a
// hand-built table_view proves only that the recursion CAN consume good
// data, not that the real loader SUPPLIES it).
// 083 T033a: UN-SKIPPED. The `GTEST_SKIP()` that stood here named this feature
// as its own unblocking condition verbatim — "Un-skip when the per-context-
// delimiter follow-up lands (tracked via L-063-3)" — and T070 retires L-063-3.
// Leaving it parked would have retired the B&L row while its own pin stayed
// skipped, which `ctest` reports as success. T031 makes `as_table_view()`
// source the delimiter from Entity 2 per context, so the assert below now
// passes on merit rather than enshrining the bug.
TEST(ValidatorNestedMembership, PerContextDelimiterResidual_L063_3b) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource dict_mr{buf.data(), buf.size()};
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kReusedTagDivergentDelimXml, &dict_mr);
    auto tv = dict.as_table_view();
    dictionary_driven_validator v{std::move(tv)};

    // U2's own declaration says the group's delimiter is FieldB(620); a
    // single valid instance is 600=1, 620=<value>. This is a CONFORMING
    // message under U2's real dialect declaration.
    auto frame = make_frame(
        "35=U2\x01"
        "600=1\x01"
        "620=B0\x01");

    std::array<std::byte, 8192> stack{};
    std::pmr::monotonic_buffer_resource arena{stack.data(), stack.size(),
                                              std::pmr::null_memory_resource()};
    auto mv = parse_index(frame, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    auto const result = v.validate(mv, &scratch_mr, nullptr);
    // CURRENT (pre-fix) behavior: result.has_value() == false — the corrupted
    // global delimiter (610, from U1) is checked against the wire's real
    // first field after 600= (620), which mismatches -> wire_required_field_missing.
    // POST-FIX target (per-context delimiter correctly resolves U2 -> 620):
    EXPECT_TRUE(result.has_value())
        << "a valid U2 instance (delimiter FieldB=620 per U2's own declaration) must validate "
           "once as_table_view() derives a per-context delimiter instead of the dictionary's "
           "single global first-seen GroupRef.first_field_tag; slot="
        << (result.has_value() ? -1 : static_cast<int>(result.error()));
}
