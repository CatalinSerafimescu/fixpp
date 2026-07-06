// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/lookup_test.cpp
//
// AC-D1..D8 parameterized over the seven currently-loadable v1.0 FIX versions:
// the four codegen-target dictionaries (FIX42, FIX44, FIX50SP2, FIXT11) plus
// the three runtime-XML-only versions vendored for D-005/006 (FIX43, FIX50,
// FIX50SP1 — spec 002 §10 F1). FIX 4.0/4.1 (D-004) are excluded pending loader
// legacy-type support (see the INSTANTIATE note). GoogleTest TEST_P fixture;
// each version is loaded fresh per test via a 4 MiB monotonic_buffer_resource.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// ---------------------------------------------------------------------------
// Per-version parameters
// ---------------------------------------------------------------------------

struct VersionParam {
    std::string filename;
    fixpp::dict::session_version expected_version;

    // AC-D6: message types that MUST appear in messages()
    std::vector<std::string> required_msg_types;

    // AC-D6 cross-version isolation: msg types that MUST NOT appear
    std::vector<std::string> forbidden_msg_types;

    // AC-D7: NoXxx tags that MUST have group_first_field() != 0
    std::vector<std::uint16_t> required_group_no_tags;

    // AC-D3: whether ClOrdID (tag 11) is expected to be declared
    bool has_clordid;

    // AC-D4 Parties component
    //   true  → component("Parties").has_value() must be true
    //   false → component("Parties") must be nullopt
    //   nullopt → tolerate either (FIXT11)
    std::optional<bool> parties_expected;

    // AC-D4 Instrument component: all four versions declare it
    bool has_instrument;
};

namespace {

fixpp::dict::Dictionary load_small_dictionary() {
    auto* mr = std::pmr::new_delete_resource();
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='11' name='ClOrdID' type='STRING'/>)"
        R"(<field number='35' name='MsgType' type='STRING'/>)"
        R"(<field number='453' name='NoPartyIDs' type='NUMINGROUP'/>)"
        R"(<field number='55' name='Symbol' type='STRING'/>)"
        R"(<field number='555' name='NoLegs' type='NUMINGROUP'/>)"
        R"(</fields>)"
        R"(<components>)"
        R"(<component name='Empty'/>)"
        R"(<component name='Instrument'><field name='Symbol' required='N'/></component>)"
        R"(</components>)"
        R"(<messages>)"
        R"(<message name='Heartbeat' msgtype='0' msgcat='admin'>)"
        R"(<field name='MsgType' required='Y'/>)"
        R"(</message>)"
        R"(<message name='NewOrderSingle' msgtype='D' msgcat='app'>)"
        R"(<field name='ClOrdID' required='Y'/>)"
        R"(<component name='Instrument' required='N'/>)"
        R"(<group name='NoPartyIDs' required='N'/>)"
        R"(<group name='NoLegs' required='N'/>)"
        R"(</message>)"
        R"(</messages>)"
        R"(</fix>)";
    return fixpp::dict::XmlLoader{}.load_from_string(kXml, mr);
}

}  // namespace

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class DictionaryLookupFixture : public ::testing::TestWithParam<VersionParam> {
protected:
    void SetUp() override {
        // 4 MiB arena; no heap allocations expected during Dictionary::load.
        buf_.resize(4u * 1024u * 1024u);
        mr_ = std::make_unique<std::pmr::monotonic_buffer_resource>(buf_.data(), buf_.size());

        auto const xml_path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / GetParam().filename;

        fixpp::dict::XmlLoader loader{};
        dict_ = std::make_unique<fixpp::dict::Dictionary>(loader.load(xml_path, mr_.get()));
    }

    fixpp::dict::Dictionary const& dict() const { return *dict_; }

private:
    std::vector<std::byte> buf_;
    std::unique_ptr<std::pmr::monotonic_buffer_resource> mr_;
    std::unique_ptr<fixpp::dict::Dictionary> dict_;
};

