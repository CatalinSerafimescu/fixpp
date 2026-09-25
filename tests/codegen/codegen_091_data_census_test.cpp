// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/codegen_091_data_census_test.cpp
//
// 091-data-field-bytes T028 [US1] -- the exact completeness census of the
// generated builders' coupled Length+Data call sites
// (specs/091-data-field-bytes/contracts/codegen-builders.md C-2.2 and C-2.3;
// spec FR-010, FR-011a, FR-012, SC-003).
//
// EXPECTED side. A pugixml walk over the dictionary SOURCES
// (dictionaries/FIX42.xml, FIX44.xml, FIX50SP2.xml and
// dictionaries/orchestra/OrchestraFIXLatest.xml). It shares no code with
// tools/codegen/fixpp-codegen (not ir.cpp, not resolve_level), and pair-ness
// comes from core::detail::standard_length_data_pairs, never from
// FieldRef::length_pair_data_tag, so the census cannot inherit the emitter's
// or the loader's coupling decision. The tuple is
// (message, structural path, Length tag, Data tag, arm), recorded for every
// standard row whose two tags both occur at one level. A level is one
// generated Args struct: the message's top level or one group entry.
// Component references expand in place; each group starts a new level whose
// path is the enclosing chain of NumInGroup tags.
//
// Message population: the builder-bearing application messages, by the same
// XML rule the sibling census uses (tests/codegen/builder_completeness_common.hpp:
// legacy msgcat=="app"; Orchestra category!="Session"), minus the v44-only
// session-FSM-dispatch set of 069 (N-002/N-003), spelled out below rather than
// read from the emitter. Orchestra message structures reference the
// StandardHeader and StandardTrailer components; builders emit a body only, so
// the walk never expands those two components.
//
// ACTUAL side. A text scan of each message's generated `.builder.cpp`: every
// `field_data(D, ...)` (top arm) and `set_data(D, ...)` (nested arm) call,
// with the enclosing group chain (tracked through `group_begin(N, ...)` /
// `group_end(...)`) and the Length tag of the `static_assert(...
// length_tag_for_data(D) == L)` that must directly precede it. The arm comes
// from the call name, not from the path depth, so a call in the wrong arm is
// reported. The `.builder.inl` is scanned the same way and must carry the
// identical call-site multiset.
//
// Checks, per version: actual == expected; no call names a Data tag outside
// the expected set; no standard-table tag is passed to any other `field(` /
// `set_<kind>(` call (the orphan-half check, keyed on the call name and the
// tag); and the set of messages whose Args carry `message_encoding` equals the
// set the FR-011a rule selects on this walk (any depth holds the Data half of
// a standard pair whose field name CONTAINS "Encoded"), with the member last
// in the Args struct and its emit the first body statement. No count is
// pinned; every expected set is recomputed from the sources on each run.
//
// Usage: codegen_091_data_census_test [gtest flags] [<generated-root>]
//          [--census-dump]
//   <generated-root> is the directory holding fixpp/<ver>/messages (default:
//   the build's own codegen output, FIXPP_CODEGEN_OUT). Point it at a
//   mutant's output to run the census over that output.
//   --census-dump prints, per version, every expected tuple, the
//   message_encoding selection, the Encoded-bearing fields that do not BEGIN
//   with "Encoded", the messages whose encoded fields are only those, and the
//   messages with no Encoded field. T029-T032 name their message and group
//   from this listing.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/length_data_pairs.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_OUT
#error "FIXPP_CODEGEN_OUT must be set by CMake target_compile_definitions"
#endif

