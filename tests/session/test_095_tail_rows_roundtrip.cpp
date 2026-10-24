// SPDX-License-Identifier: AGPL-3.0-or-later
// test_095_tail_rows_roundtrip.cpp
//
// 095-tail-evidence-pass: one offline witness per tail MsgType (suite TailRows095), the
// dictionary-derived presence check every witness asserts, and the known positives that prove that
// check able to fire (suite TailRows095Census). Second source of the
// test_077_allversions_builder_roundtrip executable; the 077 TU beside it is left unchanged.
//
// THE RULE (spec Terminology; research R-1), applied to dictionaries/FIX50SP2.xml at test runtime:
//   - a field is required when every element on its path from the message (each enclosing
//     component reference, group and the field itself) is required='Y';
//   - a group is required when every element on its path is required, where a group that is the
//     ONLY child of a component (children counted on the component's definition as written, a
//     nested component reference counting as one) inherits the required flag of the reference that
//     wraps it;
//   - a group instance's required members follow the same rules, starting from the group;
//   - a group's delimiter is its first emitted tag in dictionary order with components expanded
//     (the loader's rule, src/dictionary/xml_loader.cpp expand_field_list). When it is a nested
//     group's count tag, that nested group carries exactly one instance.
// The walk fails loudly, naming the element, on a required group inside a required group's
// instance, on a delimiter chain longer than one nested group, and on a required component
// reference whose component holds a group among siblings.
//
// No count, member list or per-MsgType outcome of the walk is recorded here (contracts W7): the
// walk re-derives them on every run. To re-derive one by hand:
//   grep -o "<message name='[^']*' msgtype='<MT>'" dictionaries/FIX50SP2.xml
//   sed -n "/<message name='<Name>'/,/<\/message>/p" dictionaries/FIX50SP2.xml
//   sed -n "/<component name='<Comp>'>/,/<\/component>/p" dictionaries/FIX50SP2.xml
// and apply the rule above to each component on the path. The field leg is compared with
// grep '^FIX50SP2,<MT>,' tools/quickfix_required_golden/golden.csv
// (QuickFIX-cpp's message-level set; it does not carry the group rule, spec FR-004).
//
// The known-positive assertions below (the deficient 35=E / 35=CC frames, the 35=i fail-loud and
// the synthetic dictionaries) state dictionary facts as literals: contracts W7's one exception,
// since they re-run on every invocation of this binary.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fstream>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <pugixml.hpp>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/app_message_read_scaffold.hpp"

namespace {

using fixpp_test_support::bytes_to_string;
using fixpp_test_support::make_decimal;
using fixpp_test_support::make_frame;
using fixpp_test_support::parse_dict;
using IndexView = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

// ── The walk (data-model §B) ─────────────────────────────────────────────────

enum class walk_status { ok, nested_required_group, delimiter_chain, group_among_siblings };

char const* to_string(walk_status s) {
    switch (s) {
        case walk_status::ok:
            return "ok";
        case walk_status::nested_required_group:
            return "nested_required_group";
        case walk_status::delimiter_chain:
            return "delimiter_chain";
        case walk_status::group_among_siblings:
            return "group_among_siblings";
    }
    return "?";
}

struct RequiredGroup {
    std::uint16_t count_tag = 0;
    std::uint16_t delim_tag = 0;
    // Set when delim_tag is a nested group's count tag: (that count tag, its own first tag).
    std::optional<std::pair<std::uint16_t, std::uint16_t>> delim_group;
    std::vector<std::uint16_t> fields;  // one instance's required fields, dictionary order
};

struct WalkResult {
    walk_status status = walk_status::ok;
    std::string element;                // the element a non-ok status names
    std::vector<std::uint16_t> fields;  // required message-level fields, dictionary order
    std::vector<RequiredGroup> groups;  // required groups, dictionary order
};

using WalkMap = std::map<std::string, WalkResult, std::less<>>;

bool is_element(pugi::xml_node n) {
    std::string_view const name = n.name();
    return name == "field" || name == "component" || name == "group";
}

bool is_group(pugi::xml_node n) { return std::string_view{n.name()} == "group"; }

bool is_required(pugi::xml_node n) {
    return std::string_view{n.attribute("required").as_string()} == "Y";
}

std::vector<pugi::xml_node> element_children(pugi::xml_node n) {
    std::vector<pugi::xml_node> out;
    for (pugi::xml_node c : n.children()) {
        if (is_element(c)) out.push_back(c);
    }
    return out;
}

class Walker {
public:
    explicit Walker(pugi::xml_document const& doc) {
        pugi::xml_node const fix = doc.child("fix");
        for (pugi::xml_node f : fix.child("fields").children("field")) {
            tags_[f.attribute("name").as_string()] =
                static_cast<std::uint16_t>(f.attribute("number").as_uint());
        }
        for (pugi::xml_node c : fix.child("components").children("component")) {
            components_[c.attribute("name").as_string()] = c;
        }
        messages_ = fix.child("messages");
    }

