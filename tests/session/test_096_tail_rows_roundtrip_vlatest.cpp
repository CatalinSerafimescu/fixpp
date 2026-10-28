// SPDX-License-Identifier: AGPL-3.0-or-later
// test_096_tail_rows_roundtrip_vlatest.cpp
//
// 096-fix-latest-tail-evidence, vlatest leg: one offline witness per FIX Latest tail MsgType (suite
// TailRows096Latest), the census holding that suite to the tail table below, the Orchestra
// required-walk with its known positives (suite TailRows096LatestCensus). A source of the
// test_077_allversions_builder_roundtrip_vlatest executable, beside 077's vlatest TU; the body
// enumeration, the presence check and check_witness are 095's, shared through
// support/tail_rows_witness.hpp (contracts W0). The walk below reads the Orchestra file with
// pugixml directly; it shares no code with src/dictionary/orchestra_loader.cpp or tools/codegen/.
//
// THE RULE (096 data-model §B.2), applied to dictionaries/orchestra/OrchestraFIXLatest.xml at test
// runtime:
//   - each fixr:message is keyed by its msgType; a msgType seen twice fails the walk loudly;
//   - the walk starts at the message's fixr:structure; a reference outside it is not walked;
//   - a fixr:fieldRef's id is the tag; it is required when every element on its path from the
//     structure (each enclosing fixr:componentRef, fixr:groupRef and the fieldRef itself) carries
//     presence="required";
//   - a fixr:componentRef resolves in fixr:components (StandardHeader and StandardTrailer are
//     skipped); a fixr:groupRef resolves in fixr:groups, its count tag is the group's
//     fixr:numInGroup id, and it is required under the same path rule;
//   - a group instance's required members follow the same rules, starting from the group;
//   - a group's delimiter is the first tag it emits in declaration order, its numInGroup
//     excluded, components expanded and a nested groupRef emitting its count tag. When it is a
//     nested group's count tag, that nested group carries exactly one instance.
// The walk fails loudly, naming the group by its count field's name (the fixr:field@name whose id
// is the group's numInGroup id), on a required group inside a required group's instance and on a
// delimiter chain longer than one nested group.
//
// No count, member list or per-MsgType outcome of the walk is recorded here (contracts W7): the
// walk re-derives them on every run. To re-derive one by hand:
//   O=dictionaries/orchestra/OrchestraFIXLatest.xml
//   awk '/<fixr:message [^>]*msgType="<MT>"/,/<\/fixr:message>/' "$O"
//   awk '/<fixr:group id="<ID>"/,/<\/fixr:group>/' "$O"
//   awk '/<fixr:component id="<ID>"/,/<\/fixr:component>/' "$O"
// and apply the rule above to each element on the path. The field leg is compared with
//   grep '^FIX50SP2,<MT>,' tools/quickfix_required_golden/golden.csv
// (QuickFIX-cpp's message-level set over FIX50SP2.xml, a file this walk does not read).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/vlatest/groups/G_1676_1Args.hpp>
#include <fixpp/vlatest/groups/G_1772_2Args.hpp>
#include <fixpp/vlatest/groups/G_2428_1Args.hpp>
#include <fixpp/vlatest/groups/G_2474_1Args.hpp>
#include <fixpp/vlatest/groups/G_2474_2Args.hpp>
#include <fixpp/vlatest/groups/G_453Args.hpp>
#include <fixpp/vlatest/messages/CrossRequest.hpp>
#include <fixpp/vlatest/messages/CrossRequestAck.hpp>
#include <fixpp/vlatest/messages/MarketDataReport.hpp>
#include <fixpp/vlatest/messages/MarketDataStatisticsReport.hpp>
#include <fixpp/vlatest/messages/MarketDataStatisticsRequest.hpp>
#include <fixpp/vlatest/messages/MassOrder.hpp>
#include <fixpp/vlatest/messages/MassOrderAck.hpp>
#include <fixpp/vlatest/messages/PartyActionReport.hpp>
#include <fixpp/vlatest/messages/PartyActionRequest.hpp>
#include <fixpp/vlatest/messages/PartyDetailsDefinitionRequest.hpp>
#include <fixpp/vlatest/messages/PartyDetailsDefinitionRequestAck.hpp>
#include <fixpp/vlatest/messages/PartyDetailsListReport.hpp>
#include <fixpp/vlatest/messages/PartyDetailsListRequest.hpp>
#include <fixpp/vlatest/messages/PartyDetailsListUpdateReport.hpp>
#include <fixpp/vlatest/messages/PartyEntitlementsDefinitionRequest.hpp>
#include <fixpp/vlatest/messages/PartyEntitlementsDefinitionRequestAck.hpp>
#include <fixpp/vlatest/messages/PartyEntitlementsReport.hpp>
#include <fixpp/vlatest/messages/PartyEntitlementsRequest.hpp>
#include <fixpp/vlatest/messages/PartyEntitlementsUpdateReport.hpp>
#include <fixpp/vlatest/messages/PartyRiskLimitCheckRequest.hpp>
#include <fixpp/vlatest/messages/PartyRiskLimitCheckRequestAck.hpp>
#include <fixpp/vlatest/messages/PartyRiskLimitsDefinitionRequest.hpp>
#include <fixpp/vlatest/messages/PartyRiskLimitsDefinitionRequestAck.hpp>
#include <fixpp/vlatest/messages/PartyRiskLimitsReport.hpp>
#include <fixpp/vlatest/messages/PartyRiskLimitsReportAck.hpp>
#include <fixpp/vlatest/messages/PartyRiskLimitsRequest.hpp>
#include <fixpp/vlatest/messages/PartyRiskLimitsUpdateReport.hpp>
#include <fixpp/vlatest/messages/QuoteAck.hpp>
#include <fixpp/vlatest/messages/SecurityMassStatus.hpp>
#include <fixpp/vlatest/messages/SecurityMassStatusRequest.hpp>
#include <fixpp/vlatest/messages/TradeMatchReport.hpp>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ostream>
#include <pugixml.hpp>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "support/app_message_read_scaffold.hpp"
#include "support/tail_rows_witness.hpp"

namespace {

// The walk's result types, the body enumeration and check_witness live in
// support/tail_rows_witness.hpp.
using fixpp_test_support::bytes_to_string;
using fixpp_test_support::make_frame;
using namespace fixpp_test_support::tail_rows;

// ── The tail table (data-model §A; the catalogue row is the authority) ───────

struct TailRow {
    char const* row;
    char const* msgtype;
    char const* name;
};

std::vector<TailRow> const& tail_rows() {
    static std::vector<TailRow> const rows = {
        {.row = "A-035", .msgtype = "DS", .name = "CrossRequest"},
        {.row = "A-036", .msgtype = "DT", .name = "CrossRequestAck"},
        {.row = "A-037", .msgtype = "DJ", .name = "MassOrder"},
        {.row = "A-038", .msgtype = "DK", .name = "MassOrderAck"},
        {.row = "A-039", .msgtype = "DC", .name = "TradeMatchReport"},
        {.row = "A-040", .msgtype = "DR", .name = "MarketDataReport"},
        {.row = "A-041", .msgtype = "DO", .name = "MarketDataStatisticsRequest"},
        {.row = "A-042", .msgtype = "DP", .name = "MarketDataStatisticsReport"},
        {.row = "A-043", .msgtype = "CF", .name = "PartyDetailsListRequest"},
        {.row = "A-044", .msgtype = "CG", .name = "PartyDetailsListReport"},
        {.row = "A-045", .msgtype = "CK", .name = "PartyDetailsListUpdateReport"},
        {.row = "A-046", .msgtype = "CX", .name = "PartyDetailsDefinitionRequest"},
        {.row = "A-047", .msgtype = "CY", .name = "PartyDetailsDefinitionRequestAck"},
        {.row = "A-048", .msgtype = "CU", .name = "PartyEntitlementsRequest"},
        {.row = "A-049", .msgtype = "CV", .name = "PartyEntitlementsReport"},
        {.row = "A-050", .msgtype = "CZ", .name = "PartyEntitlementsUpdateReport"},
        {.row = "A-051", .msgtype = "DA", .name = "PartyEntitlementsDefinitionRequest"},
        {.row = "A-052", .msgtype = "DB", .name = "PartyEntitlementsDefinitionRequestAck"},
        {.row = "A-053", .msgtype = "CL", .name = "PartyRiskLimitsRequest"},
        {.row = "A-054", .msgtype = "CM", .name = "PartyRiskLimitsReport"},
        {.row = "A-055", .msgtype = "CR", .name = "PartyRiskLimitsUpdateReport"},
        {.row = "A-056", .msgtype = "CS", .name = "PartyRiskLimitsDefinitionRequest"},
        {.row = "A-057", .msgtype = "CT", .name = "PartyRiskLimitsDefinitionRequestAck"},
        {.row = "A-058", .msgtype = "DE", .name = "PartyRiskLimitsReportAck"},
        {.row = "A-059", .msgtype = "DH", .name = "PartyActionRequest"},
        {.row = "A-060", .msgtype = "DI", .name = "PartyActionReport"},
        {.row = "A-061", .msgtype = "DF", .name = "PartyRiskLimitCheckRequest"},
        {.row = "A-062", .msgtype = "DG", .name = "PartyRiskLimitCheckRequestAck"},
        {.row = "A-063", .msgtype = "CN", .name = "SecurityMassStatusRequest"},
        {.row = "A-064", .msgtype = "CO", .name = "SecurityMassStatus"},
        {.row = "A-065", .msgtype = "CW", .name = "QuoteAck"},
    };
    return rows;
}

// ── The Orchestra required-walk (data-model §B.2) ────────────────────────────

struct OrchestraWalk {
    WalkMap walk;
    std::string error;  // empty unless the walk failed loudly on its input
};

class OrchestraWalker {
public:
    explicit OrchestraWalker(pugi::xml_document const& doc) {
        pugi::xml_node const repo = doc.child("fixr:repository");
        for (pugi::xml_node f : repo.child("fixr:fields").children("fixr:field")) {
            field_names_[tag_of(f)] = f.attribute("name").as_string();
        }
        for (pugi::xml_node c : repo.child("fixr:components").children("fixr:component")) {
            components_[c.attribute("id").as_string()] = c;
        }
        for (pugi::xml_node g : repo.child("fixr:groups").children("fixr:group")) {
            groups_[g.attribute("id").as_string()] = g;
        }
        messages_ = repo.child("fixr:messages");
    }