// ---------------------------------------------------------------------------
// TEST_P: VersionMatches (AC-L4 / which_session_version)
// ---------------------------------------------------------------------------

TEST_P(DictionaryLookupFixture, VersionMatches) {
    EXPECT_EQ(dict().which_session_version(), GetParam().expected_version);
}

// ---------------------------------------------------------------------------
// TEST_P: MessagesNonEmptyAndContainsHeadlines (AC-D5 + AC-D6)
// ---------------------------------------------------------------------------

TEST_P(DictionaryLookupFixture, MessagesNonEmptyAndContainsHeadlines) {
    auto const msgs = dict().messages();
    ASSERT_FALSE(msgs.empty()) << "messages() must be non-empty";

    std::set<std::string> found;
    for (auto const& e : msgs) {
        found.emplace(e.msg_type);
    }

    for (auto const& required : GetParam().required_msg_types) {
        EXPECT_TRUE(found.count(required) > 0)
            << "Expected msg_type '" << required << "' not found in " << GetParam().filename;
    }

    for (auto const& forbidden : GetParam().forbidden_msg_types) {
        EXPECT_TRUE(found.count(forbidden) == 0)
            << "Forbidden msg_type '" << forbidden << "' found in " << GetParam().filename;
    }
}

// ---------------------------------------------------------------------------
// TEST_P: MessagesIsBytewiseSorted (AC-D5)
// ---------------------------------------------------------------------------

TEST_P(DictionaryLookupFixture, MessagesIsBytewiseSorted) {
    auto const msgs = dict().messages();

    auto bytewise_less = [](fixpp::dict::MessageEntry const& a,
                            fixpp::dict::MessageEntry const& b) {
        return std::lexicographical_compare(
            a.msg_type.begin(), a.msg_type.end(), b.msg_type.begin(), b.msg_type.end(),
            [](char x, char y) {
                return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
            });
    };

    EXPECT_TRUE(std::is_sorted(msgs.begin(), msgs.end(), bytewise_less))
        << "messages() span is not bytewise-sorted in " << GetParam().filename;
}

// ---------------------------------------------------------------------------
// TEST_P: FieldByNameClOrdID (AC-D3)
// ---------------------------------------------------------------------------

TEST_P(DictionaryLookupFixture, FieldByNameClOrdID) {
    if (GetParam().has_clordid) {
        auto tag = dict().field_by_name("ClOrdID");
        ASSERT_TRUE(tag.has_value())
            << "field_by_name(\"ClOrdID\") should return 11 for " << GetParam().filename;
        EXPECT_EQ(*tag, std::uint16_t{11});
    }

    // Case-sensitivity: lower-case must always be nullopt
    auto lower = dict().field_by_name("clordid");
    EXPECT_FALSE(lower.has_value())
        << "field_by_name(\"clordid\") must be nullopt (case-sensitive) for "
        << GetParam().filename;

    // Truly unknown name must be nullopt regardless of version
    auto unknown = dict().field_by_name("definitely_unknown_xyz_999");
    EXPECT_FALSE(unknown.has_value())
        << "Unknown name lookup must return nullopt for " << GetParam().filename;
}

// ---------------------------------------------------------------------------
// TEST_P: FieldRefAndFieldAgreement (AC-D1 ↔ AC-D2)
// ---------------------------------------------------------------------------