    WalkMap walk_all() const {
        WalkMap out;
        for (pugi::xml_node m : messages_.children("message")) {
            out[m.attribute("msgtype").as_string()] = walk_message(m);
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

    std::uint16_t tag_of(pugi::xml_node n) const {
        auto const it = tags_.find(n.attribute("name").as_string());
        return it == tags_.end() ? std::uint16_t{0} : it->second;
    }

    pugi::xml_node component_def(pugi::xml_node ref) const {
        auto const it = components_.find(ref.attribute("name").as_string());
        return it == components_.end() ? pugi::xml_node{} : it->second;
    }

    // The first emitted element (a field or a group) under `n`, components expanded.
    pugi::xml_node first_emitted(pugi::xml_node n) const {
        for (pugi::xml_node c : element_children(n)) {
            if (std::string_view{c.name()} != "component") return c;
            if (pugi::xml_node const inner = first_emitted(component_def(c))) return inner;
        }
        return {};
    }

    // Collects the required fields and groups under `container`, reached along a path whose
    // elements so far are all required iff `path_required`.
    void collect(pugi::xml_node container, bool path_required, std::vector<std::uint16_t>& fields,
                 std::vector<pugi::xml_node>& groups, Failure& fail) const {
        for (pugi::xml_node c : element_children(container)) {
            bool const req = path_required && is_required(c);
            std::string_view const kind = c.name();
            if (kind == "field") {
                if (req && std::find(fields.begin(), fields.end(), tag_of(c)) == fields.end()) {
                    fields.push_back(tag_of(c));
                }
            } else if (kind == "group") {
                if (req) groups.push_back(c);
            } else {
                pugi::xml_node const def = component_def(c);
                std::vector<pugi::xml_node> const kids = element_children(def);
                if (kids.size() == 1 && is_group(kids.front())) {
                    // Only-child inheritance: the group takes the flag of the reference that wraps
                    // it.
                    bool const group_required = req;
                    if (group_required) groups.push_back(kids.front());
                    continue;
                }
                if (req && std::any_of(kids.begin(), kids.end(), is_group)) {
                    fail.set(walk_status::group_among_siblings, c.attribute("name").as_string());
                }
                collect(def, req, fields, groups, fail);
            }
        }
    }

    WalkResult walk_message(pugi::xml_node m) const {
        WalkResult r;
        Failure fail;
        std::vector<pugi::xml_node> groups;
        collect(m, true, r.fields, groups, fail);
        for (pugi::xml_node g : groups) {
            RequiredGroup rg;
            rg.count_tag = tag_of(g);
            pugi::xml_node const first = first_emitted(g);
            rg.delim_tag = tag_of(first);
            if (is_group(first)) {
                pugi::xml_node const nested_first = first_emitted(first);
                if (is_group(nested_first)) {
                    fail.set(walk_status::delimiter_chain,
                             nested_first.attribute("name").as_string());
                }
                rg.delim_group = std::pair{tag_of(first), tag_of(nested_first)};
            }
            std::vector<pugi::xml_node> inner_groups;
            collect(g, true, rg.fields, inner_groups, fail);
            if (!inner_groups.empty()) {
                fail.set(walk_status::nested_required_group,
                         inner_groups.front().attribute("name").as_string());
            }
            r.groups.push_back(std::move(rg));
        }
        r.status = fail.status;
        r.element = fail.element;
        return r;
    }

    std::map<std::string, std::uint16_t, std::less<>> tags_;
    std::map<std::string, pugi::xml_node, std::less<>> components_;
    pugi::xml_node messages_;
};

// The MsgType of `<message name='<generated_name>' msgtype=…>`, or empty when no message has
// that name. The census and every witness's tag-35 check share this one lookup.
std::string msgtype_for_generated_name(pugi::xml_document const& doc,
                                       std::string_view generated_name) {
    for (pugi::xml_node m : doc.child("fix").child("messages").children("message")) {
        if (generated_name == m.attribute("name").as_string()) {
            return m.attribute("msgtype").as_string();
        }
    }
    return {};
}

// `<RowId>_<GeneratedName>` → its two halves.
std::pair<std::string, std::string> split_test_name(std::string_view test_name) {
    std::size_t const us = test_name.find('_');
    if (us == std::string_view::npos) return {std::string{test_name}, {}};
    return {std::string{test_name.substr(0, us)}, std::string{test_name.substr(us + 1)}};
}

// ── The body enumeration (research R-4, contracts W4) ───────────────────────

struct BodyEntry {
    std::string path;
    std::uint16_t tag = 0;
    std::string value;
    int group = -1;  // index into WalkResult::groups, -1 at message level
    std::size_t instance = 0;
};

struct BodyEnumeration {
    std::vector<BodyEntry> entries;           // document order
    std::vector<std::size_t> instance_count;  // per required group: group_slices(count_tag).size()
};

bool is_framing_or_msgtype(std::uint16_t tag) {
    return tag == 8 || tag == 9 || tag == 10 || tag == 35;
}

// Every parsed body entry but the framing tags and tag 35, each attributed by extent to message
// level or to the instance of the outermost required group whose slice contains it.
BodyEnumeration enumerate_body(IndexView const& mv, WalkResult const& wr) {
    BodyEnumeration out;
    std::vector<std::span<fixpp::wire::group_slice const>> slices;
    for (RequiredGroup const& g : wr.groups) {
        slices.push_back(mv.offsets().group_slices(g.count_tag));
        out.instance_count.push_back(slices.back().size());
    }
    std::byte const* const base = mv.bytes().data();
    for (auto const& e : mv.offsets().entries()) {
        if (is_framing_or_msgtype(e.tag)) continue;
        std::byte const* const value_at = base + e.offset;
        BodyEntry be;
        be.tag = e.tag;
        be.value.assign(reinterpret_cast<char const*>(value_at), e.length);
        for (std::size_t gi = 0; gi < slices.size() && be.group < 0; ++gi) {
            for (std::size_t k = 0; k < slices[gi].size(); ++k) {
                auto const& s = slices[gi][k];
                if (value_at >= s.data && value_at < s.data + s.len) {
                    be.group = static_cast<int>(gi);
                    be.instance = k;
                    break;
                }
            }
        }
        be.path = be.group < 0
                      ? std::to_string(e.tag)
                      : std::to_string(wr.groups[static_cast<std::size_t>(be.group)].count_tag) +
                            "[" + std::to_string(be.instance) + "]." + std::to_string(e.tag);
        out.entries.push_back(std::move(be));
    }
    return out;
}

std::optional<std::string> value_at(BodyEnumeration const& body, int group, std::size_t instance,
                                    std::uint16_t tag) {
    for (BodyEntry const& e : body.entries) {
        if (e.group == group && (group < 0 || e.instance == instance) && e.tag == tag) {
            return e.value;
        }
    }
    return std::nullopt;
}

long long parse_count(std::string const& v) {
    long long n = -1;
    auto const [p, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
    return (ec == std::errc{} && p == v.data() + v.size()) ? n : -1;
}

// The tag a path names: `<tag>` or `<count_tag>[<k>].<tag>`.
std::uint16_t tag_of_path(std::string_view path) {
    std::size_t const dot = path.rfind('.');
    std::string_view const t = dot == std::string_view::npos ? path : path.substr(dot + 1);
    unsigned v = 0;
    std::from_chars(t.data(), t.data() + t.size(), v);
    return static_cast<std::uint16_t>(v);
}

std::string instance_path(std::uint16_t count_tag, std::size_t k, std::uint16_t tag) {
    return std::to_string(count_tag) + "[" + std::to_string(k) + "]." + std::to_string(tag);
}

// The closure-required members the parsed frame lacks, as an ordered sequence with duplicates
// kept (data-model §B's leg order). The MsgType is the frame's own tag 35.
std::vector<std::string> required_members_missing(WalkMap const& walk, IndexView const& mv) {
    std::string const mt{mv.msg_type()};
    auto const it = walk.find(mt);
    if (it == walk.end()) return {"35=" + mt + ": no such message in the walk"};
    WalkResult const& wr = it->second;
    if (wr.status != walk_status::ok) {
        return {"35=" + mt + ": walk status " + to_string(wr.status) + " at " + wr.element};
    }
    BodyEnumeration const body = enumerate_body(mv, wr);
    std::vector<std::string> out;
    std::size_t const ng = wr.groups.size();
    auto const count_of = [&](std::size_t gi) {
        return value_at(body, -1, 0, wr.groups[gi].count_tag);
    };
    // Leg 1: message-level fields.
    for (std::uint16_t f : wr.fields) {
        if (!value_at(body, -1, 0, f)) out.push_back(std::to_string(f));
    }
    // Leg 2: required groups absent.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        if (!count_of(gi)) out.push_back(std::to_string(wr.groups[gi].count_tag));
    }
    // Leg 3: count value below 2.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        if (auto const c = count_of(gi); c && parse_count(*c) < 2) {
            out.push_back(std::to_string(wr.groups[gi].count_tag));
        }
    }
    // Leg 4: instance count differs from the count value.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        if (auto const c = count_of(gi);
            c && parse_count(*c) != static_cast<long long>(body.instance_count[gi])) {
            out.push_back(std::to_string(wr.groups[gi].count_tag));
        }
    }
    // Leg 5 (delimiter leg): each instance's delimiter.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        RequiredGroup const& g = wr.groups[gi];
        for (std::size_t k = 0; k < body.instance_count[gi]; ++k) {
            if (!value_at(body, static_cast<int>(gi), k, g.delim_tag)) {
                out.push_back(instance_path(g.count_tag, k, g.delim_tag));
            }
        }
    }
    // Leg 6 (field leg): each instance's required fields; the delimiter is the delimiter leg's.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        RequiredGroup const& g = wr.groups[gi];
        for (std::size_t k = 0; k < body.instance_count[gi]; ++k) {
            for (std::uint16_t f : g.fields) {
                if (f == g.delim_tag) continue;
                if (!value_at(body, static_cast<int>(gi), k, f)) {
                    out.push_back(instance_path(g.count_tag, k, f));
                }
            }
        }
    }
    // Leg 7 (delim_group leg): where the nested count tag is present in the instance, its value
    // must be 1 and the nested delimiter present. An absent nested count is the delimiter leg's.
    for (std::size_t gi = 0; gi < ng; ++gi) {
        RequiredGroup const& g = wr.groups[gi];
        if (!g.delim_group) continue;
        auto const [nested_count, nested_delim] = *g.delim_group;
        for (std::size_t k = 0; k < body.instance_count[gi]; ++k) {
            auto const n = value_at(body, static_cast<int>(gi), k, nested_count);
            if (!n) continue;
            if (parse_count(*n) != 1) out.push_back(instance_path(g.count_tag, k, nested_count));
            if (!value_at(body, static_cast<int>(gi), k, nested_delim)) {
                out.push_back(instance_path(g.count_tag, k, nested_delim));
            }
        }
    }
    return out;
}