namespace {

namespace fs = std::filesystem;

std::string g_gen_root = FIXPP_CODEGEN_OUT;  // NOLINT(cert-err58-cpp)
bool g_dump = false;

// (message, structural path, Length tag, Data tag, arm)
using Site = std::tuple<std::string, std::string, int, int, std::string>;
using SiteBag = std::map<Site, int>;

struct StandardTable {
    std::map<int, int> length_to_data;
    std::map<int, int> data_to_length;
    std::set<int> all_tags;
};

StandardTable const& standard() {
    static StandardTable const t = [] {
        StandardTable s;
        for (auto const& row : fixpp::core::detail::standard_length_data_pairs) {
            s.length_to_data.emplace(row.length_tag, row.data_tag);
            s.data_to_length.emplace(row.data_tag, row.length_tag);
            s.all_tags.insert(row.length_tag);
            s.all_tags.insert(row.data_tag);
        }
        return s;
    }();
    return t;
}

std::string join_path(std::vector<int> const& stack) {
    if (stack.empty()) return "-";
    std::string s;
    for (std::size_t i = 0; i < stack.size(); ++i) {
        if (i) s += '.';
        s += std::to_string(stack[i]);
    }
    return s;
}

// ── Expected side: dictionary-source walk ────────────────────────────────

struct MessageFacts {
    std::set<int> tags_any_depth;  // every field and NumInGroup tag at any depth
};

struct ExpectedVersion {
    SiteBag sites;
    std::map<std::string, MessageFacts> messages;  // population only
    std::map<int, std::string> field_name;         // tag -> dictionary field name
};

// Records the standard pairs whose two halves both occur among `tags` (one
// level). A standard tag seen twice at one level has no single Args member to
// map to, so the walk refuses it rather than silently deduplicating.
void emit_level(std::string const& msg, std::vector<int> const& path, std::vector<int> const& tags,
                ExpectedVersion& ev) {
    std::map<int, int> count;
    for (int t : tags) ++count[t];
    for (auto const& [tag, n] : count) {
        if (n > 1 && standard().all_tags.contains(tag)) {
            throw std::runtime_error("standard pair tag " + std::to_string(tag) +
                                     " occurs twice at one level: msg=" + msg +
                                     " path=" + join_path(path));
        }
    }
    for (auto const& [len, data] : standard().length_to_data) {
        if (count.contains(len) && count.contains(data)) {
            ++ev.sites[Site{msg, join_path(path), len, data, path.empty() ? "top" : "nested"}];
        }
    }
}

// ---- legacy <fix> schema ----

struct LegacyDict {
    std::map<std::string, int> field_number;
    std::map<std::string, pugi::xml_node> components;
};

void walk_legacy(LegacyDict const& d, pugi::xml_node container, std::vector<int>& path,
                 std::vector<int>& level_tags, std::string const& msg, MessageFacts& facts,
                 ExpectedVersion& ev, int depth) {
    if (depth > 40) throw std::runtime_error("legacy walk too deep (cycle?) in " + msg);
    for (pugi::xml_node ch : container.children()) {
        std::string_view const kind = ch.name();
        std::string const name = ch.attribute("name").as_string();
        if (kind == "field") {
            auto it = d.field_number.find(name);
            if (it == d.field_number.end()) throw std::runtime_error("unknown field " + name);
            level_tags.push_back(it->second);
            facts.tags_any_depth.insert(it->second);
        } else if (kind == "component") {
            auto it = d.components.find(name);
            if (it == d.components.end()) throw std::runtime_error("unknown component " + name);
            walk_legacy(d, it->second, path, level_tags, msg, facts, ev, depth + 1);
        } else if (kind == "group") {
            auto it = d.field_number.find(name);
            if (it == d.field_number.end()) throw std::runtime_error("unknown group field " + name);
            level_tags.push_back(it->second);
            facts.tags_any_depth.insert(it->second);
            path.push_back(it->second);
            std::vector<int> inner;
            walk_legacy(d, ch, path, inner, msg, facts, ev, depth + 1);
            emit_level(msg, path, inner, ev);
            path.pop_back();
        }
    }
}

ExpectedVersion expected_legacy(std::string const& xml_file, std::set<std::string> const& exclude) {
    std::string const path = std::string(FIXPP_DICT_DATA_DIR) + "/" + xml_file;
    pugi::xml_document doc;
    if (auto r = doc.load_file(path.c_str()); !r) {
        throw std::runtime_error("cannot load " + path + ": " + r.description());
    }
    pugi::xml_node const root = doc.child("fix");
    if (!root) throw std::runtime_error("no <fix> root in " + path);

    LegacyDict d;
    ExpectedVersion ev;
    for (pugi::xml_node f : root.child("fields").children("field")) {
        int const number = f.attribute("number").as_int();
        d.field_number.emplace(f.attribute("name").as_string(), number);
        ev.field_name.emplace(number, f.attribute("name").as_string());
    }
    for (pugi::xml_node c : root.child("components").children("component")) {
        d.components.emplace(c.attribute("name").as_string(), c);
    }
    for (pugi::xml_node m : root.child("messages").children("message")) {
        std::string const msgcat = m.attribute("msgcat").as_string();
        std::string const msg_type = m.attribute("msgtype").as_string();
        if (msgcat != "app" && msgcat != "admin") {
            throw std::runtime_error(path + ": message " + msg_type + " has no app/admin msgcat");
        }
        if (msgcat != "app" || exclude.contains(msg_type)) continue;
        std::string const name = m.attribute("name").as_string();
        MessageFacts facts;
        std::vector<int> stack;
        std::vector<int> top;
        walk_legacy(d, m, stack, top, name, facts, ev, 0);
        emit_level(name, stack, top, ev);
        ev.messages.emplace(name, std::move(facts));
    }
    return ev;
}

// ---- Orchestra (FIX Latest) ----

struct OrchestraDict {
    std::map<int, pugi::xml_node> components;
    std::map<int, std::string> component_name;
    std::map<int, pugi::xml_node> groups;
};

void walk_orchestra(OrchestraDict const& d, pugi::xml_node container, std::vector<int>& path,
                    std::vector<int>& level_tags, std::string const& msg, MessageFacts& facts,
                    ExpectedVersion& ev, int depth) {
    if (depth > 40) throw std::runtime_error("orchestra walk too deep (cycle?) in " + msg);
    for (pugi::xml_node ch : container.children()) {
        std::string_view const kind = ch.name();
        int const id = ch.attribute("id").as_int();
        if (kind == "fixr:fieldRef") {
            level_tags.push_back(id);
            facts.tags_any_depth.insert(id);
        } else if (kind == "fixr:componentRef") {
            auto it = d.components.find(id);
            if (it == d.components.end()) {
                throw std::runtime_error("unknown componentRef " + std::to_string(id));
            }
            std::string const& cname = d.component_name.at(id);
            if (cname == "StandardHeader" || cname == "StandardTrailer") continue;
            walk_orchestra(d, it->second, path, level_tags, msg, facts, ev, depth + 1);
        } else if (kind == "fixr:groupRef") {
            auto it = d.groups.find(id);
            if (it == d.groups.end()) {
                throw std::runtime_error("unknown groupRef " + std::to_string(id));
            }
            int const counter = it->second.child("fixr:numInGroup").attribute("id").as_int();
            if (counter == 0) throw std::runtime_error("group without numInGroup in " + msg);
            level_tags.push_back(counter);
            facts.tags_any_depth.insert(counter);
            path.push_back(counter);
            std::vector<int> inner;
            walk_orchestra(d, it->second, path, inner, msg, facts, ev, depth + 1);
            emit_level(msg, path, inner, ev);
            path.pop_back();
        }
    }
}

ExpectedVersion expected_orchestra() {
    std::string const path = std::string(FIXPP_DICT_DATA_DIR) + "/orchestra/OrchestraFIXLatest.xml";
    pugi::xml_document doc;
    if (auto r = doc.load_file(path.c_str()); !r) {
        throw std::runtime_error("cannot load " + path + ": " + r.description());
    }
    pugi::xml_node const repo = doc.child("fixr:repository");
    if (!repo) throw std::runtime_error("no fixr:repository root in " + path);

    OrchestraDict d;
    ExpectedVersion ev;
    for (pugi::xml_node f : repo.child("fixr:fields").children("fixr:field")) {
        ev.field_name.emplace(f.attribute("id").as_int(), f.attribute("name").as_string());
    }
    for (pugi::xml_node c : repo.child("fixr:components").children("fixr:component")) {
        int const id = c.attribute("id").as_int();
        d.components.emplace(id, c);
        d.component_name.emplace(id, c.attribute("name").as_string());
    }
    for (pugi::xml_node g : repo.child("fixr:groups").children("fixr:group")) {
        d.groups.emplace(g.attribute("id").as_int(), g);
    }
    for (pugi::xml_node m : repo.child("fixr:messages").children("fixr:message")) {
        std::string const category = m.attribute("category").as_string();
        if (category.empty()) {
            throw std::runtime_error(path + ": message without a category attribute");
        }
        if (category == "Session") continue;
        std::string const name = m.attribute("name").as_string();
        MessageFacts facts;
        std::vector<int> stack;
        std::vector<int> top;
        walk_orchestra(d, m.child("fixr:structure"), stack, top, name, facts, ev, 0);
        emit_level(name, stack, top, ev);
        ev.messages.emplace(name, std::move(facts));
    }
    return ev;
}

// The FR-011a rule on this walk: Data halves of the standard table whose
// dictionary field name contains "Encoded".
std::set<int> encoded_data_tags(ExpectedVersion const& ev) {
    std::set<int> out;
    for (auto const& [data, len] : standard().data_to_length) {
        auto it = ev.field_name.find(data);
        if (it != ev.field_name.end() && it->second.find("Encoded") != std::string::npos) {
            out.insert(data);
        }
    }
    return out;
}

std::set<std::string> message_encoding_selection(ExpectedVersion const& ev) {
    std::set<int> const enc = encoded_data_tags(ev);
    std::set<std::string> out;
    for (auto const& [msg, facts] : ev.messages) {
        for (int t : facts.tags_any_depth) {
            if (enc.contains(t)) {
                out.insert(msg);
                break;
            }
        }
    }
    return out;
}

// ── Actual side: generated-source scan ───────────────────────────────────

// One `.<name>(<int>` occurrence on a line; `first_int` is -1 when the first
// argument is not an integer literal.
struct Call {
    std::string name;
    int first_int;
    std::size_t close_paren;  // index just past the integer literal, or npos
};

bool is_ident(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

std::vector<Call> scan_calls(std::string_view line) {
    std::vector<Call> out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '.') continue;
        std::size_t j = i + 1;
        while (j < line.size() && is_ident(line[j])) ++j;
        if (j == i + 1 || j >= line.size() || line[j] != '(') continue;
        Call c{std::string(line.substr(i + 1, j - i - 1)), -1, std::string_view::npos};
        std::size_t k = j + 1;
        while (k < line.size() && line[k] == ' ') ++k;
        std::size_t const digits = k;
        while (k < line.size() && line[k] >= '0' && line[k] <= '9') ++k;
        if (k > digits) {
            c.first_int = std::stoi(std::string(line.substr(digits, k - digits)));
            c.close_paren = k;
        }
        out.push_back(std::move(c));
    }
    return out;
}

