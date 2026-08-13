// tests/codegen/test_082_class_xml_consistency_test.cpp
// 082-structural-group-detection T029 [US1] — FR-021 / SC-004.
//
// THE class-side ⟷ raw-XML consistency gate: the by-construction reconciliation
// instrument behind the `v42` 0 → 18 `class G_` delta. FR-021 asks for an
// instrument, NOT a regeneration transcript, so that "reconciled by construction"
// has something behind it and the delta is durable.
//
// It composes two derivations that share no code and no predicate:
//   CLASS SIDE      — parsed purely from the TEXT of the generated Messages.hpp.
//   STRUCTURAL SIDE — `build_quickfix_oracle()` (FR-018's oracle), a from-scratch
//                     pugixml walk of the vendored dictionary XML.
//
// ── NON-CIRCULARITY: this is NOT the same class as its vlatest sibling ────────
// `vlatest_manifest_class_consistency_test.cpp` deliberately links NO pugixml and
// NO fixpp header — both of its sides are generated-artifact text. THIS test does
// link pugixml (via the oracle), so do not cite that file's rationale as covering
// this one. The non-circularity claim here is different and narrower: the class
// side is the OUTPUT of the emitter under change, and the structural side is a raw
// XML walk that never consults `FieldRef::type`, `VersionIR::group_tags`, or
// `group_first_field` — the three predicates 082 re-points. So the gate can
// witness the re-point rather than moving with it (D-6 / FR-018).
//
// ── CLASS-SIDE EXTRACTION RULE ───────────────────────────────────────────────
// Ported from the version-agnostic rule documented at
// `vlatest_manifest_class_consistency_test.cpp:33-63`, and re-verified empirically
// against the real generated `v42` header before this test was written:
//   - MESSAGE class:   `^class <Name> {$`      (0-indent) … `^};$`
//   - GROUP flyweight: `^    class G_<N> {$`   (4-indent) … `^    };$`,
//                      in `namespace fixpp::<ns>::groups`.
//   - a flyweight's own scalar members:  `wire::get(ctx_.span, <N>,`
//   - a group REFERENCE (message-level or nested): `group_view<…G_<N>>`
//
// ⚠️ MEASURED, and it bites: the `group_view` marker has TWO spellings in one
// file. Inside a flyweight it is UNQUALIFIED (`group_view<G_295>`); at message
// level it is FULLY QUALIFIED (`group_view<::fixpp::v42::groups::G_296>`) — there
// is no bare `group_view<G_296>` anywhere in `v42/Messages.hpp`. A scanner written
// for the unqualified form alone silently sees ZERO message-level group references
// and every leg still passes. Both spellings are handled below, and mutation M3
// exists precisely to keep that true.
//
// ── THE EMITTER'S FLYWEIGHT-MEMBER RULE: UNION, and it was MEASURED ──────────
// `G_<N>` is a single version-wide-shared flyweight, but the oracle's member sets
// are PER CONTEXT — and FIX42's tag 146 has 6 contexts in 4 distinct variants
// (T017: {News,Email} 19, MarketDataRequest 20, {SecurityDefinitionRequest,
// SecurityDefinition} 22, QuoteRequest 31). So "the flyweight's members" needed a
// rule. It was derived from OUTPUT, not by reading the emitter (which the
// non-circularity constraint forbids): `G_146` carries 53 members, and the UNION
// of the 6 per-context direct-member sets is exactly 53, equal both directions.
// First-seen (19) and largest (31) are both refuted. Hence leg C compares against
// the union — and because that is an EXACT set equality, no subset weakening is
// needed anywhere in this gate.
//
// ── THE THREE LEGS ───────────────────────────────────────────────────────────
//   A  flyweight tag set == `oracle.group_tags`, both directions
//   B  each message's top-level group refs == that msg_type's top-level groups
//   C  each flyweight's DIRECT member set (own scalars + nested group refs)
//      == the UNION over that tag's contexts of the oracle's direct members
//
// Every leg asserts its parsed POPULATION against a pinned count first. A text
// scanner that matches nothing is otherwise a false-green generator, and this
// branch has been bitten by exactly that shape more than once.
//
// ── MUTATION MATRIX — measured against THIS binary, not a prototype ──────────
// Applied to the generated `v42/Messages.hpp`, each guarded by an `applied == 1`
// assertion (a mutation that fails to apply must not read as a pass — M3 initially
// matched nothing because of the qualified-spelling trap above), and the file
// restored and re-hashed to `827a9bd0…` afterwards:
//
//   M1  delete `class G_384` entirely      → A's COUNT pin, C, nesting  RED
//   M2  drop `G_296`'s scalar member 367   → C only, and only on {296}  RED
//   M3  drop one message-level
//       `group_view<::fixpp::v42::groups::G_296>`  → B only             RED
//   M4  re-tag `class G_384` → `class G_9999`      → A's SET equality   RED
//
// **M4 exists because M1 is not sufficient.** Under M1 the `ASSERT_EQ` population
// pin fires first and aborts the test, so leg A's actual set-equality `EXPECT_EQ`
// never executes — the count was doing all the work and a flyweight emitted under
// the WRONG tag would keep the count at 18 and slip through. M4 holds the count at
// 18 and moves only the set; it reports `structural-only: 384  class-only: 9999`.
// Do not delete M4 as redundant with M1: they prove different assertions.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "required_scope_oracle.hpp"  // 082 T005/T008: the independent oracle