TEST_P(DictionaryLookupFixture, FieldRefAndFieldAgreement) {
    using fixpp::dict::field_presence;

    // Logon ("A") is present in all four versions.
    std::string_view const logon_type = "A";

    // Tag 49 (SenderCompID) is a standard header field; declared on every
    // message in all FIX versions including FIXT11.
    std::uint16_t const sender_tag = 49;

    auto const fr = dict().field_ref(logon_type, sender_tag);

    if (fr.rule != field_presence::NotDeclared) {
        // AC-D2: field() must agree
        auto const opt = dict().field(logon_type, sender_tag);
        ASSERT_TRUE(opt.has_value())
            << "field() must have_value() when field_ref().rule != NotDeclared"
            << " for " << GetParam().filename;
        EXPECT_EQ(opt->rule, fr.rule)
            << "field().rule must match field_ref().rule for " << GetParam().filename;
    }

    // Absent tag: 9999 is never declared in any real dictionary
    auto const absent_fr = dict().field_ref(logon_type, 9999u);
    EXPECT_EQ(absent_fr.rule, field_presence::NotDeclared)
        << "field_ref for absent tag must return NotDeclared for " << GetParam().filename;

    auto const absent_opt = dict().field(logon_type, 9999u);
    EXPECT_FALSE(absent_opt.has_value())
        << "field() for absent tag must be nullopt for " << GetParam().filename;
}

// ---------------------------------------------------------------------------
// TEST_P: ComponentParties (AC-D4 — cross-version isolation)
// ---------------------------------------------------------------------------

TEST_P(DictionaryLookupFixture, ComponentParties) {
    auto const& p = GetParam();

    // All four shipped versions declare the Instrument component.
    if (p.has_instrument) {
        auto const inst = dict().component("Instrument");
        EXPECT_TRUE(inst.has_value()) << "Instrument component must be declared in " << p.filename;
        // R1 (F1.1): payload must be non-zero for declared components.
        if (inst.has_value()) {
            EXPECT_GT(inst->field_count, std::uint16_t{0})
                << "Instrument component field_count must be > 0 in " << p.filename;
        }
    } else {
        // FIX42 and FIXT11: Instrument must NOT be declared (R5 alignment).
        EXPECT_FALSE(dict().component("Instrument").has_value())
            << "Instrument component must NOT be declared in " << p.filename;
    }

    // Parties: FIX42 predates it (added in 4.3); FIX44/50SP2 carry it.
    // FIXT11: no Parties (only HopGrp and MsgTypeGrp — R5 fix: pin to false).
    if (p.parties_expected.has_value()) {
        bool const expected = *p.parties_expected;
        auto const parties = dict().component("Parties");
        bool const actual = parties.has_value();
        if (expected) {
            EXPECT_TRUE(actual) << "Parties component must be declared in " << p.filename;
            // R1 (F1.1): payload check for Parties when declared.
            if (actual) {
                EXPECT_GT(parties->field_count, std::uint16_t{0})
                    << "Parties component field_count must be > 0 in " << p.filename;
            }
        } else {
            EXPECT_FALSE(actual) << "Parties component must NOT be declared in " << p.filename;
        }
    }

    // R7 (P1-B): parent_component_id semantics — "0 if top-level; otherwise the
    // enclosing component's 1-based id" per [2c §4.2] / data-model.md Entity 2.
    // Tested on FIX44 where SecAltIDGrp is nested inside Instrument's body.
    if (p.expected_version == fixpp::dict::session_version::v44 ||
        p.expected_version == fixpp::dict::session_version::v50sp2) {
        // Instrument is a top-level component — its parent_component_id must be 0.
        auto const ins = dict().component("Instrument");
        ASSERT_TRUE(ins.has_value()) << "Instrument must be declared in " << p.filename;
        EXPECT_EQ(ins->parent_component_id, std::uint16_t{0})
            << "Instrument is top-level; parent_component_id must be 0 in " << p.filename;

        // SecAltIDGrp is referenced inside Instrument's body — its
        // parent_component_id must equal Instrument's 1-based id
        // (component_id is 0-based; parent_component_id is 1-based).
        auto const sag = dict().component("SecAltIDGrp");
        ASSERT_TRUE(sag.has_value()) << "SecAltIDGrp must be declared in " << p.filename;
        EXPECT_EQ(sag->parent_component_id, static_cast<std::uint16_t>(ins->component_id + 1u))
            << "SecAltIDGrp is nested inside Instrument; "
               "parent_component_id must equal Instrument's 1-based id in "
            << p.filename;
    }

    // Unknown component must be nullopt in every version
    EXPECT_FALSE(dict().component("NonExistentComponent_xyz_999").has_value())
        << "Unknown component must return nullopt for " << p.filename;

    // group() with dummy no_tag must be nullopt
    EXPECT_FALSE(dict().group(9999u).has_value())
        << "group(9999) must return nullopt for " << p.filename;

    // R1 (F1.2): GroupRef payload must be non-zero for declared groups.
    // Verify field_count on well-known groups in versions that declare them.
    for (auto const no_tag : p.required_group_no_tags) {
        auto const gr = dict().group(no_tag);
        ASSERT_TRUE(gr.has_value()) << "group(" << no_tag << ") must be declared in " << p.filename;
        EXPECT_GT(gr->field_count, std::uint16_t{0})
            << "group(" << no_tag << ") field_count must be > 0 in " << p.filename;
    }

    // R6 (P1-A / F1.1): component_fields() must be reachable and consistent.
    // The side table exposed by component_fields(name) must match the
    // field_count advertised on the ComponentRef per [2c §4.2] / spec.md Q4.
    if (p.has_instrument) {
        auto const inst = dict().component("Instrument");
        ASSERT_TRUE(inst.has_value()) << "Instrument component must be declared in " << p.filename;
        auto const span = dict().component_fields("Instrument");
        EXPECT_EQ(span.size(), static_cast<std::size_t>(inst->field_count))
            << "component_fields(\"Instrument\").size() must equal "
               "Instrument.field_count in "
            << p.filename;
        EXPECT_FALSE(span.empty())
            << "component_fields(\"Instrument\") must not be empty in " << p.filename;
        // First field of Instrument in FIX44/FIX50SP2 is Symbol (tag 55).
        if (!span.empty()) {
            EXPECT_EQ(span[0].tag, std::uint16_t{55})
                << "component_fields(\"Instrument\")[0].tag must be Symbol(55) in " << p.filename;
        }
    }

    // component_fields() on an unknown name must return empty span.
    EXPECT_TRUE(dict().component_fields("NonExistentComponent_xyz_999").empty())
        << "component_fields() for unknown name must return empty span in " << p.filename;
}