struct ScanResult {
    SiteBag sites;
    std::vector<std::string> orphans;    // "msg: call(tag)"
    std::vector<std::string> anomalies;  // parse-shape problems
};

void scan_builder_file(fs::path const& file, std::string const& msg, ScanResult& out) {
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot open " + file.string());
    std::vector<int> stack;
    struct Pending {
        int data;
        int length;
    };
    std::optional<Pending> pending;
    std::string line;
    std::size_t lineno = 0;
    auto where = [&] {
        return msg + " (" + file.filename().string() + ":" + std::to_string(lineno) + ")";
    };
    while (std::getline(in, line)) {
        ++lineno;
        bool const is_static_assert = line.find("static_assert(") != std::string::npos;
        for (Call const& c : scan_calls(line)) {
            if (c.name == "group_begin") {
                if (c.first_int < 0)
                    out.anomalies.push_back("group_begin without a literal tag: " + where());
                stack.push_back(c.first_int);
            } else if (c.name == "group_end") {
                if (stack.empty()) {
                    out.anomalies.push_back("group_end with no open group: " + where());
                } else {
                    stack.pop_back();
                }
            } else if (c.name == "length_tag_for_data" && is_static_assert) {
                std::size_t const eq =
                    line.find("==", c.close_paren == std::string::npos ? 0 : c.close_paren);
                int length = -1;
                if (eq != std::string::npos) {
                    std::size_t k = eq + 2;
                    while (k < line.size() && line[k] == ' ') ++k;
                    std::size_t const d0 = k;
                    while (k < line.size() && line[k] >= '0' && line[k] <= '9') ++k;
                    if (k > d0) length = std::stoi(line.substr(d0, k - d0));
                }
                if (pending)
                    out.anomalies.push_back("static_assert not followed by its call: " + where());
                pending = Pending{c.first_int, length};
            } else if (c.name == "field_data" || c.name == "set_data") {
                int length = 0;
                if (!pending) {
                    out.anomalies.push_back("call with no preceding static_assert: " + where());
                } else if (pending->data != c.first_int) {
                    out.anomalies.push_back("static_assert names Data " +
                                            std::to_string(pending->data) + " but the call names " +
                                            std::to_string(c.first_int) + ": " + where());
                } else {
                    length = pending->length;
                }
                pending.reset();
                ++out.sites[Site{msg, join_path(stack), length, c.first_int,
                                 c.name == "field_data" ? "top" : "nested"}];
            } else if ((c.name == "field" || c.name.starts_with("set_")) &&
                       standard().all_tags.contains(c.first_int)) {
                out.orphans.push_back(msg + ": " + c.name + "(" + std::to_string(c.first_int) +
                                      ", ...)");
            }
        }
    }
    if (pending) out.anomalies.push_back("trailing static_assert with no call: " + msg);
    if (!stack.empty()) out.anomalies.push_back("unbalanced group_begin/group_end: " + msg);
}