std::string join(std::vector<std::string> const& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + v[i];
    return s + "]";
}

std::string join_tags(std::set<std::uint16_t> const& v) {
    std::string s = "{";
    bool first = true;
    for (std::uint16_t t : v) {
        s += (first ? "" : " ") + std::to_string(t);
        first = false;
    }
    return s + "}";
}

// ── The tail table (data-model §A; the catalogue row is the authority) ───────

struct TailRow {
    char const* row;
    std::vector<char const*> msgtypes;
};

std::vector<TailRow> const& tail_rows() {
    static std::vector<TailRow> const rows = {
        {"A-018", {"BN"}},
        {"A-023", {"CA", "BZ"}},
        {"A-027", {"BI", "BJ", "BS"}},
        {"A-028", {"BT", "BU", "BV"}},
        {"A-029", {"BK", "BP"}},
        {"A-030", {"BQ"}},
        {"A-031", {"BM"}},
        {"A-032", {"CB"}},
        {"A-033", {"CC", "CD", "CE"}},
        {"C-003", {"CQ"}},
        {"N-002", {"BE", "BF"}},
        {"N-003", {"BW", "BX", "BY"}},
    };
    return rows;
}

// ── Fixture: the dictionary, loaded once ─────────────────────────────────────

using Intent = std::vector<std::pair<std::string, std::string>>;