#ifndef FIXPP_CODEGEN_INC
#error "FIXPP_CODEGEN_INC must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake target_compile_definitions"
#endif

namespace {

using fixpp_test::required_scope_oracle::build_quickfix_oracle;
using fixpp_test::required_scope_oracle::DictOracle;

// One generated read-tier version. FR-021 makes `v42` the requirement and
// version-parameterisation the ideal; all four `<fix>`-schema versions were
// verified to share the extraction rule, so all four are gated.
//
// The counts are pins, not decoration: `expect_messages` / `expect_groups` are
// what stop a scanner that parsed nothing from reading as agreement. v42's
// 46 / 18 is FR-016's headline; 59 / 505 / 1 are T018's registered-after column
// for FIX44 / FIX50SP2 / FIXT11, so this gate independently corroborates it from
// the *generated class tier* — a third derivation.
struct VersionCase {
    char const* ns;
    char const* xml;
    std::size_t expect_messages;
    std::size_t expect_groups;
};

constexpr std::array<VersionCase, 4> kCases{{
    {"v42", "FIX42.xml", 46, 18},
    {"v44", "FIX44.xml", 93, 59},
    {"v50sp2", "FIX50SP2.xml", 156, 505},
    {"vt11", "FIXT11.xml", 8, 1},
}};

[[nodiscard]] std::string read_file(std::filesystem::path const& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open generated header: " + p.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] std::vector<std::string> split_lines(std::string const& s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        auto const nl = s.find('\n', start);
        if (nl == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return out;
}

// Parses `<digits>` at `pos`; returns false if there is no digit there. Bounded
// to uint16 range, which every FIX tag is.
[[nodiscard]] bool parse_tag_at(std::string const& s, std::size_t pos, std::uint16_t& out) {
    if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') {
        return false;
    }
    std::uint32_t v = 0;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
        v = v * 10U + static_cast<std::uint32_t>(s[pos] - '0');
        if (v > 0xFFFFU) {
            return false;
        }
        ++pos;
    }
    out = static_cast<std::uint16_t>(v);
    return true;
}

// Every `group_view<…G_<N>>` in `body`, accepting BOTH the unqualified
// (`group_view<G_295>`) and fully-qualified
// (`group_view<::fixpp::v42::groups::G_296>`) spellings — see the banner.
[[nodiscard]] std::set<std::uint16_t> group_refs_in(std::string const& body) {
    static constexpr std::string_view kOpen{"group_view<"};
    std::set<std::uint16_t> out;
    for (std::size_t i = body.find(kOpen); i != std::string::npos; i = body.find(kOpen, i + 1)) {
        auto const close = body.find('>', i);
        if (close == std::string::npos) {
            continue;
        }
        // Take the LAST "G_" inside the angle brackets, so a namespace
        // qualification cannot shadow the flyweight name.
        auto const inner = body.substr(i + kOpen.size(), close - (i + kOpen.size()));
        auto const g = inner.rfind("G_");
        if (g == std::string::npos) {
            continue;
        }
        std::uint16_t tag = 0;
        if (parse_tag_at(inner, g + 2, tag)) {
            out.insert(tag);
        }
    }
    return out;
}

// Every `wire::get(ctx_.span, <N>,` in `body` — a flyweight's own scalar members.
[[nodiscard]] std::set<std::uint16_t> scalar_members_in(std::string const& body) {
    static constexpr std::string_view kPat{"wire::get(ctx_.span, "};
    std::set<std::uint16_t> out;
    for (std::size_t i = body.find(kPat); i != std::string::npos; i = body.find(kPat, i + 1)) {
        std::uint16_t tag = 0;
        if (parse_tag_at(body, i + kPat.size(), tag)) {
            out.insert(tag);
        }
    }
    return out;
}

struct ClassSide {
    // flyweight no_tag -> its body text
    std::map<std::uint16_t, std::string> flyweights;
    // message class name -> its body text
    std::map<std::string, std::string> messages;
    // message class name -> its `msg_type_v` literal
    std::map<std::string, std::string> msg_types;
};