// ---------------------------------------------------------------------------
// TEST_P: GroupDelimiterTags (AC-D7)
// ---------------------------------------------------------------------------

TEST_P(DictionaryLookupFixture, GroupDelimiterTags) {
    for (auto const no_tag : GetParam().required_group_no_tags) {
        EXPECT_NE(dict().group_first_field(no_tag), std::uint16_t{0})
            << "group_first_field(" << no_tag << ") must be non-zero in " << GetParam().filename;
    }

    // R6 (P1-A / F1.2): group_fields() side table must be reachable and
    // consistent with the field_count on the GroupRef per [2c §4.2] / spec.md Q4.
    for (auto const no_tag : GetParam().required_group_no_tags) {
        auto const gr = dict().group(no_tag);
        ASSERT_TRUE(gr.has_value())
            << "group(" << no_tag << ") must be declared in " << GetParam().filename;
        auto const span = dict().group_fields(no_tag);
        EXPECT_EQ(span.size(), static_cast<std::size_t>(gr->field_count))
            << "group_fields(" << no_tag
            << ").size() must equal "
               "group.field_count in "
            << GetParam().filename;
        EXPECT_FALSE(span.empty())
            << "group_fields(" << no_tag << ") must not be empty in " << GetParam().filename;
    }

    // group_fields() on a non-existent no_tag must return empty span.
    EXPECT_TRUE(dict().group_fields(9999u).empty())
        << "group_fields(9999) must return empty span in " << GetParam().filename;
}

// ---------------------------------------------------------------------------
// TEST_P: CrossVersionIsolation (spec.md §4.2 AC-D6 sub-bullet)
// ---------------------------------------------------------------------------
// Already covered by MessagesNonEmptyAndContainsHeadlines via forbidden list,
// but add explicit named assertions for the two canonical cases.