class Tail095Fixture : public ::testing::Test {
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
        std::string const path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX50SP2.xml";
        if (pugi::xml_parse_result const r = shared_->doc.load_file(path.c_str()); !r) {
            shared_->load_error = "pugixml could not load " + path + ": " + r.description();
            return;
        }
        shared_->walk = Walker{shared_->doc}.walk_all();
        fixpp::dict::XmlLoader loader;
        shared_->dict.emplace(loader.load(path, &shared_->dict_arena));
        shared_->tv.emplace(shared_->dict->as_table_view());
    }

    void SetUp() override { ASSERT_TRUE(shared_->load_error.empty()) << shared_->load_error; }

    static pugi::xml_document const& doc() { return shared_->doc; }
    static WalkMap const& walk() { return shared_->walk; }
    static fixpp::dict::table_view const& tv() { return *shared_->tv; }

    // W3–W5 for one witness: frame, independent parse, tag 35, both-direction readback,
    // presence check.
    void check_witness(std::string const& body, Intent const& intent) {
        ASSERT_GE(intent.size(), 2U) << "W4 intent-size: the intent list holds " << intent.size()
                                     << " entries; a witness reads back at least two";
        std::vector<std::byte> const frame = make_frame("FIX.5.0SP2", body);
        ::testing::TestResult const* const result =
            ::testing::UnitTest::GetInstance()->current_test_info()->result();
        int const parts_before = result->total_part_count();
        auto const mv = parse_dict(frame, tv(), &read_arena_);
        ASSERT_EQ(result->total_part_count(), parts_before)
            << "W3: the parser refused the witness frame (a builder/parser disagreement, FR-020)";

        std::string const test_name =
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::string const expected_mt =
            msgtype_for_generated_name(doc(), split_test_name(test_name).second);
        EXPECT_EQ(std::string{mv.msg_type()}, expected_mt)
            << "W1 tag 35: the frame carries 35=" << mv.msg_type() << ", the test name "
            << test_name << " maps to 35=" << expected_mt;

        auto const it = walk().find(mv.msg_type());
        ASSERT_NE(it, walk().end()) << "35=" << mv.msg_type() << " is not in the walk";
        WalkResult const& wr = it->second;
        ASSERT_STREQ(to_string(wr.status), "ok")
            << "walk status at " << wr.element << " for 35=" << mv.msg_type();
        for (std::uint16_t f : wr.fields) {
            EXPECT_FALSE(is_framing_or_msgtype(f))
                << "W3: closure-required tag " << f << " is one the framing writes";
        }

        // W4: the intent list against the parsed body, as a multiset, both directions.
        BodyEnumeration const parsed = enumerate_body(mv, wr);
        std::vector<bool> used(parsed.entries.size(), false);
        for (auto const& [path, literal] : intent) {
            if (is_framing_or_msgtype(tag_of_path(path))) continue;
            std::optional<std::size_t> same_path;
            bool matched = false;
            for (std::size_t i = 0; i < parsed.entries.size(); ++i) {
                if (used[i] || parsed.entries[i].path != path) continue;
                if (parsed.entries[i].value == literal) {
                    used[i] = true;
                    matched = true;
                    break;
                }
                if (!same_path) same_path = i;
            }
            if (matched) continue;
            if (same_path) {
                used[*same_path] = true;
                ADD_FAILURE() << "W4 value mismatch at " << path << ": intended '" << literal
                              << "', parsed '" << parsed.entries[*same_path].value << "'";
            } else {
                ADD_FAILURE() << "W4 intended, not parsed: " << path << "=" << literal;
            }
        }
        for (std::size_t i = 0; i < parsed.entries.size(); ++i) {
            if (!used[i]) {
                ADD_FAILURE() << "W4 parsed, not intended: " << parsed.entries[i].path << "="
                              << parsed.entries[i].value;
            }
        }

        // W5: the dictionary-derived presence check, on the parsed frame.
        std::vector<std::string> const missing = required_members_missing(walk(), mv);
        EXPECT_TRUE(missing.empty()) << "W5 required members missing: " << join(missing);
    }

    std::array<std::byte, 131072> out_{};
    std::pmr::monotonic_buffer_resource read_arena_{65536};
    std::pmr::monotonic_buffer_resource arena_{4096};

    static std::unique_ptr<Shared> shared_;
};