// Indentation IS the discriminator between the two class populations, per the
// ported rule. Deliberately no regex: `v50sp2/Messages.hpp` is multi-megabyte.
[[nodiscard]] ClassSide parse_class_side(std::filesystem::path const& header) {
    ClassSide cs;
    auto const lines = split_lines(read_file(header));

    enum class In { None, Message, Group };
    In in = In::None;
    std::string cur_name;
    std::uint16_t cur_tag = 0;
    std::string body;

    auto flush = [&] {
        if (in == In::Message) {
            cs.messages.emplace(cur_name, body);
            static constexpr std::string_view kMt{"msg_type_v = \""};
            if (auto const i = body.find(kMt); i != std::string::npos) {
                auto const start = i + kMt.size();
                if (auto const end = body.find('"', start); end != std::string::npos) {
                    cs.msg_types.emplace(cur_name, body.substr(start, end - start));
                }
            }
        } else if (in == In::Group) {
            cs.flyweights.emplace(cur_tag, body);
        }
        in = In::None;
        body.clear();
    };

    for (auto const& l : lines) {
        if (in == In::Group && l == "    };") {
            flush();
            continue;
        }
        if (in == In::Message && l == "};") {
            flush();
            continue;
        }
        if (in == In::None) {
            // GROUP flyweight: `    class G_<N> {`
            static constexpr std::string_view kG{"    class G_"};
            if (l.starts_with(kG) && l.ends_with(" {")) {
                std::uint16_t tag = 0;
                if (parse_tag_at(l, kG.size(), tag)) {
                    in = In::Group;
                    cur_tag = tag;
                    continue;
                }
            }
            // MESSAGE class: `class <Name> {`
            static constexpr std::string_view kM{"class "};
            if (l.starts_with(kM) && l.ends_with(" {")) {
                in = In::Message;
                cur_name = l.substr(kM.size(), l.size() - kM.size() - 2);
                continue;
            }
            continue;
        }
        body += l;
        body += '\n';
    }
    return cs;
}

// ── structural side, derived ONLY from the oracle's maps ─────────────────────

// The union, over every context of `no_tag`, of that context's DIRECT members.
// This is what a version-wide-shared flyweight must carry (see banner).
[[nodiscard]] std::set<std::uint16_t> union_direct_members(DictOracle const& o,
                                                            std::uint16_t no_tag) {
    std::set<std::uint16_t> out;
    for (auto const& [key, members] : o.group_members) {
        if (key.no_tag == no_tag) {
            out.insert(members.begin(), members.end());
        }
    }
    return out;
}

// Groups appearing at a message's TOP level == the contexts of that msg_type
// whose ancestor path is empty (`GroupContextKey.path` excludes no_tag itself).
[[nodiscard]] std::set<std::uint16_t> top_level_groups(DictOracle const& o,
                                                        std::string const& msg_type) {
    std::set<std::uint16_t> out;
    for (auto const& [key, members] : o.group_members) {
        if (key.msg_type == msg_type && key.path.empty()) {
            out.insert(key.no_tag);
        }
    }
    return out;
}

[[nodiscard]] std::string describe(std::set<std::uint16_t> const& expected,
                                   std::set<std::uint16_t> const& actual) {
    std::ostringstream ss;
    auto emit = [&ss](char const* label, std::set<std::uint16_t> const& a,
                      std::set<std::uint16_t> const& b) {
        ss << label;
        bool any = false;
        for (auto const t : a) {
            if (!b.contains(t)) {
                ss << ' ' << t;
                any = true;
            }
        }
        if (!any) {
            ss << " <none>";
        }
    };
    emit("structural-only:", expected, actual);
    emit("  class-only:", actual, expected);
    return ss.str();
}

[[nodiscard]] std::filesystem::path header_of(VersionCase const& c) {
    return std::filesystem::path{FIXPP_CODEGEN_INC} / "fixpp" / c.ns / "Messages.hpp";
}

[[nodiscard]] std::filesystem::path xml_of(VersionCase const& c) {
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / c.xml;
}

}  // namespace