TEST_P(DictionaryLookupFixture, CrossVersionIsolation) {
    auto const msgs = dict().messages();

    auto has_msg_type = [&](std::string_view mt) {
        for (auto const& e : msgs) {
            if (e.msg_type == mt) return true;
        }
        return false;
    };

    auto const& p = GetParam();

    if (p.expected_version == fixpp::dict::session_version::vt11) {
        EXPECT_FALSE(has_msg_type("D")) << "FIXT11 must not contain NewOrderSingle (D)";
    }

    if (p.expected_version == fixpp::dict::session_version::v50sp2) {
        EXPECT_TRUE(has_msg_type("V")) << "FIX50SP2 must contain MarketDataRequest (V)";
    }
}

// ---------------------------------------------------------------------------
// TEST_P: RequiredFieldsAndLengthPairs (covers Dictionary::required_fields()
// + Dictionary::length_pair_data_tag(); both were uncovered before T035).
// ---------------------------------------------------------------------------

TEST_P(DictionaryLookupFixture, RequiredFieldsAndLengthPairs) {
    auto const& p = GetParam();

    // Call required_fields("A") on every version (exercises find_msg_required
    // + the required-pool offset path including the empty-run branch). The
    // specific required-field set differs by version: pre-FIXT (4.2/4.4/4.3)
    // and FIXT11 carry session fields (49/56) on Logon; the FIX 5.0 family
    // (5.0 / 5.0SP1 / 5.0SP2) application overlays leave Logon required-free —
    // indeed carry no Logon message at all — because session fields moved to
    // FIXT — see [FIX50SP2 §6].
    auto const logon_required = dict().required_fields("A");

    bool const app_only_no_logon =
        p.expected_version == fixpp::dict::session_version::v50 ||
        p.expected_version == fixpp::dict::session_version::v50sp1 ||
        p.expected_version == fixpp::dict::session_version::v50sp2;
    if (!app_only_no_logon) {
        bool has_sender = false;
        bool has_target = false;
        for (auto const tag : logon_required) {
            if (tag == 49u) has_sender = true;
            if (tag == 56u) has_target = true;
        }
        EXPECT_TRUE(has_sender) << "Logon required_fields() must contain SenderCompID (49) in "
                                << p.filename;
        EXPECT_TRUE(has_target) << "Logon required_fields() must contain TargetCompID (56) in "
                                << p.filename;
    }

    // Unknown msg_type → empty span (covers the find_msg_required miss path).
    auto const unknown = dict().required_fields("definitely_not_a_msg_type");
    EXPECT_TRUE(unknown.empty()) << "required_fields() for unknown msg_type must be empty span in "
                                 << p.filename;

    // length_pair_data_tag: RawDataLength (95) is paired with RawData (96) in
    // every FIX version that declares both. FIXT11 is session-only and may
    // omit RawData; tolerate that. This covers length_pair_data_tag_impl's
    // walk-and-match path.
    auto const raw_data_tag = dict().length_pair_data_tag(95u);
    if (p.expected_version != fixpp::dict::session_version::vt11) {
        EXPECT_EQ(raw_data_tag, std::uint16_t{96})
            << "length_pair_data_tag(95) must return 96 (RawData) in " << p.filename;
    }

    // Unknown length tag → 0 (covers the no-match path through fields_).
    EXPECT_EQ(dict().length_pair_data_tag(std::uint16_t{9999}), std::uint16_t{0})
        << "length_pair_data_tag(9999) must return 0 in " << p.filename;

    // R2 (F1.3): component-scoped LENGTH/DATA adjacency captured from the
    // global <fields> declaration order. EncodedLegIssuerLen(618)→
    // EncodedLegIssuer(619) lives inside the InstrumentLeg component in FIX44;
    // the old message-only walk never descended into <component> nodes.
    if (p.expected_version == fixpp::dict::session_version::v44 ||
        p.expected_version == fixpp::dict::session_version::v50sp2) {
        EXPECT_EQ(dict().length_pair_data_tag(std::uint16_t{618}), std::uint16_t{619})
            << "length_pair_data_tag(618) must return 619 (EncodedLegIssuer) in " << p.filename;
    }

    // R2 (F1.3): XMLDATA-typed pair: SecurityXMLLen(1184)→SecurityXML(1185) in
    // FIX50SP2 (adjacent in the global <fields> block; XMLDATA type).
    if (p.expected_version == fixpp::dict::session_version::v50sp2) {
        EXPECT_EQ(dict().length_pair_data_tag(std::uint16_t{1184}), std::uint16_t{1185})
            << "length_pair_data_tag(1184) must return 1185 (SecurityXML) in " << p.filename;
    }
}