std::unique_ptr<Tail095Fixture::Shared> Tail095Fixture::shared_;

class TailRows095 : public Tail095Fixture {};
class TailRows095Census : public Tail095Fixture {};

}  // namespace

// ── Known positives ───────────────────────────────────────────────

TEST_F(TailRows095Census, FieldLegMatchesQuickFixCppGolden) {
    std::vector<std::string> asserted;
    for (TailRow const& r : tail_rows()) {
        for (char const* mt : r.msgtypes) asserted.emplace_back(mt);
    }
    asserted.emplace_back("E");

    std::ifstream in{FIXPP_095_QUICKFIX_GOLDEN_CSV};
    ASSERT_TRUE(in) << "cannot open " << FIXPP_095_QUICKFIX_GOLDEN_CSV;
    std::map<std::string, std::vector<std::set<std::uint16_t>>> rows;
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("FIX50SP2,", 0) != 0) continue;
        std::size_t const c2 = line.find(',', 9);
        if (c2 == std::string::npos) continue;
        std::string const mt = line.substr(9, c2 - 9);
        std::string tags = line.substr(c2 + 1);
        tags.erase(std::remove(tags.begin(), tags.end(), '"'), tags.end());
        std::set<std::uint16_t> set;
        std::istringstream ts{tags};
        for (unsigned t = 0; ts >> t;) set.insert(static_cast<std::uint16_t>(t));
        rows[mt].push_back(std::move(set));
    }
    bool rows_ok = true;
    for (std::string const& mt : asserted) {
        std::size_t const n = rows.count(mt) ? rows[mt].size() : 0;
        if (n != 1) {
            rows_ok = false;
            ADD_FAILURE() << "golden.csv holds " << n << " rows FIX50SP2," << mt
                          << ", (exactly one required)";
        }
    }
    ASSERT_TRUE(rows_ok);
    for (std::string const& mt : asserted) {
        auto const it = walk().find(mt);
        ASSERT_NE(it, walk().end()) << "35=" << mt << " is not in the walk";
        std::set<std::uint16_t> const walked(it->second.fields.begin(), it->second.fields.end());
        std::set<std::uint16_t> const& golden = rows[mt].front();
        EXPECT_EQ(walked, golden) << "35=" << mt << ": walk " << join_tags(walked)
                                  << ", golden.csv " << join_tags(golden);
    }
}