    [[nodiscard]] OrchestraWalk walk_all() const {
        OrchestraWalk out;
        for (pugi::xml_node m : messages_.children("fixr:message")) {
            std::string const mt = m.attribute("msgType").as_string();
            // The key rule: a msgType seen twice fails the walk (no last-writer-wins).
            if (!out.walk.try_emplace(mt, walk_message(m)).second && out.error.empty()) {
                out.error = "duplicate fixr:message msgType " + mt;
            }
        }
        return out;
    }

private:
    struct Failure {
        walk_status status = walk_status::ok;
        std::string element;
        void set(walk_status s, std::string e) {
            if (status == walk_status::ok) {
                status = s;
                element = std::move(e);
            }
        }
    };

    static std::uint16_t tag_of(pugi::xml_node n) {
        return static_cast<std::uint16_t>(n.attribute("id").as_uint());
    }

    static bool is_required(pugi::xml_node n) {
        return std::string_view{n.attribute("presence").as_string()} == "required";
    }

    static bool is_group_ref(pugi::xml_node n) {
        return std::string_view{n.name()} == "fixr:groupRef";
    }

    static bool is_header_or_trailer(pugi::xml_node component) {
        std::string_view const name = component.attribute("name").as_string();
        return name == "StandardHeader" || name == "StandardTrailer";
    }

    [[nodiscard]] pugi::xml_node component_def(pugi::xml_node ref) const {
        auto const it = components_.find(ref.attribute("id").as_string());
        return it == components_.end() ? pugi::xml_node{} : it->second;
    }

    [[nodiscard]] pugi::xml_node group_def(pugi::xml_node ref) const {
        auto const it = groups_.find(ref.attribute("id").as_string());
        return it == groups_.end() ? pugi::xml_node{} : it->second;
    }

    static std::uint16_t count_tag_of(pugi::xml_node group) {
        return tag_of(group.child("fixr:numInGroup"));
    }

    // A group is named by its count field's name.
    [[nodiscard]] std::string count_field_name(pugi::xml_node group) const {
        auto const it = field_names_.find(count_tag_of(group));
        return it == field_names_.end() ? std::string{} : it->second;
    }

    // The first emission under `container` in declaration order: a fieldRef, or a groupRef (which
    // emits its count tag); components expanded, numInGroup excluded.
    [[nodiscard]] pugi::xml_node first_emission(pugi::xml_node container) const {
        for (pugi::xml_node c : container.children()) {
            std::string_view const kind = c.name();
            if (kind == "fixr:fieldRef" || kind == "fixr:groupRef") return c;
            if (kind == "fixr:componentRef") {
                if (pugi::xml_node const inner = first_emission(component_def(c))) return inner;
            }
        }
        return {};
    }

    // The tag an emission writes first: a field's tag, or a nested group's count tag.
    [[nodiscard]] std::uint16_t emitted_tag(pugi::xml_node emission) const {
        return is_group_ref(emission) ? count_tag_of(group_def(emission)) : tag_of(emission);
    }

    // Collects the required fields and groups under `container`, reached along a path whose
    // elements so far are all required iff `path_required`.
    void collect(pugi::xml_node container, bool path_required, std::vector<std::uint16_t>& fields,
                 std::vector<pugi::xml_node>& groups) const {
        for (pugi::xml_node c : container.children()) {
            bool const req = path_required && is_required(c);
            std::string_view const kind = c.name();
            if (kind == "fixr:fieldRef") {
                if (req && std::ranges::find(fields, tag_of(c)) == fields.end()) {
                    fields.push_back(tag_of(c));
                }
            } else if (kind == "fixr:groupRef") {
                if (req) groups.push_back(group_def(c));
            } else if (kind == "fixr:componentRef") {
                pugi::xml_node const def = component_def(c);
                if (is_header_or_trailer(def)) continue;
                collect(def, req, fields, groups);
            }
        }
    }

    [[nodiscard]] WalkResult walk_message(pugi::xml_node m) const {
        WalkResult r;
        Failure fail;
        std::vector<pugi::xml_node> groups;
        collect(m.child("fixr:structure"), true, r.fields, groups);
        for (pugi::xml_node g : groups) {
            RequiredGroup rg;
            rg.count_tag = count_tag_of(g);
            pugi::xml_node const first = first_emission(g);
            rg.delim_tag = emitted_tag(first);
            if (is_group_ref(first)) {
                pugi::xml_node const nested = group_def(first);
                pugi::xml_node const nested_first = first_emission(nested);
                if (is_group_ref(nested_first)) {
                    fail.set(walk_status::delimiter_chain,
                             count_field_name(group_def(nested_first)));
                }
                rg.delim_group = std::pair{rg.delim_tag, emitted_tag(nested_first)};
            }
            std::vector<pugi::xml_node> inner_groups;
            collect(g, true, rg.fields, inner_groups);
            if (!inner_groups.empty()) {
                fail.set(walk_status::nested_required_group,
                         count_field_name(inner_groups.front()));
            }
            r.groups.push_back(std::move(rg));
        }
        r.status = fail.status;
        r.element = fail.element;
        return r;
    }

    std::map<std::uint16_t, std::string> field_names_;
    std::map<std::string, pugi::xml_node, std::less<>> components_;
    std::map<std::string, pugi::xml_node, std::less<>> groups_;
    pugi::xml_node messages_;
};

// The msgType of `<fixr:message name='<generated_name>' msgType=…>`, or empty when no message has
// that name. The census and every witness's tag-35 check share this one lookup.
std::string msgtype_for_orchestra_name(pugi::xml_document const& doc,
                                       std::string_view generated_name) {
    for (pugi::xml_node m :
         doc.child("fixr:repository").child("fixr:messages").children("fixr:message")) {
        if (generated_name == m.attribute("name").as_string()) {
            return m.attribute("msgType").as_string();
        }
    }
    return {};
}

// ── Fixture: the Orchestra file and its walk, loaded once ────────────────────

class Tail096LatestFixture : public ::testing::Test {
protected:
    struct Shared {
        pugi::xml_document doc;
        std::string load_error;
        std::pmr::monotonic_buffer_resource dict_arena{65536};
        std::optional<fixpp::dict::Dictionary> dict;
        std::optional<fixpp::dict::table_view> tv;
        WalkMap walk;
    };

    static void SetUpTestSuite() {
        if (shared_) return;
        shared_ = std::make_unique<Shared>();
        std::string const path = std::string(FIXPP_ORCHESTRA_DATA_DIR) + "/OrchestraFIXLatest.xml";
        if (pugi::xml_parse_result const r = shared_->doc.load_file(path.c_str()); !r) {
            shared_->load_error = "pugixml could not load " + path + ": " + r.description();
            return;
        }
        OrchestraWalk w = OrchestraWalker{shared_->doc}.walk_all();
        shared_->walk = std::move(w.walk);
        if (!w.error.empty()) {
            shared_->load_error = "Orchestra walk of " + path + ": " + w.error;
            return;
        }
        // The parse dictionary: the production Orchestra reader (contracts W3), not the walk.
        fixpp::dict::OrchestraLoader loader;
        shared_->dict.emplace(loader.load(path, &shared_->dict_arena));
        shared_->tv.emplace(shared_->dict->as_table_view());
    }

    void SetUp() override { ASSERT_TRUE(shared_->load_error.empty()) << shared_->load_error; }

    static pugi::xml_document const& doc() { return shared_->doc; }
    static WalkMap const& walk() { return shared_->walk; }
    // tv is emplaced on every path that leaves load_error empty, and SetUp() fails the test
    // before a test body runs otherwise.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    static fixpp::dict::table_view const& tv() { return *shared_->tv; }

    // W3–W5 for one witness: frame, independent parse, tag 35, both-direction readback,
    // presence check. The expected MsgType is the one the test's own name maps to.
    void check_witness(std::string const& body, Intent const& intent) {
        std::string const test_name =
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::string const expected_mt =
            msgtype_for_orchestra_name(doc(), split_test_name(test_name).second);
        fixpp_test_support::tail_rows::check_witness(walk(), tv(), &read_arena_, expected_mt, body,
                                                     intent);
    }

    std::array<std::byte, 131072> out_{};
    std::pmr::monotonic_buffer_resource read_arena_{65536};