fs::path messages_dir(std::string const& ver) {
    return fs::path(g_gen_root) / "fixpp" / ver / "messages";
}

std::vector<std::string> builder_messages(std::string const& ver) {
    fs::path const dir = messages_dir(ver);
    if (!fs::is_directory(dir)) throw std::runtime_error("no generated directory " + dir.string());
    std::vector<std::string> out;
    constexpr std::string_view kSuffix = ".builder.cpp";
    for (auto const& e : fs::directory_iterator(dir)) {
        std::string const fn = e.path().filename().string();
        if (fn.size() > kSuffix.size() && fn.ends_with(kSuffix)) {
            out.push_back(fn.substr(0, fn.size() - kSuffix.size()));
        }
    }
    std::ranges::sort(out);
    if (out.empty()) throw std::runtime_error("no *.builder.cpp under " + dir.string());
    return out;
}

// ── Args / body shape for message_encoding ───────────────────────────────

struct ArgsShape {
    bool has_member = false;
    bool member_is_last = false;
};

ArgsShape read_args_shape(std::string const& ver, std::string const& msg) {
    fs::path const file = messages_dir(ver) / (msg + ".hpp");
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot open " + file.string());
    std::string const open = "struct " + msg + "Args {";
    std::string line;
    bool inside = false;
    std::string last_member;
    ArgsShape s;
    while (std::getline(in, line)) {
        if (!inside) {
            inside = line.find(open) != std::string::npos;
            continue;
        }
        if (line.starts_with("};")) break;
        // A member line ends in `name{};` or `name;`.
        std::string_view v = line;
        while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) v.remove_suffix(1);
        if (!v.ends_with(";")) continue;
        v.remove_suffix(1);
        if (v.ends_with("{}")) v.remove_suffix(2);
        std::size_t b = v.size();
        while (b > 0 && is_ident(v[b - 1])) --b;
        std::string const name{v.substr(b)};
        if (name.empty()) continue;
        if (name == "message_encoding") s.has_member = true;
        last_member = name;
    }
    if (!inside) throw std::runtime_error("no `" + open + "` in " + file.string());
    s.member_is_last = (last_member == "message_encoding");
    return s;
}