TEST_F(TailRows095Census, KnownPositive_DeficientFrames) {
    ASSERT_STREQ(to_string(walk().at("E").status), "ok") << "35=E at " << walk().at("E").element;
    ASSERT_STREQ(to_string(walk().at("CC").status), "ok") << "35=CC at " << walk().at("CC").element;

    struct Frame {
        char const* name;
        std::string body;
        std::vector<std::string> expected;
    };
    // Each expected list is a dictionary fact, written from the file, not from the walk.
    // Re-derive it with the header's sed recipe over NewOrderList -> ListOrdGrp and
    // StreamAssignmentRequest -> StrmAsgnReqGrp -> Parties, then data-model §B's leg order.
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
        {"E-absent", e_head, {"73"}},
        {"E-one",
         e_head + "73=1\x01"
                  "11=O0\x01"
                  "67=1\x01"
                  "54=1\x01",
         {"73"}},
        {"E-short",
         e_head + "73=2\x01"
                  "11=O0\x01"
                  "67=1\x01"
                  "54=1\x01",
         {"73"}},
        {"E-member",
         e_head + "73=2\x01"
                  "11=O0\x01"
                  "67=1\x01"
                  "54=1\x01"
                  "11=O1\x01"
                  "54=1\x01",
         {"73[1].67"}},
        {"E-nodelim",
         e_head + "73=2\x01"
                  "67=1\x01"
                  "54=1\x01"
                  "11=O1\x01"
                  "67=2\x01"
                  "54=1\x01",
         {"73[0].11"}},
        {"CC-nodelim",
         cc_head + "1499=2\x01"
                   "146=1\x01"
                   "55=SYM\x01"
                   "453=1\x01"
                   "448=P1\x01",
         {"1499[0].453"}},
        {"CC-nested",
         cc_head + "1499=2\x01"
                   "453=1\x01"
                   "447=D\x01"
                   "453=1\x01"
                   "448=P1\x01",
         {"1499[0].448"}},
        {"CC-count2",
         cc_head + "1499=2\x01"
                   "453=2\x01"
                   "448=PA\x01"
                   "448=PB\x01"
                   "453=1\x01"
                   "448=PC\x01",
         {"1499[0].453"}},
    };
    for (Frame const& f : frames) {
        SCOPED_TRACE(f.name);
        std::pmr::monotonic_buffer_resource arena{65536};
        std::vector<std::byte> const frame = make_frame("FIX.5.0SP2", f.body);
        ::testing::TestResult const* const result =
            ::testing::UnitTest::GetInstance()->current_test_info()->result();
        int const parts_before = result->total_part_count();
        auto const mv = parse_dict(frame, tv(), &arena);
        ASSERT_EQ(result->total_part_count(), parts_before)
            << f.name << ": the parser refused the frame";
        EXPECT_EQ(required_members_missing(walk(), mv), f.expected) << f.name;
    }
}