    static std::unique_ptr<Shared> shared_;
};

std::unique_ptr<Tail096LatestFixture::Shared> Tail096LatestFixture::shared_;

class TailRows096Latest : public Tail096LatestFixture {};
class TailRows096LatestCensus : public Tail096LatestFixture {};

// Walks one inline Orchestra document through the walk's entry point.
OrchestraWalk walk_fragment(char const* xml) {
    pugi::xml_document d;
    if (!d.load_string(xml)) {
        ADD_FAILURE() << "pugixml could not load the fragment";
        return {};
    }
    return OrchestraWalker{d}.walk_all();
}

// ── The cross-dictionary differential (data-model §D) ────────────────────────
//
// THE RULE: for each given MsgType, the QuickFIX walk of FIX50SP2.xml and the Orchestra walk are
// compared by tag, and a finding carries the value each dictionary holds at a path where they
// differ:
//   - message-level required fields, path <tag>, value required | absent;
//   - required groups by count tag, path <count_tag>, value required | absent;
//   - for a group required on both sides only: its delimiter, path <count_tag>/delim, the
//     delimiter tag; its instance members, path <count_tag>[*].<tag>, required | absent; its
//     nested delimiter group, path <count_tag>/delim_group, "<count>,<delim>" | none;
//   - a walk status other than ok on either side gives that MsgType exactly one finding, path
//     status, value "<status> <element>" | ok, and no other finding. (claim-ok: the rule)
// Every finding of every given MsgType is collected, then sorted by MsgType, then path, each as a
// std::string. A MsgType absent from either walk fails the calling test.
// To re-derive one MsgType's values by hand, apply this TU's rule above to the Orchestra file and
// the rule in the header comment of test_095_tail_rows_roundtrip.cpp to FIX50SP2.xml:
//   sed -n "/<message name='<Name>'/,/<\/message>/p" dictionaries/FIX50SP2.xml
//   awk '/<fixr:message [^>]*msgType="<MT>"/,/<\/fixr:message>/' "$O"
// with O as in this file's header comment.

struct Finding {
    std::string msg_type;
    std::string path;
    std::string fix50sp2;
    std::string orchestra;
    bool operator==(Finding const&) const = default;
};

void PrintTo(Finding const& f, std::ostream* os) {
    *os << '{' << f.msg_type << ", " << f.path << ", " << f.fix50sp2 << ", " << f.orchestra << '}';
}

std::string status_value(WalkResult const& r) {
    if (r.status == walk_status::ok) return "ok";
    return std::string{to_string(r.status)} + " " + r.element;
}

std::string presence_value(bool required) { return required ? "required" : "absent"; }

std::string delim_group_value(RequiredGroup const& g) {
    if (!g.delim_group) return "none";
    return std::to_string(g.delim_group->first) + "," + std::to_string(g.delim_group->second);
}

std::vector<std::uint16_t> count_tags(WalkResult const& r) {
    std::vector<std::uint16_t> out;
    out.reserve(r.groups.size());
    for (RequiredGroup const& g : r.groups) out.push_back(g.count_tag);
    return out;
}

RequiredGroup const* find_group(WalkResult const& r, std::uint16_t count_tag) {
    auto const it = std::ranges::find_if(
        r.groups, [&](RequiredGroup const& g) { return g.count_tag == count_tag; });
    return it == r.groups.end() ? nullptr : &*it;
}

// One finding per tag required on exactly one side, at path `prefix` + the tag.
void compare_tags(std::string const& mt, std::string const& prefix,
                  std::vector<std::uint16_t> const& qf, std::vector<std::uint16_t> const& orch,
                  std::vector<Finding>& out) {
    std::set<std::uint16_t> tags(qf.begin(), qf.end());
    tags.insert(orch.begin(), orch.end());
    for (std::uint16_t const t : tags) {
        bool const in_qf = std::ranges::find(qf, t) != qf.end();
        bool const in_orch = std::ranges::find(orch, t) != orch.end();
        if (in_qf != in_orch) {
            out.push_back({.msg_type = mt,
                           .path = prefix + std::to_string(t),
                           .fix50sp2 = presence_value(in_qf),
                           .orchestra = presence_value(in_orch)});
        }
    }
}

void compare_msgtype(std::string const& mt, WalkResult const& qf, WalkResult const& orch,
                     std::vector<Finding>& out) {
    // A non-ok status on either side: one status finding, no other. (claim-ok: the rule)
    if (qf.status != walk_status::ok || orch.status != walk_status::ok) {
        out.push_back({.msg_type = mt,
                       .path = "status",
                       .fix50sp2 = status_value(qf),
                       .orchestra = status_value(orch)});
        return;
    }
    // Message-level required fields.
    compare_tags(mt, "", qf.fields, orch.fields, out);
    // Required groups, by count tag.
    compare_tags(mt, "", count_tags(qf), count_tags(orch), out);
    // Each group required on both sides: delimiter, instance members, delim_group.
    for (RequiredGroup const& g : qf.groups) {
        RequiredGroup const* const o = find_group(orch, g.count_tag);
        if (o == nullptr) continue;
        std::string const ct = std::to_string(g.count_tag);
        if (g.delim_tag != o->delim_tag) {
            out.push_back({.msg_type = mt,
                           .path = ct + "/delim",
                           .fix50sp2 = std::to_string(g.delim_tag),
                           .orchestra = std::to_string(o->delim_tag)});
        }
        compare_tags(mt, ct + "[*].", g.fields, o->fields, out);
        if (delim_group_value(g) != delim_group_value(*o)) {
            out.push_back({.msg_type = mt,
                           .path = ct + "/delim_group",
                           .fix50sp2 = delim_group_value(g),
                           .orchestra = delim_group_value(*o)});
        }
    }
}

std::vector<Finding> diff(WalkMap const& qf_walk, WalkMap const& orch_walk,
                          std::vector<std::string> const& msg_types) {
    std::vector<Finding> out;
    for (std::string const& mt : msg_types) {
        auto const qf = qf_walk.find(mt);
        auto const orch = orch_walk.find(mt);
        if (qf == qf_walk.end() || orch == orch_walk.end()) {
            ADD_FAILURE() << "35=" << mt << " is not in "
                          << (qf == qf_walk.end() ? "the FIX50SP2.xml walk" : "the Orchestra walk");
            continue;
        }
        compare_msgtype(mt, qf->second, orch->second, out);
    }
    std::ranges::sort(out, [](Finding const& a, Finding const& b) {
        return std::tie(a.msg_type, a.path) < std::tie(b.msg_type, b.path);
    });
    return out;
}

// Walks one inline QuickFIX-XML document through the shared Walker.
WalkMap walk_qf_fragment(char const* xml) {
    pugi::xml_document d;
    if (!d.load_string(xml)) {
        ADD_FAILURE() << "pugixml could not load the QuickFIX fragment";
        return {};
    }
    return Walker{d}.walk_all();
}

// Fixture: FIX50SP2.xml through the shared Walker and the Orchestra file through OrchestraWalker,
// each loaded and walked once.
class TailRows096Differential : public ::testing::Test {
protected:
    struct Shared {
        std::string load_error;
        WalkMap qf_walk;
        WalkMap orch_walk;
    };

    static void SetUpTestSuite() {
        if (shared_) return;
        shared_ = std::make_unique<Shared>();
        std::string const qf_path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX50SP2.xml";
        pugi::xml_document qf_doc;
        if (pugi::xml_parse_result const r = qf_doc.load_file(qf_path.c_str()); !r) {
            shared_->load_error = "pugixml could not load " + qf_path + ": " + r.description();
            return;
        }
        shared_->qf_walk = Walker{qf_doc}.walk_all();
        std::string const orch_path =
            std::string(FIXPP_ORCHESTRA_DATA_DIR) + "/OrchestraFIXLatest.xml";
        pugi::xml_document orch_doc;
        if (pugi::xml_parse_result const r = orch_doc.load_file(orch_path.c_str()); !r) {
            shared_->load_error = "pugixml could not load " + orch_path + ": " + r.description();
            return;
        }
        OrchestraWalk w = OrchestraWalker{orch_doc}.walk_all();
        shared_->orch_walk = std::move(w.walk);
        if (!w.error.empty()) {
            shared_->load_error = "Orchestra walk of " + orch_path + ": " + w.error;
        }
    }

    void SetUp() override { ASSERT_TRUE(shared_->load_error.empty()) << shared_->load_error; }

    static WalkMap const& qf_walk() { return shared_->qf_walk; }
    static WalkMap const& orch_walk() { return shared_->orch_walk; }

    static std::unique_ptr<Shared> shared_;
};

std::unique_ptr<TailRows096Differential::Shared> TailRows096Differential::shared_;

}  // namespace

// ── The walk's known positives ───────────────────────────────────────────────