// ---------------------------------------------------------------------------
// NoexceptDiscipline (AC-D8) — compile-time; one non-parameterized TEST
// ---------------------------------------------------------------------------

TEST(DictionaryNoexcept, AllPublicAccessorsAreNoexcept) {
    using D = fixpp::dict::Dictionary const&;

    static_assert(noexcept(std::declval<D>().which_session_version()));
    static_assert(noexcept(std::declval<D>().field_ref({}, 0u)));
    static_assert(noexcept(std::declval<D>().required_fields({})));
    static_assert(noexcept(std::declval<D>().field_valid_for({}, 0u)));
    static_assert(noexcept(std::declval<D>().group_first_field(0u)));
    static_assert(noexcept(std::declval<D>().length_pair_data_tag(0u)));
    static_assert(noexcept(std::declval<D>().field({}, 0u)));
    static_assert(noexcept(std::declval<D>().field_by_name({})));
    static_assert(noexcept(std::declval<D>().component({})));
    static_assert(noexcept(std::declval<D>().group(0u)));
    static_assert(noexcept(std::declval<D>().messages()));
    // R6: new component_fields / group_fields accessors must be noexcept.
    static_assert(noexcept(std::declval<D>().component_fields({})));
    static_assert(noexcept(std::declval<D>().group_fields(0u)));

    SUCCEED();  // All static_asserts above already enforce the property.
}

TEST(DictionaryAccessors, SmallLoadedDictionaryCoversMissAndEmptyPaths) {
    auto dict = load_small_dictionary();

    EXPECT_EQ(dict.group_first_field(9999u), std::uint16_t{0});

    auto const empty_component = dict.component("Empty");
    ASSERT_TRUE(empty_component.has_value());
    EXPECT_EQ(empty_component->field_count, std::uint16_t{0});
    EXPECT_TRUE(dict.component_fields("Empty").empty());
    EXPECT_TRUE(dict.component_fields("MissingComponent").empty());

    auto const empty_group = dict.group(453u);
    ASSERT_TRUE(empty_group.has_value());
    EXPECT_EQ(empty_group->field_count, std::uint16_t{0});
    EXPECT_TRUE(dict.group_fields(453u).empty());
    EXPECT_EQ(dict.group_first_field(454u), std::uint16_t{0});
    EXPECT_TRUE(dict.group_fields(454u).empty());
    EXPECT_TRUE(dict.group_fields(9999u).empty());

    auto const present_msg_fields = dict.message_fields("D");
    ASSERT_EQ(present_msg_fields.size(), std::size_t{4});
    EXPECT_EQ(present_msg_fields[0].tag, std::uint16_t{11});
    EXPECT_EQ(present_msg_fields[1].tag, std::uint16_t{55});
    EXPECT_EQ(present_msg_fields[2].tag, std::uint16_t{453});
    EXPECT_EQ(present_msg_fields[3].tag, std::uint16_t{555});
    EXPECT_TRUE(dict.message_fields("Z").empty());

    EXPECT_EQ(dict.field_name(11u), "ClOrdID");
    EXPECT_TRUE(dict.field_name(9999u).empty());

    EXPECT_TRUE(dict.field_valid_for("D", 11u));
    EXPECT_FALSE(dict.field_valid_for("D", 9999u));
    EXPECT_EQ(dict.length_pair_data_tag(35u), std::uint16_t{0});
}