TEST_F(TailRows095Census, NestedRequiredGroupFailsLoud_MassQuote) {
    auto const it = walk().find("i");
    ASSERT_NE(it, walk().end());
    EXPECT_STREQ(to_string(it->second.status), "nested_required_group") << "35=i";
    EXPECT_EQ(it->second.element, "NoQuoteEntries") << "35=i";
}

TEST_F(TailRows095Census, WalkStatusesOnSyntheticDictionaries) {
    // S1: a required component holding one field and one group.
    char const* const s1 =
        "<fix type='FIX' major='5' minor='0' servicepack='2'><header/><trailer/>"
        "<messages><message name='SynthOne' msgtype='S1' msgcat='app'>"
        "<component name='FieldAndGroup' required='Y'/></message></messages>"
        "<components><component name='FieldAndGroup'>"
        "<field name='SynthField' required='Y'/>"
        "<group name='NoSynthEntries' required='N'><field name='SynthEntry' required='Y'/></group>"
        "</component></components>"
        "<fields><field number='9001' name='SynthField' type='STRING'/>"
        "<field number='9002' name='NoSynthEntries' type='NUMINGROUP'/>"
        "<field number='9003' name='SynthEntry' type='STRING'/></fields></fix>";
    // S2: a required group whose first emitted element is a group whose own first emitted
    // element is again a group.
    char const* const s2 =
        "<fix type='FIX' major='5' minor='0' servicepack='2'><header/><trailer/>"
        "<messages><message name='SynthTwo' msgtype='S2' msgcat='app'>"
        "<group name='NoOuter' required='Y'>"
        "<group name='NoMiddle' required='N'>"
        "<group name='NoInner' required='N'><field name='InnerField' required='N'/></group>"
        "</group></group></message></messages><components/>"
        "<fields><field number='9101' name='NoOuter' type='NUMINGROUP'/>"
        "<field number='9102' name='NoMiddle' type='NUMINGROUP'/>"
        "<field number='9103' name='NoInner' type='NUMINGROUP'/>"
        "<field number='9104' name='InnerField' type='STRING'/></fields></fix>";

    pugi::xml_document d1;
    ASSERT_TRUE(d1.load_string(s1));
    WalkMap const w1 = Walker{d1}.walk_all();
    ASSERT_EQ(w1.count("S1"), 1U);
    EXPECT_STREQ(to_string(w1.at("S1").status), "group_among_siblings") << "S1";
    EXPECT_EQ(w1.at("S1").element, "FieldAndGroup") << "S1";

    pugi::xml_document d2;
    ASSERT_TRUE(d2.load_string(s2));
    WalkMap const w2 = Walker{d2}.walk_all();
    ASSERT_EQ(w2.count("S2"), 1U);
    EXPECT_STREQ(to_string(w2.at("S2").status), "delimiter_chain") << "S2";
    EXPECT_EQ(w2.at("S2").element, "NoInner") << "S2";
}