// Synthetic fragments (contracts W6): each states, as a literal, what the rule above makes of it.
TEST_F(TailRows096LatestCensus, OrchestraWalkOnSyntheticFragments) {
    // S2: a required group whose first emission is a group whose own first emission is again a
    // group. Group names differ from their count fields' names.
    auto const s2 = [] {
        SCOPED_TRACE("S2");
        OrchestraWalk const w = walk_fragment(
            "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository'>"
            "<fixr:fields>"
            "<fixr:field id='9101' name='NoOuter' type='NumInGroup'/>"
            "<fixr:field id='9102' name='NoMiddle' type='NumInGroup'/>"
            "<fixr:field id='9103' name='NoInner' type='NumInGroup'/>"
            "<fixr:field id='9104' name='InnerField' type='String'/>"
            "</fixr:fields><fixr:components/><fixr:groups>"
            "<fixr:group id='9201' name='OuterGrp'><fixr:numInGroup id='9101'/>"
            "<fixr:groupRef id='9202'/></fixr:group>"
            "<fixr:group id='9202' name='MiddleGrp'><fixr:numInGroup id='9102'/>"
            "<fixr:groupRef id='9203'/></fixr:group>"
            "<fixr:group id='9203' name='InnerGrp'><fixr:numInGroup id='9103'/>"
            "<fixr:fieldRef id='9104'/></fixr:group>"
            "</fixr:groups><fixr:messages>"
            "<fixr:message name='SynthTwo' msgType='S2'><fixr:structure>"
            "<fixr:groupRef id='9201' presence='required'/>"
            "</fixr:structure></fixr:message>"
            "</fixr:messages></fixr:repository>");
        ASSERT_EQ(w.walk.count("S2"), 1U);
        EXPECT_STREQ(to_string(w.walk.at("S2").status), "delimiter_chain") << "S2";
        EXPECT_EQ(w.walk.at("S2").element, "NoInner") << "S2";
    };
    // S4: an optional componentRef holding a required fieldRef, beside a required field.
    auto const s4 = [] {
        SCOPED_TRACE("S4");
        OrchestraWalk const w = walk_fragment(
            "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository'>"
            "<fixr:fields>"
            "<fixr:field id='9400' name='SynthRequired' type='String'/>"
            "<fixr:field id='9401' name='SynthInOptionalComponent' type='String'/>"
            "</fixr:fields><fixr:components>"
            "<fixr:component id='9450' name='OptionalWrap'>"
            "<fixr:fieldRef id='9401' presence='required'/></fixr:component>"
            "</fixr:components><fixr:groups/><fixr:messages>"
            "<fixr:message name='SynthFour' msgType='S4'><fixr:structure>"
            "<fixr:fieldRef id='9400' presence='required'/>"
            "<fixr:componentRef id='9450'/>"
            "</fixr:structure></fixr:message>"
            "</fixr:messages></fixr:repository>");
        ASSERT_EQ(w.walk.count("S4"), 1U);
        EXPECT_STREQ(to_string(w.walk.at("S4").status), "ok") << "S4";
        EXPECT_EQ(w.walk.at("S4").fields, (std::vector<std::uint16_t>{9400})) << "S4";
        EXPECT_TRUE(w.walk.at("S4").groups.empty()) << "S4";
    };
    // S5: two fixr:message elements sharing one msgType.
    auto const s5 = [] {
        SCOPED_TRACE("S5");
        OrchestraWalk const w = walk_fragment(
            "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository'>"
            "<fixr:fields><fixr:field id='9500' name='SynthFive' type='String'/></fixr:fields>"
            "<fixr:components/><fixr:groups/><fixr:messages>"
            "<fixr:message name='SynthFiveA' msgType='S5'><fixr:structure>"
            "<fixr:fieldRef id='9500' presence='required'/>"
            "</fixr:structure></fixr:message>"
            "<fixr:message name='SynthFiveB' msgType='S5'><fixr:structure>"
            "<fixr:fieldRef id='9500'/>"
            "</fixr:structure></fixr:message>"
            "</fixr:messages></fixr:repository>");
        EXPECT_EQ(w.error, "duplicate fixr:message msgType S5") << "S5";
    };
    // S6: a required fieldRef inside fixr:structure and a decoy directly under fixr:message.
    auto const s6 = [] {
        SCOPED_TRACE("S6");
        OrchestraWalk const w = walk_fragment(
            "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository'>"
            "<fixr:fields>"
            "<fixr:field id='9301' name='SynthInStructure' type='String'/>"
            "<fixr:field id='9302' name='SynthDecoy' type='String'/>"
            "</fixr:fields><fixr:components/><fixr:groups/><fixr:messages>"
            "<fixr:message name='SynthSix' msgType='S6'>"
            "<fixr:fieldRef id='9302' presence='required'/>"
            "<fixr:structure><fixr:fieldRef id='9301' presence='required'/></fixr:structure>"
            "</fixr:message>"
            "</fixr:messages></fixr:repository>");
        ASSERT_EQ(w.walk.count("S6"), 1U);
        EXPECT_EQ(w.walk.at("S6").fields, (std::vector<std::uint16_t>{9301})) << "S6";
    };
    s2();
    s4();
    s5();
    s6();
}

// Holds suite TailRows096Latest (selected by its exact name: TailRows096Latest is a prefix of
// TailRows096LatestCensus) to the tail table: every finding is collected before the one assertion.
TEST_F(TailRows096LatestCensus, EveryTailMsgTypeHasExactlyOneWitness) {
    std::map<std::string, std::string> row_of_msgtype;
    std::map<std::string, int> multiplicity;
    for (TailRow const& r : tail_rows()) {
        row_of_msgtype[r.msgtype] = r.row;
        multiplicity[r.msgtype] = 0;
    }
    std::vector<std::string> findings;
    ::testing::UnitTest const* const ut = ::testing::UnitTest::GetInstance();
    for (int s = 0; s < ut->total_test_suite_count(); ++s) {
        ::testing::TestSuite const* const suite = ut->GetTestSuite(s);
        if (std::string_view{suite->name()} != "TailRows096Latest") continue;
        for (int t = 0; t < suite->total_test_count(); ++t) {
            std::string const name = suite->GetTestInfo(t)->name();
            auto const [row, generated] = split_test_name(name);
            std::string const mt = msgtype_for_orchestra_name(doc(), generated);
            if (mt.empty()) {
                std::string f = "unmapped name TailRows096Latest.";
                f += name;
                f += ": no <fixr:message name='";
                f += generated;
                f += "'> in OrchestraFIXLatest.xml";
                findings.push_back(std::move(f));
                continue;
            }
            auto const it = row_of_msgtype.find(mt);
            if (it == row_of_msgtype.end()) {
                std::string f = "extra MsgType 35=";
                f += mt;
                f += " (TailRows096Latest.";
                f += name;
                f += ")";
                findings.push_back(std::move(f));
                continue;
            }
            ++multiplicity[mt];
            if (drop_hyphen(it->second) != row) {
                std::string f = "row mismatch: TailRows096Latest.";
                f += name;
                f += " names row ";
                f += row;
                f += ", 35=";
                f += mt;
                f += " is row ";
                f += it->second;
                findings.push_back(std::move(f));
            }
        }
    }
    for (auto const& [mt, n] : multiplicity) {
        if (n != 1) {
            findings.push_back("35=" + mt + " has multiplicity " + std::to_string(n) +
                               " (exactly one witness required)");
        }
    }
    EXPECT_TRUE(findings.empty()) << "census findings: " << join(findings);
}

TEST_F(TailRows096LatestCensus, FieldLegMatchesQuickFixCppGolden) {
    std::vector<std::string> asserted;
    for (TailRow const& r : tail_rows()) asserted.emplace_back(r.msgtype);

    std::ifstream in{FIXPP_095_QUICKFIX_GOLDEN_CSV};
    ASSERT_TRUE(in) << "cannot open " << FIXPP_095_QUICKFIX_GOLDEN_CSV;
    std::map<std::string, std::vector<std::set<std::uint16_t>>> rows;
    for (std::string line; std::getline(in, line);) {
        if (!line.starts_with("FIX50SP2,")) continue;
        std::size_t const c2 = line.find(',', 9);
        if (c2 == std::string::npos) continue;
        std::string const mt = line.substr(9, c2 - 9);
        std::string tags = line.substr(c2 + 1);
        std::erase(tags, '"');
        std::set<std::uint16_t> set;
        std::istringstream ts{tags};
        for (unsigned t = 0; ts >> t;) set.insert(static_cast<std::uint16_t>(t));
        rows[mt].push_back(std::move(set));
    }
    bool rows_ok = true;
    for (std::string const& mt : asserted) {
        std::size_t const n = rows.contains(mt) ? rows[mt].size() : 0;
        if (n != 1) {
            rows_ok = false;
            ADD_FAILURE() << "golden.csv holds " << n << " rows FIX50SP2," << mt
                          << ", (exactly one required)";
        }
    }
    ASSERT_TRUE(rows_ok);
    for (std::string const& mt : asserted) {
        auto const it = walk().find(mt);
        ASSERT_NE(it, walk().end()) << "35=" << mt << " is not in the Orchestra walk";
        std::set<std::uint16_t> const walked(it->second.fields.begin(), it->second.fields.end());
        std::set<std::uint16_t> const& golden = rows[mt].front();
        EXPECT_EQ(walked, golden) << "35=" << mt << ": Orchestra walk " << join_tags(walked)
                                  << ", golden.csv " << join_tags(golden);
    }
}

// 095's deficient 35=E / 35=CC frames, parsed under the Orchestra dictionary and checked against
// the Orchestra walk. Each expected list is a dictionary fact of the Orchestra file: re-derive it
// with the header's awk recipe over 35=E, group 2030 (NoOrders), and over 35=CC, group 2148
// (NoAsgnReqs), group 1012 (NoPartyIDs), then data-model §B's leg order; never from the walk's
// output. The frame bodies are 095's, byte for byte (quickstart §1b).
TEST_F(TailRows096LatestCensus, KnownPositive_DeficientFrames) {
    ASSERT_STREQ(to_string(walk().at("E").status), "ok") << "35=E at " << walk().at("E").element;
    ASSERT_STREQ(to_string(walk().at("CC").status), "ok") << "35=CC at " << walk().at("CC").element;

    struct Frame {
        char const* name;
        std::string body;
        std::vector<std::string> expected;
    };
    // clang-format off
    std::string const e_head =
        "35=E\x01"
        "66=E_list\x01"
        "394=1\x01"
        "68=2\x01";
    std::string const cc_head =
        "35=CC\x01"
        "1497=CC_req\x01"
        "1498=1\x01";
    std::vector<Frame> const frames = {
        {.name = "E-absent", .body = e_head, .expected = {"73"}},
        {.name = "E-one",
         .body = e_head + "73=1\x01"
                          "11=O0\x01"
                          "67=1\x01"
                          "54=1\x01",
         .expected = {"73"}},
        {.name = "E-short",
         .body = e_head + "73=2\x01"
                          "11=O0\x01"
                          "67=1\x01"
                          "54=1\x01",
         .expected = {"73"}},
        {.name = "E-member",
         .body = e_head + "73=2\x01"
                          "11=O0\x01"
                          "67=1\x01"
                          "54=1\x01"
                          "11=O1\x01"
                          "54=1\x01",
         .expected = {"73[1].67"}},
        {.name = "E-nodelim",
         .body = e_head + "73=2\x01"
                          "67=1\x01"
                          "54=1\x01"
                          "11=O1\x01"
                          "67=2\x01"
                          "54=1\x01",
         .expected = {"73[0].11"}},
        {.name = "CC-nodelim",
         .body = cc_head + "1499=2\x01"
                           "146=1\x01"
                           "55=SYM\x01"
                           "453=1\x01"
                           "448=P1\x01",
         .expected = {"1499[0].453"}},
        {.name = "CC-nested",
         .body = cc_head + "1499=2\x01"
                           "453=1\x01"
                           "447=D\x01"
                           "453=1\x01"
                           "448=P1\x01",
         .expected = {"1499[0].448"}},
        {.name = "CC-count2",
         .body = cc_head + "1499=2\x01"
                           "453=2\x01"
                           "448=PA\x01"
                           "448=PB\x01"
                           "453=1\x01"
                           "448=PC\x01",
         .expected = {"1499[0].453"}},
    };
    // clang-format on
    for (Frame const& f : frames) {
        SCOPED_TRACE(f.name);
        std::pmr::monotonic_buffer_resource arena{65536};
        std::vector<std::byte> const frame = make_frame("FIX.5.0SP2", f.body);
        bool refused = false;
        auto const mv = parse_noting_refusal(frame, tv(), &arena, refused);
        ASSERT_FALSE(refused) << f.name << ": the parser refused the frame";
        EXPECT_EQ(required_members_missing(walk(), mv), f.expected) << f.name;
    }
}

