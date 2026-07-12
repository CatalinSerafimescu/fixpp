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
#include <fixpp/dict/table_view.hpp>
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

    auto const result = v.validate(mv, &scratch_mr);
    EXPECT_TRUE(result.has_value())
        << "a valid depth-3 nested message must validate; the flat root-context walk queries "
           "grandchild 555 at root -> bare delimiter 299 -> false-rejects the real 602-delimited "
           "leg. Nesting-aware query-before-push resolves 555 under [296,295] -> delimiter 602. "
           "slot="
        << (result.has_value() ? -1 : static_cast<int>(result.error()));
}