TEST(DictionaryAccessors, MovedFromDictionaryUsesNullHandleFallbacks) {
    auto dict = load_small_dictionary();
    auto null_dict = std::move(dict);
    (void)null_dict;

    EXPECT_EQ(dict.which_session_version(), fixpp::dict::session_version::Unknown);
    EXPECT_EQ(dict.field_ref("D", 11u).rule, fixpp::dict::field_presence::NotDeclared);
    EXPECT_TRUE(dict.required_fields("D").empty());
    EXPECT_FALSE(dict.field_valid_for("D", 11u));
    EXPECT_EQ(dict.group_first_field(9999u), std::uint16_t{0});
    EXPECT_EQ(dict.length_pair_data_tag(95u), std::uint16_t{0});
    EXPECT_FALSE(dict.field("D", 11u).has_value());
    EXPECT_FALSE(dict.field_by_name("ClOrdID").has_value());
    EXPECT_FALSE(dict.component("Instrument").has_value());
    EXPECT_FALSE(dict.group(9999u).has_value());
    EXPECT_TRUE(dict.messages().empty());
    EXPECT_TRUE(dict.component_fields("Instrument").empty());
    EXPECT_TRUE(dict.group_fields(9999u).empty());
    EXPECT_TRUE(dict.message_fields("D").empty());
    EXPECT_TRUE(dict.field_name(11u).empty());
}

// ---------------------------------------------------------------------------
// INSTANTIATE_TEST_SUITE_P
// ---------------------------------------------------------------------------

