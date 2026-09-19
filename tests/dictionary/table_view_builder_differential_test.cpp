// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/table_view_builder_differential_test.cpp — fixpp#456, §6 seam 4.
//
// `build()` must produce the same table the old direct population did. Sixteen
// near-identical one-line forwarders are exactly the shape where an argument gets
// forwarded in the wrong order or to the wrong sibling, and every one of those
// mistakes COMPILES wherever the two parameters share a type.
//
// The comparand is a REAL dictionary: `Dictionary::as_table_view()` over FIX44,
// which itself populates a builder now, so the ground truth is read OUT of that view
// and replayed INTO a hand-driven builder. A forwarder that transposes its arguments
// then disagrees with the dictionary about the same question.
//
// ⚠️ A DIFFERENTIAL IS BLIND TO A SWAP THAT IS SYMMETRIC IN THE VALUES IT WAS GIVEN.
// Every assertion below that covers a same-typed parameter pair — (no_tag, member),
// (no_tag, first), (length_tag, data_tag) — uses DISTINCT values and asserts the
// DIRECTION, not merely that something was registered. Where the two parameters have
// different types (msg_type/tag, tag/field_type) a transposition does not compile and
// needs no arm.
//
// ⚠️ NOT EVERY FORWARDER HAS A DICTIONARY COMPARAND. `add_enum`'s code list,
// `set_multi_value`'s bit and `add_fixt_framing_tag` are either absent from FIX44's
// relevant slice or reachable only through an accessor that collapses them, so those
// are written and read back through the view's own `const` accessors. That is not a
// differential — it is a round-trip — and it is labelled as such rather than counted
// as one.
//
// Mutation procedure, per forwarder class: transpose the two arguments inside ONE
// forwarder body in table_view.hpp (e.g. `tv_.set_group_first(first, no_tag)`), or
// point one forwarder at its sibling (`add_valid` → `add_required`), and confirm THIS
// binary goes RED. Do not verify with the group arms alone: they share a comparand.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fixpp::dict::field_type;
using fixpp::dict::table_view;
using fixpp::dict::table_view_builder;

std::vector<std::uint16_t> to_vec(std::span<std::uint16_t const> s) {
    return std::vector<std::uint16_t>(s.begin(), s.end());
}

// NoPartyIDs in FIX44 — a group whose delimiter is NOT its lowest-tag member
// (PartyID(448) vs PartyIDSource(447)) and is distinct from the count tag, which is
// what makes the direction of `set_group_first` observable at all.
constexpr std::uint16_t kNoPartyIDs = 453;

struct Fix44 {
    std::vector<std::byte> buf = std::vector<std::byte>(4U * 1024U * 1024U);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    fixpp::dict::Dictionary dict =
        fixpp::dict::XmlLoader{}.load(std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml",
                                      &mr);
    table_view view = dict.as_table_view();
};

// ── the differential: replay the dictionary's own answers through the builder ──

TEST(TableViewBuilderDifferential, GroupStructureMatchesTheDictionary) {
    Fix44 f;
    table_view const& ref = f.view;

    auto const first = ref.group_first_field(kNoPartyIDs);
    auto const members = to_vec(ref.group_member_tags(kNoPartyIDs));
    auto const required = to_vec(ref.group_required_members(kNoPartyIDs));

    // Non-vacuity first: every assertion below is satisfied by an empty replay.
    ASSERT_NE(first, std::uint16_t{0}) << "FIX44 NoPartyIDs(453) must have a delimiter";
    ASSERT_FALSE(members.empty()) << "FIX44 NoPartyIDs(453) must have members";

    // The transposition arms below need a member tag DISTINCT from both the no_tag and
    // the delimiter: a probe keyed on a tag that happens to equal one of them cannot
    // tell a transposed registration from a correct one. ⚠️ `members.front()` is NOT
    // that tag — the bare member list is INSERTION-ordered, and `set_group_first` adds
    // the delimiter as a member before the membership loop runs, so the first entry is
    // the delimiter itself. Select, do not assume.
    std::uint16_t probe_member = 0;
    for (auto const m : members) {
        if (m != first && m != kNoPartyIDs) {
            probe_member = m;
            break;
        }
    }
    ASSERT_NE(probe_member, std::uint16_t{0})
        << "every member of NoPartyIDs(453) is the delimiter or the count tag — the "
           "transposition arms below would be vacuous";

    table_view_builder b;
    b.set_group_first(kNoPartyIDs, first);
    for (auto const m : members) {
        b.add_group_member(kNoPartyIDs, m);
    }
    for (auto const r : required) {
        b.add_group_required_member(kNoPartyIDs, r);
    }
    table_view const built = std::move(b).build();

    EXPECT_EQ(built.group_first_field(kNoPartyIDs), first);
    EXPECT_EQ(to_vec(built.group_member_tags(kNoPartyIDs)), members);
    EXPECT_EQ(to_vec(built.group_required_members(kNoPartyIDs)), required);

    // The transposition arms, stated as their own assertions: a
    // `set_group_first(first, no_tag)` would register the group under `first`, and an
    // `add_group_member(member, no_tag)` would register the members under each member.
    EXPECT_EQ(built.group_first_field(first), std::uint16_t{0})
        << "set_group_first registered the group under its delimiter — arguments transposed";
    EXPECT_TRUE(built.group_member_tags(probe_member).empty())
        << "add_group_member registered members under a member tag — arguments transposed";
}