// ── LEG A — the 0 → 18 delta itself ──────────────────────────────────────────
TEST(Class082XmlConsistency, FlyweightSetEqualsStructuralGroupTags) {
    for (auto const& c : kCases) {
        SCOPED_TRACE(c.ns);
        auto const cs = parse_class_side(header_of(c));
        auto const oracle = build_quickfix_oracle(xml_of(c));

        // Population pins FIRST — a scanner that parsed nothing must FAIL here
        // rather than agree with an empty structural set.
        ASSERT_EQ(cs.messages.size(), c.expect_messages)
            << c.ns << ": parsed message-class count drifted from the pin -- re-derive, and "
                       "suspect the extraction rule before the emitter";
        ASSERT_EQ(cs.flyweights.size(), c.expect_groups)
            << c.ns << ": parsed `class G_` flyweight count drifted from the pin";

        std::set<std::uint16_t> class_tags;
        for (auto const& [tag, body] : cs.flyweights) {
            class_tags.insert(tag);
        }
        EXPECT_EQ(oracle.group_tags, class_tags)
            << c.ns << ": the emitted `class G_` set must equal the raw-XML reachable group-tag "
                       "set EXACTLY, both directions -- " << describe(oracle.group_tags, class_tags);
    }
}

// ── LEG B — each group is emitted into the right messages ────────────────────
TEST(Class082XmlConsistency, PerMessageTopLevelGroupRefsMatchStructure) {
    for (auto const& c : kCases) {
        SCOPED_TRACE(c.ns);
        auto const cs = parse_class_side(header_of(c));
        auto const oracle = build_quickfix_oracle(xml_of(c));

        ASSERT_EQ(cs.msg_types.size(), c.expect_messages)
            << c.ns << ": every message class must expose a `msg_type_v` -- a missing one would "
                       "silently drop that message from this leg";

        std::size_t checked = 0;
        for (auto const& [name, msg_type] : cs.msg_types) {
            auto const body = cs.messages.find(name);
            ASSERT_NE(body, cs.messages.end());
            auto const class_groups = group_refs_in(body->second);
            auto const structural = top_level_groups(oracle, msg_type);
            EXPECT_EQ(structural, class_groups)
                << c.ns << " message " << name << " (msg_type=" << msg_type
                << "): top-level group references must match the dictionary EXACTLY -- "
                << describe(structural, class_groups);
            ++checked;
        }
        ASSERT_EQ(checked, c.expect_messages) << c.ns << ": not every message was checked";
    }
}

// ── LEG C — each flyweight's shape, including the nesting ────────────────────
TEST(Class082XmlConsistency, FlyweightDirectMembersEqualUnionOverContexts) {
    for (auto const& c : kCases) {
        SCOPED_TRACE(c.ns);
        auto const cs = parse_class_side(header_of(c));
        auto const oracle = build_quickfix_oracle(xml_of(c));

        ASSERT_EQ(cs.flyweights.size(), c.expect_groups) << c.ns << ": flyweight count drifted";

        std::size_t checked = 0;
        for (auto const& [tag, body] : cs.flyweights) {
            // A flyweight's DIRECT members are its own scalars plus the count
            // tags of the groups nested directly inside it. The oracle records a
            // nested group's count tag as a member of its parent context, so the
            // two sides are the same shape.
            auto direct = scalar_members_in(body);
            auto const nested = group_refs_in(body);
            direct.insert(nested.begin(), nested.end());

            auto const structural = union_direct_members(oracle, tag);
            EXPECT_EQ(structural, direct)
                << c.ns << " G_" << tag
                << ": flyweight direct members must equal the UNION of this tag's per-context "
                   "declared direct members, EXACTLY -- " << describe(structural, direct);
            ++checked;
        }
        ASSERT_EQ(checked, c.expect_groups) << c.ns << ": not every flyweight was checked";
    }
}

// ── The 296 → 295 nesting, named explicitly ─────────────────────────────────
// SC-004 calls out this nesting by name, and T020 pins it for the one case at the
// emitter level. Leg C already covers it version-wide as one row among 18, which
// is exactly the failure mode a named pin guards against: a leg that would still
// be "mostly green" if this single edge flattened.
TEST(Class082XmlConsistency, V42MassQuoteNestingIsExpressedAsANestedTypedGroup) {
    auto const cs = parse_class_side(header_of(kCases[0]));  // v42
    ASSERT_EQ(cs.flyweights.size(), 18U);

    auto const g296 = cs.flyweights.find(296);
    ASSERT_NE(g296, cs.flyweights.end()) << "v42 must emit `class G_296` (NoQuoteSets)";
    EXPECT_TRUE(group_refs_in(g296->second).contains(295))
        << "v42 G_296 (NoQuoteSets) must reference G_295 (NoQuoteEntries) as a NESTED typed "
           "group, not flatten it (SC-004 / US1 AC4)";

    // And the converse direction: 295 must NOT appear at MassQuote's top level,
    // which is what a flattening would look like from the message side.
    auto const oracle = build_quickfix_oracle(xml_of(kCases[0]));
    EXPECT_FALSE(top_level_groups(oracle, "i").contains(295))
        << "the dictionary itself must place 295 inside 296, not at MassQuote's top level -- if "
           "this fires, the fixture premise moved, not the emitter";
}