// True when the first statement after the `body_builder` declaration is the
// `message_encoding` guard and its body emits `field(347, *args.message_encoding)`.
bool message_encoding_emitted_first(std::string const& ver, std::string const& msg) {
    fs::path const file = messages_dir(ver) / (msg + ".builder.cpp");
    std::ifstream in(file);
    std::string line;
    bool after_decl = false;
    int step = 0;
    while (std::getline(in, line)) {
        if (!after_decl) {
            after_decl = line.find("::fixpp::wire::body_builder bb") != std::string::npos;
            continue;
        }
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        if (step == 0) {
            if (line.find("if (args.message_encoding)") == std::string::npos) return false;
            step = 1;
            continue;
        }
        for (Call const& c : scan_calls(line)) {
            if (c.name == "field" && c.first_int == 347 &&
                line.find("*args.message_encoding") != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    return false;
}

// ── Per-version census, computed once ────────────────────────────────────

struct VersionSpec {
    std::string ver;
    std::string xml;  // empty => Orchestra
    std::set<std::string> exclude;
};

// gtest prints a parameter through this instead of its raw bytes.
void PrintTo(VersionSpec const& v, std::ostream* os) { *os << v.ver; }

struct Census {
    ExpectedVersion expected;
    ScanResult cpp;
    ScanResult inl;
    std::vector<std::string> generated;
};

Census const& census_for(VersionSpec const& spec) {
    static std::map<std::string, Census> cache;
    if (auto it = cache.find(spec.ver); it != cache.end()) return it->second;
    Census c;
    c.expected = spec.xml.empty() ? expected_orchestra() : expected_legacy(spec.xml, spec.exclude);
    c.generated = builder_messages(spec.ver);
    for (std::string const& msg : c.generated) {
        scan_builder_file(messages_dir(spec.ver) / (msg + ".builder.cpp"), msg, c.cpp);
        scan_builder_file(messages_dir(spec.ver) / (msg + ".builder.inl"), msg, c.inl);
    }
    return cache.emplace(spec.ver, std::move(c)).first->second;
}

std::string describe(SiteBag const& bag, char const* label) {
    std::ostringstream os;
    std::size_t total = 0;
    for (auto const& [s, n] : bag) total += static_cast<std::size_t>(n);
    os << total << " site(s) " << label << ":\n";
    std::size_t shown = 0;
    for (auto const& [s, n] : bag) {
        if (shown++ == 25) {
            os << "  ... (rerun with --census-dump for the full expected listing)\n";
            break;
        }
        auto const& [msg, path, len, data, arm] = s;
        os << "  msg=" << msg << " path=" << path << " L=" << len << " D=" << data << " arm=" << arm
           << (n > 1 ? " x" + std::to_string(n) : "") << "\n";
    }
    return os.str();
}

// a - b as bags
SiteBag bag_minus(SiteBag const& a, SiteBag const& b) {
    SiteBag out;
    for (auto const& [s, n] : a) {
        auto it = b.find(s);
        int const m = n - (it == b.end() ? 0 : it->second);
        if (m > 0) out.emplace(s, m);
    }
    return out;
}

std::string list(std::vector<std::string> const& v, std::size_t cap = 25) {
    std::ostringstream os;
    for (std::size_t i = 0; i < v.size() && i < cap; ++i) os << "  " << v[i] << "\n";
    if (v.size() > cap) os << "  ... (" << v.size() - cap << " more)\n";
    return os.str();
}

void dump(VersionSpec const& spec, Census const& c) {
    std::ostream& os = std::cout;
    std::size_t total = 0;
    for (auto const& [s, n] : c.expected.sites) total += static_cast<std::size_t>(n);
    os << "== census " << spec.ver << ": expected tuples=" << total
       << " population=" << c.expected.messages.size() << " generated=" << c.generated.size()
       << "\n";
    for (auto const& [s, n] : c.expected.sites) {
        auto const& [msg, path, len, data, arm] = s;
        os << "EXPECTED\t" << spec.ver << '\t' << msg << '\t' << path << '\t' << len << '\t' << data
           << '\t' << arm << '\n';
    }
    std::set<int> const enc = encoded_data_tags(c.expected);
    std::set<int> contains_not_begins;
    for (int t : enc) {
        std::string const& n = c.expected.field_name.at(t);
        os << "ENCODED_FIELD\t" << spec.ver << '\t' << t << '\t' << n << '\n';
        if (!n.starts_with("Encoded")) contains_not_begins.insert(t);
    }
    for (int t : contains_not_begins) {
        os << "ENCODED_NOT_PREFIX\t" << spec.ver << '\t' << t << '\t' << c.expected.field_name.at(t)
           << '\n';
    }
    for (std::string const& m : message_encoding_selection(c.expected)) {
        os << "MESSAGE_ENCODING\t" << spec.ver << '\t' << m << '\n';
    }
    for (auto const& [msg, facts] : c.expected.messages) {
        std::set<int> held;
        for (int t : facts.tags_any_depth) {
            if (enc.contains(t)) held.insert(t);
        }
        if (held.empty()) {
            os << "NO_ENCODED\t" << spec.ver << '\t' << msg << '\n';
        } else if (std::ranges::includes(contains_not_begins, held)) {
            os << "ONLY_NOT_PREFIX_ENCODED\t" << spec.ver << '\t' << msg << '\n';
        }
    }
}

class DataCensus091 : public ::testing::TestWithParam<VersionSpec> {};

// Anti-vacuity: the walk and the scan both see a non-empty world, so an
// equality below cannot pass because both sides came back empty.
TEST_P(DataCensus091, WalkAndScanAreNonEmpty) {
    Census const* c = nullptr;
    ASSERT_NO_THROW(c = &census_for(GetParam()));
    EXPECT_FALSE(c->expected.messages.empty()) << "dictionary walk found no builder message";
    EXPECT_FALSE(c->expected.sites.empty()) << "dictionary walk found no coupled pair";
    EXPECT_FALSE(c->generated.empty()) << "no generated builder source";
    EXPECT_FALSE(message_encoding_selection(c->expected).empty())
        << "FR-011a rule selected no message";
}

// C-2.2: actual == expected, per (message, path, L, D, arm).
TEST_P(DataCensus091, CoupledSitesEqualDictionaryDerivedSet) {
    Census const* c = nullptr;
    ASSERT_NO_THROW(c = &census_for(GetParam()));
    EXPECT_TRUE(c->cpp.anomalies.empty()) << ".builder.cpp scan anomalies:\n"
                                          << list(c->cpp.anomalies);
    SiteBag const missing = bag_minus(c->expected.sites, c->cpp.sites);
    SiteBag const extra = bag_minus(c->cpp.sites, c->expected.sites);
    EXPECT_TRUE(missing.empty()) << describe(missing,
                                             "expected but not emitted as field_data/set_data");
    EXPECT_TRUE(extra.empty()) << describe(extra,
                                           "emitted but not expected (or wrong arm / Length)");
}

// C-2.2 / FR-012: no field_data/set_data names a Data tag outside the
// expected set.
TEST_P(DataCensus091, NoCallOutsideExpectedDataTags) {
    Census const* c = nullptr;
    ASSERT_NO_THROW(c = &census_for(GetParam()));
    std::set<int> expected_data;
    for (auto const& [s, n] : c->expected.sites) expected_data.insert(std::get<3>(s));
    std::vector<std::string> bad;
    for (auto const& [s, n] : c->cpp.sites) {
        if (!expected_data.contains(std::get<3>(s))) {
            bad.push_back(std::get<0>(s) + " D=" + std::to_string(std::get<3>(s)));
        }
    }
    EXPECT_TRUE(bad.empty()) << "field_data/set_data on a tag outside the expected Data set:\n"
                             << list(bad);
}

// C-2.2: each message's .builder.cpp and .builder.inl carry the same
// call-site multiset.
TEST_P(DataCensus091, BuilderCppAndInlCallSitesIdentical) {
    Census const* c = nullptr;
    ASSERT_NO_THROW(c = &census_for(GetParam()));
    EXPECT_TRUE(c->inl.anomalies.empty()) << ".builder.inl scan anomalies:\n"
                                          << list(c->inl.anomalies);
    SiteBag const only_cpp = bag_minus(c->cpp.sites, c->inl.sites);
    SiteBag const only_inl = bag_minus(c->inl.sites, c->cpp.sites);
    EXPECT_TRUE(only_cpp.empty()) << describe(only_cpp, "in .builder.cpp but not in .builder.inl");
    EXPECT_TRUE(only_inl.empty()) << describe(only_inl, "in .builder.inl but not in .builder.cpp");
}

// C-2.2 orphan-half check (FR-008 proviso): no standard-table tag reaches any
// `field(` / `set_<kind>(` call other than field_data/set_data.
TEST_P(DataCensus091, NoOrphanStandardPairHalf) {
    Census const* c = nullptr;
    ASSERT_NO_THROW(c = &census_for(GetParam()));
    std::set<std::string> tags;
    for (std::string const& o : c->cpp.orphans) {
        auto const open = o.rfind('(');
        auto const comma = o.find(',', open);
        tags.insert(o.substr(open + 1, comma - open - 1));
    }
    std::ostringstream t;
    for (auto const& s : tags) t << s << ' ';
    EXPECT_TRUE(c->cpp.orphans.empty())
        << c->cpp.orphans.size()
        << " standard-pair half call(s) outside field_data/set_data; tags: " << t.str() << "\n"
        << list(c->cpp.orphans);
}

// C-2.3 / FR-011a: the messages whose Args carry message_encoding equal the
// rule's selection; the member is last and its emit is the first statement.
TEST_P(DataCensus091, MessageEncodingSetShapeAndPosition) {
    Census const* c = nullptr;
    ASSERT_NO_THROW(c = &census_for(GetParam()));
    std::set<std::string> const selected = message_encoding_selection(c->expected);
    std::set<std::string> carrying;
    std::vector<std::string> not_last;
    std::vector<std::string> not_first;
    for (std::string const& msg : c->generated) {
        ArgsShape const s = read_args_shape(GetParam().ver, msg);
        if (!s.has_member) continue;
        carrying.insert(msg);
        if (!s.member_is_last) not_last.push_back(msg);
        if (!message_encoding_emitted_first(GetParam().ver, msg)) not_first.push_back(msg);
    }
    std::vector<std::string> missing;
    std::vector<std::string> extra;
    std::ranges::set_difference(selected, carrying, std::back_inserter(missing));
    std::ranges::set_difference(carrying, selected, std::back_inserter(extra));
    EXPECT_TRUE(missing.empty()) << missing.size()
                                 << " message(s) the FR-011a rule selects lack message_encoding:\n"
                                 << list(missing);
    EXPECT_TRUE(extra.empty()) << extra.size()
                               << " message(s) carry message_encoding the rule does not select:\n"
                               << list(extra);
    EXPECT_TRUE(not_last.empty()) << "message_encoding is not the last Args member:\n"
                                  << list(not_last);
    EXPECT_TRUE(not_first.empty()) << "message_encoding emit is not the first body statement:\n"
                                   << list(not_first);
}

std::vector<VersionSpec> versions() {
    std::vector<VersionSpec> v{
        {"v42", "FIX42.xml", {}},
        // 069 N-002/N-003: the v44 builder set omits these application MsgTypes.
        {"v44", "FIX44.xml", {"BE", "BF", "BW", "BX", "BY"}},
        {"v50sp2", "FIX50SP2.xml", {}},
    };
#ifdef FIXPP_091_CENSUS_HAS_VLATEST
    v.push_back({"vlatest", "", {}});
#endif
    return v;
}

INSTANTIATE_TEST_SUITE_P(Versions, DataCensus091, ::testing::ValuesIn(versions()),
                         [](::testing::TestParamInfo<VersionSpec> const& i) {
                             return i.param.ver;
                         });

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    for (int i = 1; i < argc; ++i) {
        std::string_view const a = argv[i];
        if (a == "--census-dump") {
            g_dump = true;
        } else if (!a.starts_with("-")) {
            g_gen_root = std::string(a);
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            return 2;
        }
    }
    std::cout << "[census] generated root: " << g_gen_root << "\n";
    if (g_dump) {
        try {
            for (VersionSpec const& spec : versions()) dump(spec, census_for(spec));
        } catch (std::exception const& e) {
            std::cerr << "census dump failed: " << e.what() << "\n";
            return 2;
        }
    }
    return RUN_ALL_TESTS();
}