TEST_F(TailRows096LatestCensus, NestedRequiredGroupFailsLoud_MassQuote) {
    auto const it = walk().find("i");
    ASSERT_NE(it, walk().end());
    EXPECT_STREQ(to_string(it->second.status), "nested_required_group") << "35=i";
    EXPECT_EQ(it->second.element, "NoQuoteEntries") << "35=i";
}

// ── Witnesses: one per tail MsgType (contracts W1–W5) ─────────────────────────
//
// Each value is written twice, as the same literal: once in its intent entry, once in its Args
// setter (data-model §C; an enum literal is the first fixr:code value of the field's fixr:codeSet
// in the Orchestra file). Neither statement reads the other.

TEST_F(TailRows096Latest, A035_CrossRequest) {
    Intent intent;
    fixpp::vlatest::CrossRequestArgs args{};
    intent.emplace_back("2672", "A035_CrossRequest_cross_request_id");
    args.cross_request_id = "A035_CrossRequest_cross_request_id";
    intent.emplace_back("376", "A035_CrossRequest_compliance_id");
    args.compliance_id = "A035_CrossRequest_compliance_id";
    auto const built = fixpp::vlatest::build_CrossRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_CrossRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A036_CrossRequestAck) {
    Intent intent;
    fixpp::vlatest::CrossRequestAckArgs args{};
    intent.emplace_back("2672", "A036_CrossRequestAck_cross_request_id");
    args.cross_request_id = "A036_CrossRequestAck_cross_request_id";
    intent.emplace_back("376", "A036_CrossRequestAck_compliance_id");
    args.compliance_id = "A036_CrossRequestAck_compliance_id";
    auto const built = fixpp::vlatest::build_CrossRequestAck(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_CrossRequestAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A037_MassOrder) {
    Intent intent;
    fixpp::vlatest::MassOrderArgs args{};
    std::array<fixpp::vlatest::groups::G_2428_1Args, 2> order_entries{};
    intent.emplace_back("2423", "A037_MassOrder_mass_order_request_id");
    args.mass_order_request_id = "A037_MassOrder_mass_order_request_id";
    intent.emplace_back("58", "A037_MassOrder_text");
    args.text = "A037_MassOrder_text";
    intent.emplace_back("2428", "2");
    intent.emplace_back("2428[0].2429", "1");
    order_entries[0].order_entry_action = '1';
    intent.emplace_back("2428[0].11", "A037_MassOrder_cl_ord_id_i0");
    order_entries[0].cl_ord_id = "A037_MassOrder_cl_ord_id_i0";
    intent.emplace_back("2428[1].2429", "1");
    order_entries[1].order_entry_action = '1';
    intent.emplace_back("2428[1].11", "A037_MassOrder_cl_ord_id_i1");
    order_entries[1].cl_ord_id = "A037_MassOrder_cl_ord_id_i1";
    args.order_entries = std::span<fixpp::vlatest::groups::G_2428_1Args const>{order_entries};
    auto const built = fixpp::vlatest::build_MassOrder(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_MassOrder refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A038_MassOrderAck) {
    Intent intent;
    fixpp::vlatest::MassOrderAckArgs args{};
    intent.emplace_back("2425", "1");
    args.mass_order_request_status = 1;
    intent.emplace_back("1815", "1");
    args.trading_capacity = 1;
    auto const built = fixpp::vlatest::build_MassOrderAck(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_MassOrderAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A039_TradeMatchReport) {
    Intent intent;
    fixpp::vlatest::TradeMatchReportArgs args{};
    intent.emplace_back("880", "A039_TradeMatchReport_trd_match_id");
    args.trd_match_id = "A039_TradeMatchReport_trd_match_id";
    intent.emplace_back("1300", "A039_TradeMatchReport_market_segment_id");
    args.market_segment_id = "A039_TradeMatchReport_market_segment_id";
    auto const built = fixpp::vlatest::build_TradeMatchReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeMatchReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A040_MarketDataReport) {
    Intent intent;
    fixpp::vlatest::MarketDataReportArgs args{};
    intent.emplace_back("2535", "1");
    args.md_report_event = 1;
    intent.emplace_back("2536", "2536");
    args.md_report_count = 2536;
    auto const built = fixpp::vlatest::build_MarketDataReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A041_MarketDataStatisticsRequest) {
    Intent intent;
    fixpp::vlatest::MarketDataStatisticsRequestArgs args{};
    std::array<fixpp::vlatest::groups::G_2474_1Args, 2> md_statistics{};
    intent.emplace_back("2452", "A041_MarketDataStatisticsRequest_md_statistic_req_id");
    args.md_statistic_req_id = "A041_MarketDataStatisticsRequest_md_statistic_req_id";
    intent.emplace_back("263", "0");
    args.subscription_request_type = '0';
    intent.emplace_back("2474", "2");
    intent.emplace_back("2474[0].2475", "A041_MarketDataStatisticsRequest_md_statistic_id_i0");
    md_statistics[0].md_statistic_id = "A041_MarketDataStatisticsRequest_md_statistic_id_i0";
    intent.emplace_back("2474[1].2475", "A041_MarketDataStatisticsRequest_md_statistic_id_i1");
    md_statistics[1].md_statistic_id = "A041_MarketDataStatisticsRequest_md_statistic_id_i1";
    args.md_statistics = std::span<fixpp::vlatest::groups::G_2474_1Args const>{md_statistics};
    auto const built = fixpp::vlatest::build_MarketDataStatisticsRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataStatisticsRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A042_MarketDataStatisticsReport) {
    Intent intent;
    fixpp::vlatest::MarketDataStatisticsReportArgs args{};
    std::array<fixpp::vlatest::groups::G_2474_2Args, 2> md_statistics{};
    intent.emplace_back("2453", "A042_MarketDataStatisticsReport_md_statistic_rpt_id");
    args.md_statistic_rpt_id = "A042_MarketDataStatisticsReport_md_statistic_rpt_id";
    intent.emplace_back("2474", "2");
    intent.emplace_back("2474[0].2456", "1");
    md_statistics[0].md_statistic_type = 1;
    intent.emplace_back("2474[0].2475", "A042_MarketDataStatisticsReport_md_statistic_id_i0");
    md_statistics[0].md_statistic_id = "A042_MarketDataStatisticsReport_md_statistic_id_i0";
    intent.emplace_back("2474[1].2456", "1");
    md_statistics[1].md_statistic_type = 1;
    intent.emplace_back("2474[1].2475", "A042_MarketDataStatisticsReport_md_statistic_id_i1");
    md_statistics[1].md_statistic_id = "A042_MarketDataStatisticsReport_md_statistic_id_i1";
    args.md_statistics = std::span<fixpp::vlatest::groups::G_2474_2Args const>{md_statistics};
    auto const built = fixpp::vlatest::build_MarketDataStatisticsReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataStatisticsReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A043_PartyDetailsListRequest) {
    Intent intent;
    fixpp::vlatest::PartyDetailsListRequestArgs args{};
    intent.emplace_back("1505", "A043_PartyDetailsListRequest_party_details_list_request_id");
    args.party_details_list_request_id =
        "A043_PartyDetailsListRequest_party_details_list_request_id";
    intent.emplace_back("58", "A043_PartyDetailsListRequest_text");
    args.text = "A043_PartyDetailsListRequest_text";
    auto const built = fixpp::vlatest::build_PartyDetailsListRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyDetailsListRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A044_PartyDetailsListReport) {
    Intent intent;
    fixpp::vlatest::PartyDetailsListReportArgs args{};
    intent.emplace_back("1510", "A044_PartyDetailsListReport_party_details_list_report_id");
    args.party_details_list_report_id = "A044_PartyDetailsListReport_party_details_list_report_id";
    intent.emplace_back("58", "A044_PartyDetailsListReport_text");
    args.text = "A044_PartyDetailsListReport_text";
    auto const built = fixpp::vlatest::build_PartyDetailsListReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyDetailsListReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A045_PartyDetailsListUpdateReport) {
    Intent intent;
    fixpp::vlatest::PartyDetailsListUpdateReportArgs args{};
    intent.emplace_back("1510", "A045_PartyDetailsListUpdateReport_party_details_list_report_id");
    args.party_details_list_report_id =
        "A045_PartyDetailsListUpdateReport_party_details_list_report_id";
    intent.emplace_back("58", "A045_PartyDetailsListUpdateReport_text");
    args.text = "A045_PartyDetailsListUpdateReport_text";
    auto const built = fixpp::vlatest::build_PartyDetailsListUpdateReport(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyDetailsListUpdateReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

// NoPartyUpdates' instances carry no string or int member: ListUpdateAction, the delimiter, takes
// in instance k the k-th value the Orchestra file declares for it (data-model §C, implementation
// decision). Re-derive, with O=dictionaries/orchestra/OrchestraFIXLatest.xml:
//   awk '/<fixr:codeSet [^>]*name="ListUpdateActionCodeSet"/,/<\/fixr:codeSet>/' "$O"
TEST_F(TailRows096Latest, A046_PartyDetailsDefinitionRequest) {
    Intent intent;
    fixpp::vlatest::PartyDetailsDefinitionRequestArgs args{};
    std::array<fixpp::vlatest::groups::G_1676_1Args, 2> party_updates{};
    intent.emplace_back("1505", "A046_PartyDetailsDefinitionRequest_party_details_list_request_id");
    args.party_details_list_request_id =
        "A046_PartyDetailsDefinitionRequest_party_details_list_request_id";
    intent.emplace_back("1676", "2");
    intent.emplace_back("1676[0].1324", "A");
    party_updates[0].list_update_action = 'A';
    intent.emplace_back("1676[1].1324", "D");
    party_updates[1].list_update_action = 'D';
    args.party_updates = std::span<fixpp::vlatest::groups::G_1676_1Args const>{party_updates};
    auto const built = fixpp::vlatest::build_PartyDetailsDefinitionRequest(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyDetailsDefinitionRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A047_PartyDetailsDefinitionRequestAck) {
    Intent intent;
    fixpp::vlatest::PartyDetailsDefinitionRequestAckArgs args{};
    intent.emplace_back("1505",
                        "A047_PartyDetailsDefinitionRequestAck_party_details_list_request_id");
    args.party_details_list_request_id =
        "A047_PartyDetailsDefinitionRequestAck_party_details_list_request_id";
    intent.emplace_back("1878", "0");
    args.party_detail_request_status = 0;
    auto const built = fixpp::vlatest::build_PartyDetailsDefinitionRequestAck(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyDetailsDefinitionRequestAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A048_PartyEntitlementsRequest) {
    Intent intent;
    fixpp::vlatest::PartyEntitlementsRequestArgs args{};
    intent.emplace_back("1770", "A048_PartyEntitlementsRequest_entitlement_request_id");
    args.entitlement_request_id = "A048_PartyEntitlementsRequest_entitlement_request_id";
    intent.emplace_back("58", "A048_PartyEntitlementsRequest_text");
    args.text = "A048_PartyEntitlementsRequest_text";
    auto const built = fixpp::vlatest::build_PartyEntitlementsRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyEntitlementsRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A049_PartyEntitlementsReport) {
    Intent intent;
    fixpp::vlatest::PartyEntitlementsReportArgs args{};
    intent.emplace_back("1771", "A049_PartyEntitlementsReport_entitlement_report_id");
    args.entitlement_report_id = "A049_PartyEntitlementsReport_entitlement_report_id";
    intent.emplace_back("58", "A049_PartyEntitlementsReport_text");
    args.text = "A049_PartyEntitlementsReport_text";
    auto const built = fixpp::vlatest::build_PartyEntitlementsReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyEntitlementsReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A050_PartyEntitlementsUpdateReport) {
    Intent intent;
    fixpp::vlatest::PartyEntitlementsUpdateReportArgs args{};
    std::array<fixpp::vlatest::groups::G_1772_2Args, 2> party_entitlements{};
    intent.emplace_back("1771", "A050_PartyEntitlementsUpdateReport_entitlement_report_id");
    args.entitlement_report_id = "A050_PartyEntitlementsUpdateReport_entitlement_report_id";
    intent.emplace_back("1772", "2");
    intent.emplace_back("1772[0].1324", "A");
    party_entitlements[0].list_update_action = 'A';
    intent.emplace_back("1772[0].1885", "A050_PartyEntitlementsUpdateReport_entitlement_ref_id_i0");
    party_entitlements[0].entitlement_ref_id =
        "A050_PartyEntitlementsUpdateReport_entitlement_ref_id_i0";
    intent.emplace_back("1772[1].1324", "A");
    party_entitlements[1].list_update_action = 'A';
    intent.emplace_back("1772[1].1885", "A050_PartyEntitlementsUpdateReport_entitlement_ref_id_i1");
    party_entitlements[1].entitlement_ref_id =
        "A050_PartyEntitlementsUpdateReport_entitlement_ref_id_i1";
    args.party_entitlements =
        std::span<fixpp::vlatest::groups::G_1772_2Args const>{party_entitlements};
    auto const built = fixpp::vlatest::build_PartyEntitlementsUpdateReport(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyEntitlementsUpdateReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A051_PartyEntitlementsDefinitionRequest) {
    Intent intent;
    fixpp::vlatest::PartyEntitlementsDefinitionRequestArgs args{};
    std::array<fixpp::vlatest::groups::G_1772_2Args, 2> party_entitlements{};
    intent.emplace_back("1770", "A051_PartyEntitlementsDefinitionRequest_entitlement_request_id");
    args.entitlement_request_id = "A051_PartyEntitlementsDefinitionRequest_entitlement_request_id";
    intent.emplace_back("1772", "2");
    intent.emplace_back("1772[0].1324", "A");
    party_entitlements[0].list_update_action = 'A';
    intent.emplace_back("1772[0].1885",
                        "A051_PartyEntitlementsDefinitionRequest_entitlement_ref_id_i0");
    party_entitlements[0].entitlement_ref_id =
        "A051_PartyEntitlementsDefinitionRequest_entitlement_ref_id_i0";
    intent.emplace_back("1772[1].1324", "A");
    party_entitlements[1].list_update_action = 'A';
    intent.emplace_back("1772[1].1885",
                        "A051_PartyEntitlementsDefinitionRequest_entitlement_ref_id_i1");
    party_entitlements[1].entitlement_ref_id =
        "A051_PartyEntitlementsDefinitionRequest_entitlement_ref_id_i1";
    args.party_entitlements =
        std::span<fixpp::vlatest::groups::G_1772_2Args const>{party_entitlements};
    auto const built = fixpp::vlatest::build_PartyEntitlementsDefinitionRequest(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyEntitlementsDefinitionRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A052_PartyEntitlementsDefinitionRequestAck) {
    Intent intent;
    fixpp::vlatest::PartyEntitlementsDefinitionRequestAckArgs args{};
    intent.emplace_back("1770",
                        "A052_PartyEntitlementsDefinitionRequestAck_entitlement_request_id");
    args.entitlement_request_id =
        "A052_PartyEntitlementsDefinitionRequestAck_entitlement_request_id";
    intent.emplace_back("1882", "0");
    args.entitlement_request_status = 0;
    auto const built = fixpp::vlatest::build_PartyEntitlementsDefinitionRequestAck(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyEntitlementsDefinitionRequestAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A053_PartyRiskLimitsRequest) {
    Intent intent;
    fixpp::vlatest::PartyRiskLimitsRequestArgs args{};
    intent.emplace_back("1666", "A053_PartyRiskLimitsRequest_risk_limit_request_id");
    args.risk_limit_request_id = "A053_PartyRiskLimitsRequest_risk_limit_request_id";
    intent.emplace_back("58", "A053_PartyRiskLimitsRequest_text");
    args.text = "A053_PartyRiskLimitsRequest_text";
    auto const built = fixpp::vlatest::build_PartyRiskLimitsRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A054_PartyRiskLimitsReport) {
    Intent intent;
    fixpp::vlatest::PartyRiskLimitsReportArgs args{};
    intent.emplace_back("1667", "A054_PartyRiskLimitsReport_risk_limit_report_id");
    args.risk_limit_report_id = "A054_PartyRiskLimitsReport_risk_limit_report_id";
    intent.emplace_back("58", "A054_PartyRiskLimitsReport_text");
    args.text = "A054_PartyRiskLimitsReport_text";
    auto const built = fixpp::vlatest::build_PartyRiskLimitsReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A055_PartyRiskLimitsUpdateReport) {
    Intent intent;
    fixpp::vlatest::PartyRiskLimitsUpdateReportArgs args{};
    intent.emplace_back("1667", "A055_PartyRiskLimitsUpdateReport_risk_limit_report_id");
    args.risk_limit_report_id = "A055_PartyRiskLimitsUpdateReport_risk_limit_report_id";
    intent.emplace_back("58", "A055_PartyRiskLimitsUpdateReport_text");
    args.text = "A055_PartyRiskLimitsUpdateReport_text";
    auto const built = fixpp::vlatest::build_PartyRiskLimitsUpdateReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsUpdateReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A056_PartyRiskLimitsDefinitionRequest) {
    Intent intent;
    fixpp::vlatest::PartyRiskLimitsDefinitionRequestArgs args{};
    intent.emplace_back("1666", "A056_PartyRiskLimitsDefinitionRequest_risk_limit_request_id");
    args.risk_limit_request_id = "A056_PartyRiskLimitsDefinitionRequest_risk_limit_request_id";
    intent.emplace_back("58", "A056_PartyRiskLimitsDefinitionRequest_text");
    args.text = "A056_PartyRiskLimitsDefinitionRequest_text";
    auto const built = fixpp::vlatest::build_PartyRiskLimitsDefinitionRequest(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyRiskLimitsDefinitionRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A057_PartyRiskLimitsDefinitionRequestAck) {
    Intent intent;
    fixpp::vlatest::PartyRiskLimitsDefinitionRequestAckArgs args{};
    intent.emplace_back("1666", "A057_PartyRiskLimitsDefinitionRequestAck_risk_limit_request_id");
    args.risk_limit_request_id = "A057_PartyRiskLimitsDefinitionRequestAck_risk_limit_request_id";
    intent.emplace_back("1762", "0");
    args.risk_limit_request_status = 0;
    auto const built = fixpp::vlatest::build_PartyRiskLimitsDefinitionRequestAck(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyRiskLimitsDefinitionRequestAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A058_PartyRiskLimitsReportAck) {
    Intent intent;
    fixpp::vlatest::PartyRiskLimitsReportAckArgs args{};
    intent.emplace_back("1667", "A058_PartyRiskLimitsReportAck_risk_limit_report_id");
    args.risk_limit_report_id = "A058_PartyRiskLimitsReportAck_risk_limit_report_id";
    intent.emplace_back("2316", "0");
    args.risk_limit_report_status = 0;
    auto const built = fixpp::vlatest::build_PartyRiskLimitsReportAck(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsReportAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A059_PartyActionRequest) {
    Intent intent;
    fixpp::vlatest::PartyActionRequestArgs args{};
    std::array<fixpp::vlatest::groups::G_453Args, 2> party_i_ds{};
    intent.emplace_back("2328", "A059_PartyActionRequest_party_action_request_id");
    args.party_action_request_id = "A059_PartyActionRequest_party_action_request_id";
    intent.emplace_back("2329", "0");
    args.party_action_type = 0;
    intent.emplace_back("453", "2");
    intent.emplace_back("453[0].448", "A059_PartyActionRequest_party_id_i0");
    party_i_ds[0].party_id = "A059_PartyActionRequest_party_id_i0";
    intent.emplace_back("453[1].448", "A059_PartyActionRequest_party_id_i1");
    party_i_ds[1].party_id = "A059_PartyActionRequest_party_id_i1";
    args.party_i_ds = std::span<fixpp::vlatest::groups::G_453Args const>{party_i_ds};
    auto const built = fixpp::vlatest::build_PartyActionRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyActionRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A060_PartyActionReport) {
    Intent intent;
    fixpp::vlatest::PartyActionReportArgs args{};
    std::array<fixpp::vlatest::groups::G_453Args, 2> party_i_ds{};
    intent.emplace_back("2331", "A060_PartyActionReport_party_action_report_id");
    args.party_action_report_id = "A060_PartyActionReport_party_action_report_id";
    intent.emplace_back("2329", "0");
    args.party_action_type = 0;
    intent.emplace_back("2332", "0");
    args.party_action_response = 0;
    intent.emplace_back("453", "2");
    intent.emplace_back("453[0].448", "A060_PartyActionReport_party_id_i0");
    party_i_ds[0].party_id = "A060_PartyActionReport_party_id_i0";
    intent.emplace_back("453[1].448", "A060_PartyActionReport_party_id_i1");
    party_i_ds[1].party_id = "A060_PartyActionReport_party_id_i1";
    args.party_i_ds = std::span<fixpp::vlatest::groups::G_453Args const>{party_i_ds};
    auto const built = fixpp::vlatest::build_PartyActionReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyActionReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A061_PartyRiskLimitCheckRequest) {
    Intent intent;
    fixpp::vlatest::PartyRiskLimitCheckRequestArgs args{};
    intent.emplace_back("2320", "0");
    args.risk_limit_check_trans_type = 0;
    intent.emplace_back("2321", "0");
    args.risk_limit_check_type = 0;
    auto const built = fixpp::vlatest::build_PartyRiskLimitCheckRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitCheckRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A062_PartyRiskLimitCheckRequestAck) {
    Intent intent;
    fixpp::vlatest::PartyRiskLimitCheckRequestAckArgs args{};
    intent.emplace_back("2325", "0");
    args.risk_limit_check_request_status = 0;
    intent.emplace_back("2320", "0");
    args.risk_limit_check_trans_type = 0;
    intent.emplace_back("2321", "0");
    args.risk_limit_check_type = 0;
    auto const built = fixpp::vlatest::build_PartyRiskLimitCheckRequestAck(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyRiskLimitCheckRequestAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A063_SecurityMassStatusRequest) {
    Intent intent;
    fixpp::vlatest::SecurityMassStatusRequestArgs args{};
    intent.emplace_back("324", "A063_SecurityMassStatusRequest_security_status_req_id");
    args.security_status_req_id = "A063_SecurityMassStatusRequest_security_status_req_id";
    intent.emplace_back("263", "0");
    args.subscription_request_type = '0';
    auto const built = fixpp::vlatest::build_SecurityMassStatusRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityMassStatusRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A064_SecurityMassStatus) {
    Intent intent;
    fixpp::vlatest::SecurityMassStatusArgs args{};
    intent.emplace_back("324", "A064_SecurityMassStatus_security_status_req_id");
    args.security_status_req_id = "A064_SecurityMassStatus_security_status_req_id";
    intent.emplace_back("1300", "A064_SecurityMassStatus_market_segment_id");
    args.market_segment_id = "A064_SecurityMassStatus_market_segment_id";
    auto const built = fixpp::vlatest::build_SecurityMassStatus(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityMassStatus refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096Latest, A065_QuoteAck) {
    Intent intent;
    fixpp::vlatest::QuoteAckArgs args{};
    intent.emplace_back("1865", "0");
    args.quote_ack_status = 0;
    intent.emplace_back("117", "A065_QuoteAck_quote_id");
    args.quote_id = "A065_QuoteAck_quote_id";
    intent.emplace_back("131", "A065_QuoteAck_quote_req_id");
    args.quote_req_id = "A065_QuoteAck_quote_req_id";
    auto const built = fixpp::vlatest::build_QuoteAck(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

// ── The differential's known positives (contracts W6, W7 exceptions) ────────────

// Re-derive 35=W's difference: research R-6's sed/awk pair and its golden.csv grep.
TEST_F(TailRows096Differential, KnownDifference_MarketDataSnapshotFullRefresh) {
    EXPECT_EQ(
        diff(qf_walk(), orch_walk(), {"W"}),
        (std::vector<Finding>{
            {.msg_type = "W", .path = "779", .fix50sp2 = "required", .orchestra = "absent"}}));
}

TEST_F(TailRows096Differential, KnownNonOk_MassQuote) {
    EXPECT_EQ(diff(qf_walk(), orch_walk(), {"i"}),
              (std::vector<Finding>{{.msg_type = "i",
                                     .path = "status",
                                     .fix50sp2 = "nested_required_group NoQuoteEntries",
                                     .orchestra = "nested_required_group NoQuoteEntries"}}));
}

// The 31 tail MsgTypes of the tail table: the two dictionaries agree on each (FR-012 stops a row on
// any finding).
TEST_F(TailRows096Differential, TailMsgTypesAgree) {
    std::vector<std::string> msg_types;
    for (TailRow const& r : tail_rows()) msg_types.emplace_back(r.msgtype);
    std::vector<Finding> const findings = diff(qf_walk(), orch_walk(), msg_types);
    EXPECT_TRUE(findings.empty())
        << "FIX50SP2.xml and the Orchestra file differ on the tail MsgTypes: "
        << ::testing::PrintToString(findings);
}

// One QuickFIX-XML fragment and one Orchestra fragment per path, identical but for that path; pair
// M drives collect-every-difference, the sort and the one-sided-group rule.
TEST_F(TailRows096Differential, SyntheticPairs) {
    // Group path: group 9110 required in the QuickFIX fragment, optional in the Orchestra one.
    {
        SCOPED_TRACE("pair G");
        WalkMap const qf = walk_qf_fragment(
            "<fix><header/><trailer/><messages>"
            "<message name='SynthGroup' msgtype='SG' msgcat='app'>"
            "<field name='SynthGKey' required='Y'/>"
            "<group name='NoSynthG' required='Y'><field name='SynthGMember' required='N'/></group>"
            "</message></messages><components/><fields>"
            "<field number='9110' name='NoSynthG' type='NUMINGROUP'/>"
            "<field number='9111' name='SynthGMember' type='STRING'/>"
            "<field number='9112' name='SynthGKey' type='STRING'/>"
            "</fields></fix>");
        OrchestraWalk const orch = walk_fragment(
            "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository'>"
            "<fixr:fields>"
            "<fixr:field id='9110' name='NoSynthG' type='NumInGroup'/>"
            "<fixr:field id='9111' name='SynthGMember' type='String'/>"
            "<fixr:field id='9112' name='SynthGKey' type='String'/>"
            "</fixr:fields><fixr:components/><fixr:groups>"
            "<fixr:group id='9610' name='SynthGGrp'><fixr:numInGroup id='9110'/>"
            "<fixr:fieldRef id='9111'/></fixr:group>"
            "</fixr:groups><fixr:messages>"
            "<fixr:message name='SynthGroup' msgType='SG'><fixr:structure>"
            "<fixr:fieldRef id='9112' presence='required'/>"
            "<fixr:groupRef id='9610'/>"
            "</fixr:structure></fixr:message>"
            "</fixr:messages></fixr:repository>");
        EXPECT_TRUE(orch.error.empty()) << orch.error;
        EXPECT_EQ(diff(qf, orch.walk, {"SG"}), (std::vector<Finding>{{.msg_type = "SG",
                                                                      .path = "9110",
                                                                      .fix50sp2 = "required",
                                                                      .orchestra = "absent"}}));
    }
    // Delimiter path: group 9120 required on both sides, its two optional members declared in
    // opposite orders.
    {
        SCOPED_TRACE("pair D");
        WalkMap const qf = walk_qf_fragment(
            "<fix><header/><trailer/><messages>"
            "<message name='SynthDelim' msgtype='SD' msgcat='app'>"
            "<group name='NoSynthD' required='Y'>"
            "<field name='SynthDOne' required='N'/><field name='SynthDTwo' required='N'/>"
            "</group></message></messages><components/><fields>"
            "<field number='9120' name='NoSynthD' type='NUMINGROUP'/>"
            "<field number='9121' name='SynthDOne' type='STRING'/>"
            "<field number='9122' name='SynthDTwo' type='STRING'/>"
            "</fields></fix>");
        OrchestraWalk const orch = walk_fragment(
            "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository'>"
            "<fixr:fields>"
            "<fixr:field id='9120' name='NoSynthD' type='NumInGroup'/>"
            "<fixr:field id='9121' name='SynthDOne' type='String'/>"
            "<fixr:field id='9122' name='SynthDTwo' type='String'/>"
            "</fixr:fields><fixr:components/><fixr:groups>"
            "<fixr:group id='9620' name='SynthDGrp'><fixr:numInGroup id='9120'/>"
            "<fixr:fieldRef id='9122'/><fixr:fieldRef id='9121'/></fixr:group>"
            "</fixr:groups><fixr:messages>"
            "<fixr:message name='SynthDelim' msgType='SD'><fixr:structure>"
            "<fixr:groupRef id='9620' presence='required'/>"
            "</fixr:structure></fixr:message>"
            "</fixr:messages></fixr:repository>");
        EXPECT_TRUE(orch.error.empty()) << orch.error;
        EXPECT_EQ(diff(qf, orch.walk, {"SD"}), (std::vector<Finding>{{.msg_type = "SD",
                                                                      .path = "9120/delim",
                                                                      .fix50sp2 = "9121",
                                                                      .orchestra = "9122"}}));
    }
    // Instance-member path: group 9130 required on both sides, member 9132 required inside it in
    // the QuickFIX fragment only.
    {
        SCOPED_TRACE("pair I");
        WalkMap const qf = walk_qf_fragment(
            "<fix><header/><trailer/><messages>"
            "<message name='SynthMember' msgtype='SI' msgcat='app'>"
            "<group name='NoSynthI' required='Y'>"
            "<field name='SynthIDelim' required='N'/><field name='SynthIMember' required='Y'/>"
            "</group></message></messages><components/><fields>"
            "<field number='9130' name='NoSynthI' type='NUMINGROUP'/>"
            "<field number='9131' name='SynthIDelim' type='STRING'/>"
            "<field number='9132' name='SynthIMember' type='STRING'/>"
            "</fields></fix>");
        OrchestraWalk const orch = walk_fragment(
            "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository'>"
            "<fixr:fields>"
            "<fixr:field id='9130' name='NoSynthI' type='NumInGroup'/>"
            "<fixr:field id='9131' name='SynthIDelim' type='String'/>"
            "<fixr:field id='9132' name='SynthIMember' type='String'/>"
            "</fixr:fields><fixr:components/><fixr:groups>"
            "<fixr:group id='9630' name='SynthIGrp'><fixr:numInGroup id='9130'/>"
            "<fixr:fieldRef id='9131'/><fixr:fieldRef id='9132'/></fixr:group>"
            "</fixr:groups><fixr:messages>"
            "<fixr:message name='SynthMember' msgType='SI'><fixr:structure>"
            "<fixr:groupRef id='9630' presence='required'/>"
            "</fixr:structure></fixr:message>"
            "</fixr:messages></fixr:repository>");
        EXPECT_TRUE(orch.error.empty()) << orch.error;
        EXPECT_EQ(diff(qf, orch.walk, {"SI"}), (std::vector<Finding>{{.msg_type = "SI",
                                                                      .path = "9130[*].9132",
                                                                      .fix50sp2 = "required",
                                                                      .orchestra = "absent"}}));
    }
    // delim_group path: group 9140 required on both sides, its first child the optional nested
    // group 9141, whose two optional members are declared in opposite orders.
    {
        SCOPED_TRACE("pair X");
        WalkMap const qf = walk_qf_fragment(
            "<fix><header/><trailer/><messages>"
            "<message name='SynthDelimGroup' msgtype='SX' msgcat='app'>"
            "<group name='NoSynthX' required='Y'>"
            "<group name='NoSynthXInner' required='N'>"
            "<field name='SynthXOne' required='N'/><field name='SynthXTwo' required='N'/>"
            "</group></group></message></messages><components/><fields>"
            "<field number='9140' name='NoSynthX' type='NUMINGROUP'/>"
            "<field number='9141' name='NoSynthXInner' type='NUMINGROUP'/>"
            "<field number='9142' name='SynthXOne' type='STRING'/>"
            "<field number='9143' name='SynthXTwo' type='STRING'/>"
            "</fields></fix>");
        OrchestraWalk const orch = walk_fragment(
            "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository'>"
            "<fixr:fields>"
            "<fixr:field id='9140' name='NoSynthX' type='NumInGroup'/>"
            "<fixr:field id='9141' name='NoSynthXInner' type='NumInGroup'/>"
            "<fixr:field id='9142' name='SynthXOne' type='String'/>"
            "<fixr:field id='9143' name='SynthXTwo' type='String'/>"
            "</fixr:fields><fixr:components/><fixr:groups>"
            "<fixr:group id='9640' name='SynthXGrp'><fixr:numInGroup id='9140'/>"
            "<fixr:groupRef id='9641'/></fixr:group>"
            "<fixr:group id='9641' name='SynthXInnerGrp'><fixr:numInGroup id='9141'/>"
            "<fixr:fieldRef id='9143'/><fixr:fieldRef id='9142'/></fixr:group>"
            "</fixr:groups><fixr:messages>"
            "<fixr:message name='SynthDelimGroup' msgType='SX'><fixr:structure>"
            "<fixr:groupRef id='9640' presence='required'/>"
            "</fixr:structure></fixr:message>"
            "</fixr:messages></fixr:repository>");
        EXPECT_TRUE(orch.error.empty()) << orch.error;
        EXPECT_EQ(diff(qf, orch.walk, {"SX"}), (std::vector<Finding>{{.msg_type = "SX",
                                                                      .path = "9140/delim_group",
                                                                      .fix50sp2 = "9141,9142",
                                                                      .orchestra = "9141,9143"}}));
    }
    // Pair M (contracts W6): SM declares 9105 (required in the Orchestra fragment only), 9102
    // (required in the QuickFIX fragment only), group 9100 (required on both sides; members 9104,
    // required in the QuickFIX fragment only, and 9103, required on both) and 9101 (required on
    // both). SA declares group 9200, required in the QuickFIX fragment only, whose member 9201 is
    // required inside it on both sides. Passed in the order SM, SA.
    {
        SCOPED_TRACE("pair M");
        WalkMap const qf = walk_qf_fragment(
            "<fix><header/><trailer/><messages>"
            "<message name='SynthMulti' msgtype='SM' msgcat='app'>"
            "<field name='SynthMFive' required='N'/>"
            "<field name='SynthMTwo' required='Y'/>"
            "<group name='NoSynthM' required='Y'>"
            "<field name='SynthMFour' required='Y'/><field name='SynthMThree' required='Y'/>"
            "</group>"
            "<field name='SynthMOne' required='Y'/>"
            "</message>"
            "<message name='SynthAlone' msgtype='SA' msgcat='app'>"
            "<group name='NoSynthA' required='Y'><field name='SynthAMember' required='Y'/></group>"
            "</message></messages><components/><fields>"
            "<field number='9100' name='NoSynthM' type='NUMINGROUP'/>"
            "<field number='9101' name='SynthMOne' type='STRING'/>"
            "<field number='9102' name='SynthMTwo' type='STRING'/>"
            "<field number='9103' name='SynthMThree' type='STRING'/>"
            "<field number='9104' name='SynthMFour' type='STRING'/>"
            "<field number='9105' name='SynthMFive' type='STRING'/>"
            "<field number='9200' name='NoSynthA' type='NUMINGROUP'/>"
            "<field number='9201' name='SynthAMember' type='STRING'/>"
            "</fields></fix>");
        OrchestraWalk const orch = walk_fragment(
            "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository'>"
            "<fixr:fields>"
            "<fixr:field id='9100' name='NoSynthM' type='NumInGroup'/>"
            "<fixr:field id='9101' name='SynthMOne' type='String'/>"
            "<fixr:field id='9102' name='SynthMTwo' type='String'/>"
            "<fixr:field id='9103' name='SynthMThree' type='String'/>"
            "<fixr:field id='9104' name='SynthMFour' type='String'/>"
            "<fixr:field id='9105' name='SynthMFive' type='String'/>"
            "<fixr:field id='9200' name='NoSynthA' type='NumInGroup'/>"
            "<fixr:field id='9201' name='SynthAMember' type='String'/>"
            "</fixr:fields><fixr:components/><fixr:groups>"
            "<fixr:group id='9700' name='SynthMGrp'><fixr:numInGroup id='9100'/>"
            "<fixr:fieldRef id='9104'/><fixr:fieldRef id='9103' presence='required'/>"
            "</fixr:group>"
            "<fixr:group id='9800' name='SynthAGrp'><fixr:numInGroup id='9200'/>"
            "<fixr:fieldRef id='9201' presence='required'/></fixr:group>"
            "</fixr:groups><fixr:messages>"
            "<fixr:message name='SynthMulti' msgType='SM'><fixr:structure>"
            "<fixr:fieldRef id='9105' presence='required'/>"
            "<fixr:fieldRef id='9102'/>"
            "<fixr:groupRef id='9700' presence='required'/>"
            "<fixr:fieldRef id='9101' presence='required'/>"
            "</fixr:structure></fixr:message>"
            "<fixr:message name='SynthAlone' msgType='SA'><fixr:structure>"
            "<fixr:groupRef id='9800'/>"
            "</fixr:structure></fixr:message>"
            "</fixr:messages></fixr:repository>");
        EXPECT_TRUE(orch.error.empty()) << orch.error;
        EXPECT_EQ(
            diff(qf, orch.walk, {"SM", "SA"}),
            (std::vector<Finding>{
                {.msg_type = "SA", .path = "9200", .fix50sp2 = "required", .orchestra = "absent"},
                {.msg_type = "SM",
                 .path = "9100[*].9104",
                 .fix50sp2 = "required",
                 .orchestra = "absent"},
                {.msg_type = "SM", .path = "9102", .fix50sp2 = "required", .orchestra = "absent"},
                {.msg_type = "SM",
                 .path = "9105",
                 .fix50sp2 = "absent",
                 .orchestra = "required"}}));
    }
}