INSTANTIATE_TEST_SUITE_P(
    AllRuntimeVersions, DictionaryLookupFixture,
    ::testing::Values(
        // ---- FIX 4.2 ----
        VersionParam{
            .filename = "FIX42.xml",
            .expected_version = fixpp::dict::session_version::v42,
            .required_msg_types = {"D", "8", "A", "0", "3"},
            .forbidden_msg_types = {},        // no cross-version assertions for 4.2
            .required_group_no_tags = {78u},  // NoAllocs (78) present in 4.2
                                              // NoPartyIDs(453) added in 4.3 — omit
                                              // NoLegs(555) added in 4.4 — omit
            .has_clordid = true,
            .parties_expected = std::optional<bool>{false},  // pre-4.3
            .has_instrument =
                false,  // FIX 4.2 declares Instrument's fields inline,
                        // not as a <component> entry — see dictionaries/FIX42.xml:1602
        },
        // ---- FIX 4.4 ----
        VersionParam{
            .filename = "FIX44.xml",
            .expected_version = fixpp::dict::session_version::v44,
            .required_msg_types = {"D", "8", "A", "0", "3"},
            .forbidden_msg_types = {},
            .required_group_no_tags = {453u, 78u, 555u},  // NoPartyIDs, NoAllocs, NoLegs
            .has_clordid = true,
            .parties_expected = std::optional<bool>{true},
            .has_instrument = true,
        },
        // ---- FIX 5.0 SP2 ----
        VersionParam{
            .filename = "FIX50SP2.xml",
            .expected_version = fixpp::dict::session_version::v50sp2,
            .required_msg_types = {"D", "8", "V", "W"},
            .forbidden_msg_types = {},
            .required_group_no_tags = {453u, 78u, 555u},
            .has_clordid = true,
            .parties_expected = std::optional<bool>{true},
            .has_instrument = true,
        },
        // ---- FIXT 1.1 ----
        VersionParam{
            .filename = "FIXT11.xml",
            .expected_version = fixpp::dict::session_version::vt11,
            .required_msg_types = {"A", "5", "0", "1", "2", "3", "4"},
            .forbidden_msg_types = {"D"},      // no NewOrderSingle in session-only dict
            .required_group_no_tags = {627u},  // NoHops
            .has_clordid = false,              // no application fields in FIXT11
            // R5 fix: FIXT11 only declares HopGrp and MsgTypeGrp components;
            // no Parties (verified against dictionaries/FIXT11.xml:104–113).
            .parties_expected = std::optional<bool>{false},
            .has_instrument = false,  // session-only; no Instrument
        },
        // ---- Runtime-XML-only versions (D-005/006, spec 002 §10 F1) ----
        // Vendored data + headline tests only; no codegen namespace. Assertions
        // verified against the fetched upstream XML (pinned SHA in UPSTREAM.txt).
        // ---- FIX 4.0 / FIX 4.1 (D-004, 064-fix4041-legacy-types) ----
        // Pre-FIXT: the session messages (Logon 'A', Heartbeat '0', ...) live IN
        // this dictionary, so they are asserted PRESENT — the inverse of the FIX
        // 5.0/5.0SP1 app-only shape below (session moved to FIXT.1.1). Loadable
        // now that the loader accepts their legacy `TIME`/`DATE` field types
        // (064). FIX 4.0/4.1 predate <component>s and NUMINGROUP-typed group
        // counters (their NoXxx fields are type='INT'), so has_instrument and
        // parties_expected are false and there are no registered NoXxx delimiter
        // groups. Every field below verified against the vendored files.
        VersionParam{
            .filename = "FIX40.xml",
            .expected_version = fixpp::dict::session_version::v40,
            .required_msg_types = {"D", "8", "A", "0", "3"},  // app D/8 + PRESENT pre-FIXT session A/0
            .forbidden_msg_types = {},                        // session lives in-dict (pre-FIXT)
            .required_group_no_tags = {},                     // NoXxx are type='INT', not NUMINGROUP
            .has_clordid = true,
            .parties_expected = std::optional<bool>{false},  // Parties added in 4.3
            .has_instrument = false,                         // no <component> declarations
        },
        VersionParam{
            .filename = "FIX41.xml",
            .expected_version = fixpp::dict::session_version::v41,
            .required_msg_types = {"D", "8", "A", "0", "3"},
            .forbidden_msg_types = {},
            .required_group_no_tags = {},
            .has_clordid = true,
            .parties_expected = std::optional<bool>{false},
            .has_instrument = false,
        },
        // ---- FIX 4.3 (D-005) ----
        VersionParam{
            .filename = "FIX43.xml",
            .expected_version = fixpp::dict::session_version::v43,
            .required_msg_types = {"0", "A", "D", "8", "W"},  // Parties/Instrument/MDIR era
            .forbidden_msg_types = {},
            .required_group_no_tags = {453u},  // NoPartyIDs — introduced in 4.3
            .has_clordid = true,
            .parties_expected = std::optional<bool>{true},
            .has_instrument = true,
        },
        // ---- FIX 5.0 (D-006) ----
        // Application layer only — the session messages (Logon 'A', Heartbeat
        // '0', TestRequest '1') moved to FIXT.1.1, so they MUST be absent here.
        VersionParam{
            .filename = "FIX50.xml",
            .expected_version = fixpp::dict::session_version::v50,
            .required_msg_types = {"D", "8", "W"},
            .forbidden_msg_types = {"A", "0"},  // session split → FIXT.1.1
            .required_group_no_tags = {453u},
            .has_clordid = true,
            .parties_expected = std::optional<bool>{true},
            .has_instrument = true,
        },
        // ---- FIX 5.0 SP1 (D-006) ----
        VersionParam{
            .filename = "FIX50SP1.xml",
            .expected_version = fixpp::dict::session_version::v50sp1,
            .required_msg_types = {"D", "8", "W"},
            .forbidden_msg_types = {"A", "0"},
            .required_group_no_tags = {453u},
            .has_clordid = true,
            .parties_expected = std::optional<bool>{true},
            .has_instrument = true,
        }),
    [](::testing::TestParamInfo<VersionParam> const& info) {
        // Strip the ".xml" suffix to produce a clean test-suite suffix.
        auto name = info.param.filename;
        if (auto pos = name.rfind('.'); pos != std::string::npos) name.erase(pos);
        return name;
    });