TEST(TableViewBuilderDifferential, ContextScopedGroupMatchesTheDictionary) {
    Fix44 f;
    table_view const& ref = f.view;

    // A context the dictionary itself registered: NoPartyIDs at top level in
    // NewOrderSingle(D). Read the context's own answers, then replay them.
    constexpr std::string_view kMsgType = "D";
    std::span<std::uint16_t const> const kTopLevel{};

    auto const ctx_first = ref.group_first_field(kMsgType, kTopLevel, kNoPartyIDs);
    auto const ctx_members = to_vec(ref.group_member_tags(kMsgType, kTopLevel, kNoPartyIDs));
    ASSERT_NE(ctx_first, std::uint16_t{0})
        << "FIX44 NewOrderSingle(D) must register NoPartyIDs(453) at top level";
    ASSERT_FALSE(ctx_members.empty());

    table_view_builder b;
    b.set_group_first_ctx(kMsgType, kTopLevel, kNoPartyIDs, ctx_first);
    for (auto const m : ctx_members) {
        b.add_group_member_ctx(kMsgType, kTopLevel, kNoPartyIDs, m);
    }
    table_view const built = std::move(b).build();

    EXPECT_EQ(built.group_first_field(kMsgType, kTopLevel, kNoPartyIDs), ctx_first);
    EXPECT_EQ(to_vec(built.group_member_tags(kMsgType, kTopLevel, kNoPartyIDs)), ctx_members);

    // Direction: the context key is (msg_type, path, no_tag). A forwarder that
    // transposed no_tag and first would register under `ctx_first`.
    EXPECT_EQ(built.group_first_field(kMsgType, kTopLevel, ctx_first), std::uint16_t{0})
        << "set_group_first_ctx registered under its delimiter — arguments transposed";
    // And it must not have leaked into a DIFFERENT message's context.
    EXPECT_EQ(built.group_first_field_exact("A", kTopLevel, kNoPartyIDs).has_value(), false)
        << "the context key ignored msg_type";
}

TEST(TableViewBuilderDifferential, MembershipAndTypesMatchTheDictionary) {
    Fix44 f;
    table_view const& ref = f.view;

    auto const logon_required = to_vec(ref.required_fields("A"));
    ASSERT_FALSE(logon_required.empty()) << "FIX44 Logon(A) must have required fields";

    table_view_builder b;
    for (auto const t : logon_required) {
        b.add_required("A", t);              // chain form
        b.set_type(t, ref.field_type_of(t));  // chain form
    }
    // The void-returning twins, on a second msg_type, so a forwarder pointed at its
    // sibling shows up as a msg_type that answers for the wrong message.
    b.add_required_tag("D", 11);
    b.add_valid_tag("D", 38);
    b.set_field_type(38, ref.field_type_of(38));
    table_view const built = std::move(b).build();

    EXPECT_EQ(to_vec(built.required_fields("A")), logon_required);
    for (auto const t : logon_required) {
        EXPECT_TRUE(built.field_valid_for("A", t)) << "add_required must also mark the tag valid";
        EXPECT_EQ(built.field_type_of(t), ref.field_type_of(t)) << "field_type_of(" << t << ")";
        EXPECT_FALSE(built.field_valid_for("D", t))
            << "tag " << t << " reached the wrong msg_type — add_required forwarded wrongly";
    }

    // add_required_tag REQUIRES; add_valid_tag only VALIDATES. A forwarder pointed at
    // its sibling collapses that distinction.
    EXPECT_EQ(to_vec(built.required_fields("D")), std::vector<std::uint16_t>{11});
    EXPECT_TRUE(built.field_valid_for("D", 38));
    EXPECT_EQ(built.field_type_of(38), ref.field_type_of(38));
}

TEST(TableViewBuilderDifferential, LengthDataPairMatchesTheDictionaryInBothDirections) {
    Fix44 f;
    table_view const& ref = f.view;

    // RawDataLength(95) -> RawData(96) is declared in FIX44; read it rather than
    // assuming it, and refuse to run on a dictionary that does not carry it.
    constexpr std::uint16_t kRawDataLength = 95;
    auto const data_tag = ref.length_pair_data_tag(kRawDataLength);
    ASSERT_NE(data_tag, std::uint16_t{0})
        << "FIX44 must declare a Data partner for RawDataLength(95), or this arm is vacuous";
    ASSERT_NE(data_tag, kRawDataLength);
    EXPECT_EQ(ref.data_pair_length_tag(data_tag), kRawDataLength);

    table_view_builder b;
    b.set_length_pair_data_tag(kRawDataLength, data_tag);
    EXPECT_EQ(b.length_pair_data_tag(kRawDataLength), data_tag)
        << "the builder's scalar readback disagrees with what it just stored";
    table_view const built = std::move(b).build();

    EXPECT_EQ(built.length_pair_data_tag(kRawDataLength), data_tag);
    EXPECT_EQ(built.data_pair_length_tag(data_tag), kRawDataLength);
    // The transposition: a swapped forwarder stores (data -> length) forward.
    EXPECT_EQ(built.length_pair_data_tag(data_tag), std::uint16_t{0})
        << "set_length_pair_data_tag forwarded (data, length) — arguments transposed";
    EXPECT_EQ(built.has_nonstandard_pair(), ref.has_nonstandard_pair())
        << "95/96 is a standard pair; the flag must not be set by it";
}

// ── round-trips, for the forwarders no dictionary comparand reaches ─────────
// Labelled as round-trips rather than counted as differentials: the expected value is
// the one this test wrote, not one an independent producer computed.

TEST(TableViewBuilderRoundTrip, EnumMultiValueAndFramingForwardersLandWhereTheySay) {
    table_view_builder b;

    // add_enum: an owned, sorted, deduped code list, checked through enum_valid.
    b.add_enum(40, "1");
    b.add_enum(40, "2");
    // set_multi_value: without the bit, a space-separated value is rejected whole.
    b.add_enum(215, "A");
    b.add_enum(215, "B");
    b.set_multi_value(215, true);
    // add_fixt_framing_tag: the validator-private framing surface, deliberately
    // SEPARATE from valid_/types_ (D-1) — so its readback is is_fixt_framing_tag and
    // field_type_of_with_framing, and field_type_of must NOT see it.
    b.add_fixt_framing_tag(1128, field_type::String);

    table_view const built = std::move(b).build();

    auto bytes = [](std::string_view s) {
        return std::span<std::byte const>{reinterpret_cast<std::byte const*>(s.data()), s.size()};
    };

    EXPECT_TRUE(built.enum_valid(40, bytes("1")));
    EXPECT_TRUE(built.enum_valid(40, bytes("2")));
    EXPECT_FALSE(built.enum_valid(40, bytes("3"))) << "add_enum landed on the wrong tag";
    EXPECT_FALSE(built.enum_valid(40, bytes("1 2")))
        << "tag 40 has no multi-value bit — set_multi_value landed on the wrong tag";

    EXPECT_TRUE(built.enum_valid(215, bytes("A B")))
        << "set_multi_value(215) did not reach tag 215";

    EXPECT_TRUE(built.is_fixt_framing_tag(1128));
    EXPECT_EQ(built.field_type_of_with_framing(1128), field_type::String);
    EXPECT_FALSE(built.field_valid_for("A", 1128))
        << "add_fixt_framing_tag must not touch valid_ (D-1)";
}

// ── the builder's own shape ────────────────────────────────────────────────

TEST(TableViewBuilderRoundTrip, AnUntouchedBuilderBuildsTheEmptyView) {
    table_view_builder b;
    table_view const built = std::move(b).build();

    EXPECT_FALSE(built.field_valid_for("A", 49));
    EXPECT_TRUE(built.required_fields("A").empty());
    EXPECT_EQ(built.group_first_field(kNoPartyIDs), std::uint16_t{0});
    EXPECT_FALSE(built.has_nonstandard_pair());
}

}  // namespace
