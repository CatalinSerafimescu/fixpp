// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/accessor_resolution_census_test.cpp
//
// 094-v1-tier0-bookkeeping FR-022c — D-008's compile-time binding clause,
// witnessed over the SHIPPED generated text: each typed accessor binds its
// field's tag at compile time, with its value type fixed in its generated
// text.
//
// THE PROPERTY. The tag is an integer literal (the template argument of
// `view_.template get<N>()` / `group<N, G_N>()` in Messages.hpp and of
// `view().template get<N>()` / `group<N, G_N>()` in Reify.hpp's
// owning_<Msg> classes, or the literal argument of
// `::fixpp::wire::get(ctx_.span, N, ...)` and of `nested_group_slices(...,
// N, ...)`), and the value type is fixed by the emitted text (`decode_field<T>`
// with T equal to the accessor's `expected_t<T>`, `decimal_t::parse` for a
// decimal_t accessor, or the group flyweight type). The literal is the tag of
// the field the accessor is named for, and the value type is the one the
// codegen's data-type mapping assigns to that field's data type. Reading the
// field is runtime work of the callees (the offset-table find, the entry
// scan, group occurrence segmentation through `dict_hooks`); the cell reads
// the accessor's own text and makes no claim about that work.
//
// THE MECHANISM, three checks over each version's `Messages.hpp` and, the
// same three, over its `Reify.hpp` (`run_reify_census`; there each
// owning_<Msg> class is checked against its message's `Fields.hpp` rows, and
// the grammar also closes the out-of-line definitions and the traits):
//   1. A closed line grammar, not a forbidden-token list. Every line must be
//      one of the shapes below; every member function must match one of the
//      accessor templates in full (signature and body), or one of the two
//      named exclusions. Anything else — a new include, a namespace-scope
//      table, a member without `[[nodiscard]] inline`, a body that computes
//      its tag — is an unclassified line. A deliberate emitter change that
//      adds an accessor shape must be added here too, by hand.
//   2. Population: for every class, the set of tags its typed accessors bind
//      equals the set `Fields.hpp` declares for it (a message's
//      `<Msg>_fields[]` rows with group_no_tag 0; a group flyweight G_K's rows
//      with group_no_tag K across every message), less the rows whose data
//      type the mapping omits. `Fields.hpp` is written by another emitter
//      (emit_fields.cpp) from the same IR, so this proves the walk reached
//      every accessor the version declares.
//   3. Identity, per accessor:
//      - NAME. Its identifier is the accessor name of its tag's field name in
//        the dictionary the version is generated from (`versions()`), by the
//        naming rule restated below (`accessor_name`; a group accessor drops
//        a leading "No"). The base name followed by `_t<tag>` is accepted only
//        when another accessor of the class holds the base name (the
//        emitter's collision form); which of the two gets the base name is
//        not checked.
//      - TYPE. A tag that heads a group in `Fields.hpp` has a group accessor;
//        any other has the kind `mapped_kind` gives for its `Fields.hpp` data
//        type. That table restates the codegen's DESIGN mapping, not each
//        field's correct type: it carries the MultiCharValue -> char entry
//        that fixpp#621 reports as wrong, so a wrong mapping passes here.
//      The field names come from the dictionary loaders, which the codegen
//      also uses, so a loader name defect common to both would pass here;
//      DictionaryCensus.*AllMessagesAndFieldsLoaded in
//      tests/dictionary/lookup_test.cpp checks the loaders' names against an
//      independent walk of each source XML.
//
// EXCLUSIONS. `field_value(::std::uint16_t tag)` is the runtime-tag
// passthrough (its tag is the caller's), and `view()` returns the bound
// view; neither is a typed field accessor. Each is accepted only with its
// exact signature and body, and only once per class, so a second member of
// that shape cannot carry a lookup past the census. In Reify.hpp the other
// non-accessor members (the deleted copies, the move declarations, which(),
// from_view, the private constructor and data members) are accepted only by
// their exact text, and view() and the traits specialisation once per class.
//
// SCOPE. `Messages.hpp` and `Reify.hpp` of v42, v44, v50sp2 and vt11, and
// of vlatest when the build generates it (FIXPP_CODEGEN_FIX_LATEST, which
// defines FIXPP_D008_CENSUS_HAS_VLATEST). A listed version whose header is
// missing fails; it is never skipped.
//
// GUARD. Before any shipped header is read, the checks run over a synthetic
// text in the shipped grammar, one for Messages.hpp and one for Reify.hpp.
// Each must pass all three. Each seeded
// runtime-lookup shape must fail the grammar. Each seeded identity mutation
// (a tag swap that keeps the tag set, a consistent type change, a rename, a
// changed data type) must PASS the grammar and the population check and fail
// the identity check, so the identity check is shown to be what catches it.
//
// RE-DERIVE the population by hand: for one version,
//   grep -c '\[\[nodiscard\]\] inline' build/<preset>/_codegen/include/fixpp/<v>/Messages.hpp
// counts the member functions the census classifies: the typed accessors,
// one `field_value` per class and one `view()` per message class. The same
// grep over Reify.hpp counts the owning_ typed accessors plus one
// `field_value` per class (its view() declaration is not `inline`). Re-derive
// one accessor's identity: find its tag's FieldRef row in `Fields.hpp`
// (column 2 is the data type) and `<field number="<tag>" name=...>` in the
// version's dictionary XML.

#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <map>
#include <memory_resource>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef FIXPP_CODEGEN_INC
#error "FIXPP_CODEGEN_INC must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake target_compile_definitions"
#endif

namespace {

// ---------------------------------------------------------------------------
// A linear template matcher. Literal characters must match exactly; the
// placeholders consume a maximal run of their class:
//   @N  a positive decimal integer without a leading zero (captured)
//   @I  an identifier [A-Za-z_][A-Za-z0-9_]* (captured)
//   @S  one or more characters other than '"' (captured)
// Every placeholder in the templates below is followed by a literal that is
// not in its class, so maximal consumption is exact.
// ---------------------------------------------------------------------------
struct caps {
    std::vector<std::uint32_t> nums;
    std::vector<std::string> ids;
};

bool is_id_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool is_id_char(char c) { return is_id_start(c) || (c >= '0' && c <= '9'); }

bool match(std::string_view pat, std::string_view s, caps& out) {
    std::size_t p = 0;
    std::size_t i = 0;
    while (p < pat.size()) {
        if (pat[p] == '@' && p + 1 < pat.size()) {
            char const k = pat[p + 1];
            p += 2;
            std::size_t const start = i;
            if (k == 'N') {
                if (i >= s.size() || s[i] < '1' || s[i] > '9') return false;
                std::uint64_t v = 0;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                    v = v * 10U + static_cast<std::uint64_t>(s[i] - '0');
                    if (v > 0xFFFFFFFFULL) return false;
                    ++i;
                }
                out.nums.push_back(static_cast<std::uint32_t>(v));
            } else if (k == 'I') {
                if (i >= s.size() || !is_id_start(s[i])) return false;
                while (i < s.size() && is_id_char(s[i])) ++i;
                out.ids.emplace_back(s.substr(start, i - start));
            } else if (k == 'S') {
                while (i < s.size() && s[i] != '"') ++i;
                if (i == start) return false;
                out.ids.emplace_back(s.substr(start, i - start));
            } else {
                return false;
            }
            continue;
        }
        if (i >= s.size() || s[i] != pat[p]) return false;
        ++i;
        ++p;
    }
    return i == s.size();
}

bool match(std::string_view pat, std::string_view s) {
    caps c;
    return match(pat, s, c);
}

bool all_equal(std::vector<std::uint32_t> const& v) {
    for (auto x : v) {
        if (x != v.front()) return false;
    }
    return !v.empty();
}

std::string_view strip(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    for (std::size_t at = s.find(from); at != std::string::npos;
         at = s.find(from, at + to.size())) {
        s.replace(at, from.size(), to);
    }
    return s;
}

// The value kind of a typed accessor, as its emitted text fixes it.
enum class vkind { string_view, char_, bool_, int32, decimal, group, omitted, unknown };

std::string_view kind_name(vkind k) {
    switch (k) {
        case vkind::string_view:
            return "string_view";
        case vkind::char_:
            return "char";
        case vkind::bool_:
            return "bool";
        case vkind::int32:
            return "int32";
        case vkind::decimal:
            return "decimal";
        case vkind::group:
            return "group";
        case vkind::omitted:
            return "omitted";
        case vkind::unknown:
            return "unknown";
    }
    return "unknown";
}

// The value types decode_field<T> is emitted with (emit_messages.cpp,
// kind_cpp_type). A T outside this set is unclassified.
struct scalar_type {
    std::string_view cpp;
    vkind kind;
};
constexpr scalar_type kScalarTypes[] = {{"::std::string_view", vkind::string_view},
                                        {"char", vkind::char_},
                                        {"bool", vkind::bool_},
                                        {"::std::int32_t", vkind::int32}};

constexpr std::string_view kMsgView =
    "::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index>";

// The codegen's DESIGN mapping from a field's data type to the accessor
// kind (gen_util.hpp's kind_of, [2c §4.7]), restated here rather than
// included so an edit to kind_of is caught. It is the design, not each
// field's correct type: MultiCharValue -> char is the entry fixpp#621
// reports as wrong. A switch with no default, so a new data type fails the
// build (-Wswitch) until it is classified here.
vkind mapped_kind(std::uint32_t raw) {
    using D = fixpp::dict::field_data_type;
    if (raw > static_cast<std::uint32_t>(D::DialectExtension)) return vkind::unknown;
    switch (static_cast<D>(raw)) {
        case D::Price:
        case D::Qty:
        case D::Amt:
        case D::PriceOffset:
        case D::Percentage:
        case D::Float:
            return vkind::decimal;
        case D::Char:
        case D::MultiCharValue:
            return vkind::char_;
        case D::Boolean:
            return vkind::bool_;
        case D::Int:
        case D::Length:
        case D::SeqNum:
        case D::NumInGroup:
        case D::DayOfMonth:
            return vkind::int32;
        case D::String:
        case D::MultiStringValue:
        case D::Currency:
        case D::Exchange:
        case D::Country:
        case D::MonthYear:
        case D::UtcTimestamp:
        case D::UtcTimeOnly:
        case D::UtcDateOnly:
        case D::LocalMktDate:
        case D::TzTimeOnly:
        case D::TzTimestamp:
        case D::Language:
        case D::Data:
        case D::XmlData:
            return vkind::string_view;
        case D::DialectExtension:
            return vkind::omitted;
    }
    return vkind::unknown;
}

// ---------------------------------------------------------------------------
// The naming rule, restated from its documentation (gen_util.hpp: "ClOrdID
// -> cl_ord_id; SecurityIDSource -> security_id_source", C++-keyword
// guarded; a group accessor drops the "No" prefix, "NoPartyIDs ->
// party_i_ds"):
//   - a run of characters other than [A-Za-z0-9] becomes one '_';
//   - an uppercase letter starts a new word when the character before it is
//     lowercase or a digit, or when it is the last capital of an acronym
//     followed by a lowercase letter ("IDSource" -> "id_source");
//   - letters are lowercased; leading and trailing '_' are dropped;
//   - a leading digit gets a '_' prefix; a C++ keyword gets a '_' suffix.
// An empty result is reported, never defaulted.
// ---------------------------------------------------------------------------
constexpr std::string_view kCppKeywords[] = {"alignas",       "alignof",     "and",
                                             "and_eq",        "asm",         "auto",
                                             "bitand",        "bitor",       "bool",
                                             "break",         "case",        "catch",
                                             "char",          "char8_t",     "char16_t",
                                             "char32_t",      "class",       "compl",
                                             "concept",       "const",       "consteval",
                                             "constexpr",     "constinit",   "const_cast",
                                             "continue",      "co_await",    "co_return",
                                             "co_yield",      "decltype",    "default",
                                             "delete",        "do",          "double",
                                             "dynamic_cast",  "else",        "enum",
                                             "explicit",      "export",      "extern",
                                             "false",         "float",       "for",
                                             "friend",        "goto",        "if",
                                             "inline",        "int",         "long",
                                             "mutable",       "namespace",   "new",
                                             "noexcept",      "not",         "not_eq",
                                             "nullptr",       "operator",    "or",
                                             "or_eq",         "private",     "protected",
                                             "public",        "register",    "reinterpret_cast",
                                             "requires",      "return",      "short",
                                             "signed",        "sizeof",      "static",
                                             "static_assert", "static_cast", "struct",
                                             "switch",        "template",    "this",
                                             "thread_local",  "throw",       "true",
                                             "try",           "typedef",     "typeid",
                                             "typename",      "union",       "unsigned",
                                             "using",         "virtual",     "void",
                                             "volatile",      "wchar_t",     "while",
                                             "xor",           "xor_eq"};

bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::string accessor_name(std::string_view fix_name) {
    std::string out;
    for (std::size_t i = 0; i < fix_name.size(); ++i) {
        char const c = fix_name[i];
        if (!is_upper(c) && !is_lower(c) && !is_digit(c)) {
            if (!out.empty() && out.back() != '_') out += '_';
            continue;
        }
        if (is_upper(c) && i > 0 && !out.empty() && out.back() != '_') {
            char const before = fix_name[i - 1];
            bool const acronym_end =
                is_upper(before) && i + 1 < fix_name.size() && is_lower(fix_name[i + 1]);
            if (is_lower(before) || is_digit(before) || acronym_end) out += '_';
        }
        out += is_upper(c) ? static_cast<char>(c - 'A' + 'a') : c;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    while (!out.empty() && out.front() == '_') out.erase(0, 1);
    if (out.empty()) return out;
    if (is_digit(out.front())) out.insert(0, "_");
    for (std::string_view kw : kCppKeywords) {
        if (out == kw) {
            out += '_';
            break;
        }
    }
    return out;
}

std::string_view without_no_prefix(std::string_view fix_name) {
    if (fix_name.size() > 2 && fix_name[0] == 'N' && fix_name[1] == 'o' && is_upper(fix_name[2])) {
        return fix_name.substr(2);
    }
    return fix_name;
}

// ---------------------------------------------------------------------------
// One member function: the return type (after "[[nodiscard]] inline "), the
// name line, and the body lines, each stripped, the body joined by '\n'.
// ---------------------------------------------------------------------------
enum class kind { scalar, decimal, group, nested, passthrough, view, unclassified };

struct classified {
    kind k = kind::unclassified;
    std::uint32_t tag = 0;
    std::string id;             // the accessor identifier
    vkind vk = vkind::unknown;  // the value kind its text fixes
};

classified classify_message_member(std::string_view ns, std::string_view ret, std::string_view name,
                                   std::string_view body) {
    if (ret == "::fixpp::core::expected_t<::fixpp::wire::field_view>" &&
        name == "field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]" &&
        body == "{ return view_.get(tag); }") {
        return {kind::passthrough, 0, {}, vkind::unknown};
    }
    if (ret == std::string{kMsgView} + " const&" &&
        name == "view() const noexcept [[clang::lifetimebound]]" && body == "{ return view_; }") {
        return {kind::view, 0, {}, vkind::unknown};
    }
    for (scalar_type const& t : kScalarTypes) {
        if (ret != "::fixpp::core::expected_t<" + std::string{t.cpp} + ">") continue;
        std::string const name_pat = t.kind == vkind::string_view
                                         ? "@I() const noexcept [[clang::lifetimebound]]"
                                         : "@I() const noexcept";
        caps n;
        caps c;
        if (match(name_pat, name, n) &&
            match("{ return ::fixpp::dict::decode_field<" + std::string{t.cpp} +
                      ">(view_.template get<@N>()); }",
                  body, c)) {
            return {kind::scalar, c.nums.front(), n.ids.front(), t.kind};
        }
    }
    if (ret == "::fixpp::core::expected_t<::fixpp::decimal_t>") {
        caps n;
        caps c;
        if (match("@I(::std::pmr::memory_resource* mr) const noexcept", name, n) &&
            match("{ auto fv = view_.template get<@N>(); if (!fv) return "
                  "::std::unexpected{fv.error()};\n"
                  "return ::fixpp::decimal_t::parse(fv->bytes(), mr); }",
                  body, c)) {
            return {kind::decimal, c.nums.front(), n.ids.front(), vkind::decimal};
        }
    }
    {
        std::string const groups = "::fixpp::" + std::string{ns} + "::groups::G_";
        caps n;
        caps c;
        if (match("::fixpp::wire::group_view<" + groups + "@N>", ret, c) &&
            match("@I() const noexcept [[clang::lifetimebound]]", name, n) &&
            match("{ return view_.template group<@N, " + groups + "@N>(); }", body, c) &&
            all_equal(c.nums)) {
            return {kind::group, c.nums.front(), n.ids.front(), vkind::group};
        }
    }
    return {};
}

classified classify_group_member(std::string_view ret, std::string_view name,
                                 std::string_view body) {
    if (ret == "::fixpp::core::expected_t<::fixpp::wire::field_view>" &&
        name == "field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]" &&
        body == "{ return ::fixpp::wire::get(ctx_.span, tag, ctx_.hooks, ctx_.gen); }") {
        return {kind::passthrough, 0, {}, vkind::unknown};
    }
    for (scalar_type const& t : kScalarTypes) {
        if (ret != "::fixpp::core::expected_t<" + std::string{t.cpp} + ">") continue;
        std::string const name_pat = t.kind == vkind::string_view
                                         ? "@I() const noexcept [[clang::lifetimebound]]"
                                         : "@I() const noexcept";
        caps n;
        caps c;
        if (match(name_pat, name, n) &&
            match("{ return ::fixpp::dict::decode_field<" + std::string{t.cpp} +
                      ">(::fixpp::wire::get(ctx_.span, @N, ctx_.hooks, ctx_.gen)); }",
                  body, c)) {
            return {kind::scalar, c.nums.front(), n.ids.front(), t.kind};
        }
    }
    if (ret == "::fixpp::core::expected_t<::fixpp::decimal_t>") {
        caps n;
        caps c;
        if (match("@I(::std::pmr::memory_resource* mr) const noexcept", name, n) &&
            match("{ auto fv = ::fixpp::wire::get(ctx_.span, @N, ctx_.hooks, ctx_.gen); if (!fv) "
                  "return ::std::unexpected{fv.error()};\n"
                  "return ::fixpp::decimal_t::parse(fv->bytes(), mr); }",
                  body, c)) {
            return {kind::decimal, c.nums.front(), n.ids.front(), vkind::decimal};
        }
    }
    {
        caps n;
        caps c;
        if (match("::fixpp::wire::group_view<G_@N>", ret, c) &&
            match("@I() const noexcept [[clang::lifetimebound]]", name, n) &&
            match("{ if (ctx_.parent_cache_owner == nullptr) return {};\n"
                  "auto const r = ctx_.parent_cache_owner->nested_group_slices("
                  "ctx_.outer_occurrence_id, ctx_.span.size(), @N, ctx_.hooks, ctx_.gen, "
                  "ctx_.group_ctx);\n"
                  "::fixpp::wire::entry_context child_ctx = ctx_;\n"
                  "child_ctx.group_ctx = ctx_.group_ctx.pushed(@N);\n"
                  "return ::fixpp::wire::group_view<G_@N>{r.slices, child_ctx, r.alloc_failed}; }",
                  body, c) &&
            all_equal(c.nums)) {
            return {kind::nested, c.nums.front(), n.ids.front(), vkind::group};
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// The census over one Messages.hpp text.
// ---------------------------------------------------------------------------
struct accessor {
    std::uint32_t tag = 0;
    std::string id;
    vkind vk = vkind::unknown;
};

struct class_record {
    bool is_group = false;
    std::vector<accessor> typed;  // typed accessors, in order
    int passthroughs = 0;
    int views = 0;
    int traits = 0;  // Reify.hpp only: owning_message_traits specialisations
};

struct census {
    std::map<std::string, class_record> classes;
    std::vector<std::string> unclassified;  // "line N: text"
    std::size_t typed = 0;
};

constexpr std::string_view kIncludes[] = {
    "#include <fixpp/core/decimal_alias.hpp>",
    "#include <fixpp/core/error.hpp>",
    "#include <fixpp/dict/field_traits.hpp>",
    "#include <fixpp/dict/version_profile.hpp>",
    "#include <fixpp/wire/message_view_contract.hpp>",
    "#include <cstdint>",
    "#include <expected>",
    "#include <memory_resource>",
    "#include <string_view>",
    "#include <utility>",
};

census run_census(std::string_view ns, std::vector<std::string> const& lines) {
    census out;
    auto bad = [&](std::size_t idx, std::string_view why) {
        out.unclassified.push_back("line " + std::to_string(idx + 1) + " (" + std::string{why} +
                                   "): " + lines[idx]);
    };
    std::string const nsv{ns};
    std::string cls;  // the open class, empty outside one
    bool in_group = false;
    std::string last_message;  // the message class a static_assert may name
    std::set<std::string_view> includes_seen;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string_view const s = strip(lines[i]);
        if (s.empty() || s.starts_with("//")) continue;

        if (s.starts_with("[[nodiscard]] inline ")) {
            if (cls.empty()) {
                bad(i, "member function outside a class");
                continue;
            }
            std::string_view const ret = s.substr(std::string_view{"[[nodiscard]] inline "}.size());
            std::size_t const at = i;
            if (i + 1 >= lines.size()) {
                bad(at, "truncated member function");
                break;
            }
            std::string_view name = strip(lines[i + 1]);
            std::string body;
            i += 1;
            if (auto const brace = name.find('{'); brace != std::string_view::npos) {
                body = std::string{name.substr(brace)};
                name = strip(name.substr(0, brace));
            } else {
                int depth = 0;
                bool first = true;
                while (true) {
                    if (i + 1 >= lines.size()) {
                        bad(at, "unterminated body");
                        return out;
                    }
                    ++i;
                    std::string_view const b = strip(lines[i]);
                    if (!first) body += '\n';
                    first = false;
                    body += b;
                    for (char ch : b) {
                        if (ch == '{') ++depth;
                        if (ch == '}') --depth;
                    }
                    if (depth <= 0) break;
                }
            }
            classified const r = in_group ? classify_group_member(ret, name, body)
                                          : classify_message_member(ns, ret, name, body);
            auto& rec = out.classes[cls];
            switch (r.k) {
                case kind::passthrough:
                    ++rec.passthroughs;
                    break;
                case kind::view:
                    ++rec.views;
                    break;
                case kind::unclassified:
                    bad(at,
                        "member function '" + std::string{name} + "' matches no accessor template");
                    break;
                default:
                    rec.typed.push_back({r.tag, r.id, r.vk});
                    ++out.typed;
                    break;
            }
            continue;
        }

        caps c;
        if (cls.empty()) {
            if (s == "#pragma once") continue;
            if (s.starts_with("#include ")) {
                bool known = false;
                for (auto inc : kIncludes) {
                    if (s == inc) {
                        known = true;
                        if (!includes_seen.insert(inc).second) bad(i, "include repeated");
                    }
                }
                if (!known) bad(i, "include outside the pinned set");
                continue;
            }
            if (s == "namespace fixpp::" + nsv +
                         "::groups {  // shared repeating-group flyweights (AC-G5/AC-G6)" ||
                s == "}  // namespace fixpp::" + nsv + "::groups") {
                in_group = s.starts_with("namespace");
                continue;
            }
            if (s == "namespace fixpp::" + nsv + " {" || s == "}  // namespace fixpp::" + nsv) {
                in_group = false;
                continue;
            }
            if (match("class owning_@I;", s)) continue;
            if (match("class @I {", s, c)) {
                cls = c.ids.front();
                bool const group_name = match("G_@N", cls);
                if (group_name != in_group) {
                    bad(i, "class in the wrong namespace block");
                }
                if (out.classes.contains(cls)) bad(i, "class defined twice");
                out.classes[cls].is_group = in_group;
                continue;
            }
            if (!last_message.empty() && s == "static_assert(sizeof(" + last_message +
                                                  ") == sizeof(" + std::string{kMsgView} +
                                                  " const*));") {
                last_message.clear();
                continue;
            }
            bad(i, "line outside the grammar (namespace scope)");
            continue;
        }

        // Inside a class.
        if (s == "public:" || s == "private:") continue;
        if (s == "};") {
            if (!in_group) last_message = cls;
            cls.clear();
            continue;
        }
        if (in_group) {
            if (s == cls + "() noexcept = default;" ||
                s == "explicit " + cls + "(::fixpp::wire::entry_context ctx) noexcept" ||
                s == ": ctx_(ctx) {}" || s == "::fixpp::wire::entry_context ctx_{};") {
                continue;
            }
        } else {
            if (match("static constexpr ::std::string_view msg_type_v = \"@S\";", s) ||
                s == "static constexpr ::fixpp::dict::application_version version_v =" ||
                match("::fixpp::dict::application_version::@I;", s) ||
                s == "explicit " + cls + "(" + std::string{kMsgView} +
                         " const& view [[clang::lifetimebound]]) noexcept" ||
                s == ": view_(view) {}" || s == std::string{kMsgView} + " const& view_;") {
                continue;
            }
        }
        bad(i, "line outside the grammar (class scope)");
    }
    if (!cls.empty()) out.unclassified.push_back("class " + cls + " is never closed");
    return out;
}

// ---------------------------------------------------------------------------
// The census over one Reify.hpp text (emit_reify.cpp). Its owning_<Msg>
// classes carry their own typed accessors, which read through the lazily
// rebuilt view(): `view().template get<N>()` / `group<N, ...G_N>()`. The
// grammar is closed over the whole file: the pinned includes, the namespace
// lines, each owning_<Msg> class body, the out-of-line definitions of its
// move constructor, move assignment, view() and from_view (exact text, per
// class, in that order, directly after the class), and one
// owning_message_traits specialisation per class. A record is keyed by the
// message name (owning_ dropped), so the population and identity checks
// compare it with that message's Fields.hpp rows.
// ---------------------------------------------------------------------------
classified classify_owning_member(std::string_view ns, std::string_view ret, std::string_view name,
                                  std::string_view body) {
    if (ret == "::fixpp::core::expected_t<::fixpp::wire::field_view>" &&
        name == "field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]" &&
        body == "{ return view().get(tag); }") {
        return {kind::passthrough, 0, {}, vkind::unknown};
    }
    for (scalar_type const& t : kScalarTypes) {
        if (ret != "::fixpp::core::expected_t<" + std::string{t.cpp} + ">") continue;
        std::string const name_pat = t.kind == vkind::string_view
                                         ? "@I() const noexcept [[clang::lifetimebound]]"
                                         : "@I() const noexcept";
        caps n;
        caps c;
        if (match(name_pat, name, n) &&
            match("{ return ::fixpp::dict::decode_field<" + std::string{t.cpp} +
                      ">(view().template get<@N>()); }",
                  body, c)) {
            return {kind::scalar, c.nums.front(), n.ids.front(), t.kind};
        }
    }
    if (ret == "::fixpp::core::expected_t<::fixpp::decimal_t>") {
        caps n;
        caps c;
        if (match("@I(::std::pmr::memory_resource* mr) const noexcept", name, n) &&
            match("{ auto fv = view().template get<@N>(); if (!fv) return "
                  "::std::unexpected{fv.error()};\n"
                  "return ::fixpp::decimal_t::parse(fv->bytes(), mr); }",
                  body, c)) {
            return {kind::decimal, c.nums.front(), n.ids.front(), vkind::decimal};
        }
    }
    {
        std::string const groups = "::fixpp::" + std::string{ns} + "::groups::G_";
        caps n;
        caps c;
        if (match("::fixpp::wire::group_view<" + groups + "@N>", ret, c) &&
            match("@I() const noexcept [[clang::lifetimebound]]", name, n) &&
            match("{ return view().template group<@N, " + groups + "@N>(); }", body, c) &&
            all_equal(c.nums)) {
            return {kind::group, c.nums.front(), n.ids.front(), vkind::group};
        }
    }
    return {};
}

std::vector<std::string> reify_includes(std::string_view ns) {
    return {"#include <fixpp/" + std::string{ns} +
                "/Messages.hpp>   // flyweight classes + owning_<Msg> forward-decls",
            "#include <fixpp/core/decimal_alias.hpp>",
            "#include <fixpp/core/error.hpp>",
            "#include <fixpp/dict/field_traits.hpp>",
            "#include <fixpp/dict/reify.hpp>         // owning_message_traits primary template",
            "#include <fixpp/dict/version_profile.hpp>",
            "#include <fixpp/wire/message_view_contract.hpp>",
            "#include <fixpp/wire/parser.hpp>          // MessageView<Index>, Framer,",
            "#include <cstddef>",
            "#include <cstdint>",
            "#include <memory_resource>",
            "#include <new>",
            "#include <optional>",
            "#include <span>",
            "#include <string_view>",
            "#include <type_traits>",
            "#include <vector>"};
}

// The out-of-line definitions that follow each owning_<Msg> class, stripped.
std::vector<std::string> owning_out_of_line(std::string const& o) {
    std::string const mv{kMsgView};
    return {"inline " + o + "::" + o + "(" + o + " && other) noexcept",
            ": bytes_(::std::move(other.bytes_))",
            ", view_cache_(::std::nullopt) {",
            "other.view_cache_ = ::std::nullopt;  // reset source cache (I-9/AC-R4)",
            "}",
            "inline " + o + "& " + o + "::operator=(" + o + " && other) noexcept {",
            "if (this != &other) {",
            "bytes_            = ::std::move(other.bytes_);",
            "view_cache_       = ::std::nullopt;  // reset dest cache (I-9/AC-R4)",
            "other.view_cache_ = ::std::nullopt;  // reset source cache",
            "}",
            "return *this;",
            "}",
            "inline " + mv + " const&",
            o + "::view() const noexcept {",
            "if (!view_cache_) {",
            "::fixpp::wire::pmr_carry_buffer carry{0, bytes_.get_allocator().resource()};",
            "::fixpp::wire::Framer framer{};",
            "::fixpp::wire::frame_view out_arr[1]{};",
            "auto framed = framer.feed(",
            "::std::span<const ::std::byte>{bytes_.data(), bytes_.size()},",
            "carry, ::std::span<::fixpp::wire::frame_view>{out_arr, 1});",
            "if (framed && !framed->empty()) {",
            "view_cache_.emplace((*framed)[0], bytes_.get_allocator().resource());",
            "} else {",
            "view_cache_.emplace();",
            "}",
            "}",
            "return *view_cache_;",
            "}",
            "inline ::fixpp::core::expected_t<" + o + ">",
            o + "::from_view(" + mv + " const& src, ::std::pmr::memory_resource* mr) {",
            "try {",
            o + " owned{mr};",
            "auto const sb = src.bytes();  // full validated frame span",
            "owned.bytes_.assign(sb.begin(), sb.end());  // deep copy into mr arena",
            "return owned;  // move (custom noexcept move ctor)",
            "} catch (::std::bad_alloc const&) {",
            "return ::std::unexpected{::fixpp::core::error::dict_reify_oom};",
            "}",
            "}"};
}

census run_reify_census(std::string_view ns, std::vector<std::string> const& lines) {
    census out;
    auto bad = [&](std::size_t idx, std::string_view why) {
        out.unclassified.push_back("line " + std::to_string(idx + 1) + " (" + std::string{why} +
                                   "): " + lines[idx]);
    };
    std::string const nsv{ns};
    std::string const mv{kMsgView};
    std::vector<std::string> const includes = reify_includes(ns);
    std::set<std::string> includes_seen;
    enum class region { head, body, traits } where = region::head;
    std::string oid;                // the open owning_ class, empty outside one
    std::vector<std::string> tail;  // the out-of-line lines still expected
    std::size_t tail_at = 0;
    std::vector<std::string> trait;  // the traits lines still expected
    std::size_t trait_at = 0;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string_view const s = strip(lines[i]);
        if (s.empty() || s.starts_with("//")) continue;

        if (tail_at < tail.size()) {
            if (s != tail[tail_at]) bad(i, "out-of-line definition outside the grammar");
            ++tail_at;
            continue;
        }
        if (trait_at < trait.size()) {
            if (s != trait[trait_at]) bad(i, "traits specialisation outside the grammar");
            ++trait_at;
            continue;
        }

        if (!oid.empty()) {
            std::string const msg = oid.substr(std::string_view{"owning_"}.size());
            auto& rec = out.classes[msg];
            // A two-line declaration whose second line must follow exactly.
            auto pair = [&](std::string_view first, std::string const& second) {
                if (s != first) return false;
                if (i + 1 >= lines.size() || strip(lines[i + 1]) != second) {
                    bad(i, "declaration outside the grammar");
                }
                ++i;
                return true;
            };
            if (s.starts_with("[[nodiscard]] inline ")) {
                std::string_view const ret =
                    s.substr(std::string_view{"[[nodiscard]] inline "}.size());
                std::size_t const at = i;
                if (i + 2 >= lines.size()) {
                    bad(at, "truncated member function");
                    break;
                }
                std::string_view const name = strip(lines[i + 1]);
                std::string body{strip(lines[i + 2])};
                i += 2;
                int depth = 0;
                for (char ch : body) depth += ch == '{' ? 1 : ch == '}' ? -1 : 0;
                while (depth > 0 && i + 1 < lines.size()) {
                    ++i;
                    std::string_view const b = strip(lines[i]);
                    body += '\n';
                    body += b;
                    for (char ch : b) depth += ch == '{' ? 1 : ch == '}' ? -1 : 0;
                }
                classified const r = classify_owning_member(ns, ret, name, body);
                if (r.k == kind::passthrough) {
                    ++rec.passthroughs;
                } else if (r.k == kind::unclassified) {
                    bad(at,
                        "member function '" + std::string{name} + "' matches no accessor template");
                } else {
                    rec.typed.push_back({r.tag, r.id, r.vk});
                    ++out.typed;
                }
                continue;
            }
            if (pair("[[nodiscard]] " + mv + " const&",
                     "view() const noexcept [[clang::lifetimebound]];")) {
                ++rec.views;
                continue;
            }
            if (pair("[[nodiscard]] static constexpr ::fixpp::dict::application_version",
                     "which() noexcept { return version_v; }") ||
                pair("[[nodiscard]] static ::fixpp::core::expected_t<" + oid + ">",
                     "from_view(" + mv + " const& src, ::std::pmr::memory_resource* mr);")) {
                continue;
            }
            if (s == "public:" || s == "private:" ||
                match("static constexpr ::std::string_view msg_type_v = \"@S\";", s) ||
                s == "static constexpr ::fixpp::dict::application_version version_v =" ||
                match("::fixpp::dict::application_version::@I;", s) ||
                s == oid + "(" + oid + " const&) = delete;" ||
                s == oid + "& operator=(" + oid + " const&) = delete;" ||
                s == oid + "(" + oid + " &&) noexcept;" ||
                s == oid + "& operator=(" + oid + " &&) noexcept;" ||
                s == "~" + oid + "() = default;" ||
                s == "explicit " + oid + "(::std::pmr::memory_resource* mr) : bytes_(mr) {}" ||
                s == "::std::pmr::vector<::std::byte>                    bytes_;      // owned "
                     "arena copy" ||
                s == "mutable ::std::optional<" + mv +
                         "> view_cache_;  // lazy, nullopt after move") {
                continue;
            }
            if (s == "};") {
                tail = owning_out_of_line(oid);
                tail_at = 0;
                oid.clear();
                continue;
            }
            bad(i, "line outside the grammar (class scope)");
            continue;
        }

        caps c;
        if (where == region::head) {
            if (s == "#pragma once") continue;
            if (s.starts_with("#include ")) {
                bool known = false;
                for (auto const& inc : includes) {
                    if (s == inc) {
                        known = true;
                        if (!includes_seen.insert(inc).second) bad(i, "include repeated");
                    }
                }
                if (!known) bad(i, "include outside the pinned set");
                continue;
            }
            if (s == "namespace fixpp::" + nsv + " {") {
                where = region::body;
                continue;
            }
        } else if (where == region::body) {
            if (match("class owning_@I {", s, c)) {
                oid = "owning_" + c.ids.front();
                std::string const msg = c.ids.front();
                if (out.classes.contains(msg)) bad(i, "class defined twice");
                out.classes[msg].is_group = false;
                continue;
            }
            if (s == "}  // namespace fixpp::" + nsv) {
                where = region::traits;
                continue;
            }
        } else {
            if (s == "template <>" && i + 1 < lines.size() &&
                match("struct fixpp::dict::owning_message_traits<fixpp::" + nsv + "::@I> {",
                      strip(lines[i + 1]), c)) {
                std::string const msg = c.ids.front();
                auto const it = out.classes.find(msg);
                if (it == out.classes.end()) {
                    bad(i, "traits for a class the file does not define");
                } else {
                    ++it->second.traits;
                }
                std::string const q = "fixpp::" + nsv + "::";
                trait = {"using type = " + q + "owning_" + msg + ";", "};",
                         "static_assert(::std::is_same_v<",
                         "fixpp::dict::owning_message_t<" + q + msg + ">,",
                         q + "owning_" + msg + ">);  // AC-G7a"};
                trait_at = 0;
                ++i;
                continue;
            }
        }
        bad(i, "line outside the grammar (namespace scope)");
    }
    if (!oid.empty()) out.unclassified.push_back("class " + oid + " is never closed");
    if (tail_at < tail.size() || trait_at < trait.size()) {
        out.unclassified.push_back("the file ends inside a definition block");
    }
    if (where != region::traits) out.unclassified.push_back("namespace never closed");
    return out;
}

// A Reify census reports a problem as an unclassified line, or as a class
// without exactly one passthrough, one view() and one traits specialisation.
bool reify_rejects(census const& c) {
    if (!c.unclassified.empty()) return true;
    for (auto const& [name, rec] : c.classes) {
        if (rec.passthroughs != 1 || rec.views != 1 || rec.traits != 1) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// The independent side: Fields.hpp's FieldRef rows.
//   message name -> its rows with group_no_tag 0: tag -> data type
//   group no_tag -> its member rows across every message: tag -> data type
// ---------------------------------------------------------------------------
using tag_types = std::map<std::uint32_t, std::uint32_t>;

struct field_rows {
    std::map<std::string, tag_types> messages;
    std::map<std::uint32_t, tag_types> groups;
    std::size_t rows = 0;
    std::vector<std::string> malformed;
};

field_rows read_fields(std::vector<std::string> const& lines) {
    field_rows out;
    std::string cur;
    auto put = [&](tag_types& into, std::uint32_t tag, std::uint32_t type, std::size_t i) {
        auto const [it, fresh] = into.emplace(tag, type);
        if (!fresh && it->second != type) {
            out.malformed.push_back("line " + std::to_string(i + 1) +
                                    " (a second data type for the tag): " + lines[i]);
        }
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string_view const s = strip(lines[i]);
        caps c;
        if (cur.empty()) {
            if (match("inline constexpr ::fixpp::dict::FieldRef @I[] = {  // MsgType @S", s, c) ||
                match("inline constexpr ::fixpp::dict::FieldRef @I[] = {", s, c)) {
                std::string const arr = c.ids.front();
                std::string_view const suffix = "_fields";
                if (!std::string_view{arr}.ends_with(suffix)) {
                    out.malformed.push_back("line " + std::to_string(i + 1) + ": " + lines[i]);
                    continue;
                }
                cur = arr.substr(0, arr.size() - suffix.size());
                out.messages[cur];
            }
            continue;
        }
        if (s == "};") {
            cur.clear();
            continue;
        }
        // { tag, static_cast<...field_data_type>(t), static_cast<...field_presence>(r),
        //   condition_index, group_no_tag, component_index, enum_table_index,
        //   length_pair_data_tag, _reserved },
        std::vector<std::uint32_t> nums;
        std::uint64_t v = 0;
        bool in_num = false;
        for (char ch : s) {
            if (ch >= '0' && ch <= '9') {
                v = v * 10U + static_cast<std::uint64_t>(ch - '0');
                in_num = true;
            } else if (in_num) {
                nums.push_back(static_cast<std::uint32_t>(v));
                v = 0;
                in_num = false;
            }
        }
        if (nums.size() != 9 || !s.starts_with("{ ") || !s.ends_with(" },") ||
            !match("{ @N, static_cast<::fixpp::dict::field_data_type>(@S", s)) {
            out.malformed.push_back("line " + std::to_string(i + 1) + ": " + lines[i]);
            continue;
        }
        ++out.rows;
        if (nums[4] == 0) {
            put(out.messages[cur], nums[0], nums[1], i);
        } else {
            put(out.groups[nums[4]], nums[0], nums[1], i);
        }
    }
    return out;
}

bool heads_group(field_rows const& f, std::uint32_t tag) { return f.groups.contains(tag); }

// The kind an accessor of `tag` must have, given its Fields.hpp data type.
vkind expected_kind(field_rows const& f, std::uint32_t tag, std::uint32_t type) {
    return heads_group(f, tag) ? vkind::group : mapped_kind(type);
}

tag_types const* rows_of(field_rows const& f, std::string const& cls, class_record const& rec) {
    if (rec.is_group) {
        auto const it = f.groups.find(static_cast<std::uint32_t>(std::stoul(cls.substr(2))));
        return it == f.groups.end() ? nullptr : &it->second;
    }
    auto const it = f.messages.find(cls);
    return it == f.messages.end() ? nullptr : &it->second;
}

// Check 2: every class's typed-accessor tags equal its FieldRef rows (less
// the rows the mapping omits), with no tag bound twice and no class or array
// without its counterpart.
// `with_groups` is false for Reify.hpp, whose owning_<Msg> classes reuse
// Messages.hpp's group flyweights and declare none of their own.
std::vector<std::string> population_problems(census const& c, field_rows const& f,
                                             bool with_groups = true) {
    std::vector<std::string> out;
    std::size_t expected_typed = 0;
    auto compare = [&](std::string const& cls, tag_types const& rows, bool want_group) {
        std::set<std::uint32_t> want;
        for (auto const& [tag, type] : rows) {
            if (expected_kind(f, tag, type) != vkind::omitted) want.insert(tag);
        }
        expected_typed += want.size();
        auto const it = c.classes.find(cls);
        if (it == c.classes.end() || it->second.is_group != want_group) {
            out.push_back("Fields.hpp has rows for " + cls + " but Messages.hpp has no such class");
            return;
        }
        std::set<std::uint32_t> got;
        for (auto const& a : it->second.typed) {
            if (!got.insert(a.tag).second) {
                out.push_back(cls + ": tag " + std::to_string(a.tag) + " bound by two accessors");
            }
        }
        if (got != want) {
            out.push_back(cls + ": accessor tags differ from its FieldRef rows");
        }
    };
    for (auto const& [m, rows] : f.messages) compare(m, rows, false);
    if (with_groups) {
        for (auto const& [no_tag, rows] : f.groups) {
            compare("G_" + std::to_string(no_tag), rows, true);
        }
    }
    for (auto const& [name, rec] : c.classes) {
        if (rows_of(f, name, rec) == nullptr && !rec.typed.empty()) {
            out.push_back(name + " has typed accessors but no FieldRef rows in Fields.hpp");
        }
    }
    if (c.typed != expected_typed) {
        out.push_back("typed accessors classified (" + std::to_string(c.typed) +
                      ") differ from the FieldRef-derived count (" +
                      std::to_string(expected_typed) + ")");
    }
    return out;
}

// Check 3: each accessor's identifier is its field's accessor name, and its
// value kind is the one the mapping assigns.
using tag_names = std::map<std::uint32_t, std::string>;

std::vector<std::string> identity_problems(census const& c, field_rows const& f,
                                           tag_names const& names) {
    std::vector<std::string> out;
    for (auto const& [cls, rec] : c.classes) {
        tag_types const* rows = rows_of(f, cls, rec);
        std::set<std::string> ids;
        for (auto const& a : rec.typed) ids.insert(a.id);
        for (auto const& a : rec.typed) {
            std::string const where = cls + "::" + a.id + " (tag " + std::to_string(a.tag) + ")";
            auto const n = names.find(a.tag);
            std::string const fix = n == names.end() ? std::string{} : n->second;
            std::string const base =
                fix.empty() ? std::string{}
                            : accessor_name(a.vk == vkind::group ? without_no_prefix(fix) : fix);
            if (base.empty()) {
                out.push_back(where + ": the dictionary gives tag " + std::to_string(a.tag) +
                              " no field name");
            } else if (a.id != base) {
                std::string const suffixed = base + "_t" + std::to_string(a.tag);
                if (a.id != suffixed || !ids.contains(base)) {
                    out.push_back(where + ": the field is " + fix + ", whose accessor name is " +
                                  base + (a.id == suffixed ? " (no other accessor holds it)" : ""));
                }
            }
            if (rows == nullptr) continue;  // reported by the population check
            auto const r = rows->find(a.tag);
            if (r == rows->end()) continue;  // reported by the population check
            vkind const want = expected_kind(f, a.tag, r->second);
            if (a.vk != want) {
                out.push_back(where + ": its text fixes " + std::string{kind_name(a.vk)} +
                              ", the mapping assigns " + std::string{kind_name(want)} +
                              " to data type " + std::to_string(r->second));
            }
        }
    }
    return out;
}

std::string first_of(std::vector<std::string> const& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size() && i < 3; ++i) s += "\n  " + v[i];
    return s;
}

std::vector<std::string> read_lines(std::string const& path, bool& ok) {
    std::ifstream f(path);
    ok = static_cast<bool>(f);
    std::vector<std::string> out;
    std::string line;
    while (std::getline(f, line)) out.push_back(line);
    return out;
}

std::vector<std::string> split(std::string_view text) {
    std::vector<std::string> out;
    std::istringstream in{std::string{text}};
    std::string line;
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

// A synthetic Messages.hpp in the shipped grammar: two group flyweights
// (G_539 with two scalars whose names collide, so the second takes the
// `_t<tag>` form; G_78 with a scalar, a decimal and a nested-group accessor)
// and one message with three scalars, a decimal and a group accessor. The
// guard checks it passes, then that each seeded mutation of it fails.
constexpr std::string_view kSynthetic = R"(// header comment
#pragma once
#include <cstdint>
namespace fixpp::vx::groups {  // shared repeating-group flyweights (AC-G5/AC-G6)
    class G_539 {
    public:
        G_539() noexcept = default;
        explicit G_539(::fixpp::wire::entry_context ctx) noexcept
            : ctx_(ctx) {}
    [[nodiscard]] inline ::fixpp::core::expected_t<::std::string_view>
    nested_party_id() const noexcept [[clang::lifetimebound]]
    { return ::fixpp::dict::decode_field<::std::string_view>(::fixpp::wire::get(ctx_.span, 524, ctx_.hooks, ctx_.gen)); }
    [[nodiscard]] inline ::fixpp::core::expected_t<::std::string_view>
    nested_party_id_t9001() const noexcept [[clang::lifetimebound]]
    { return ::fixpp::dict::decode_field<::std::string_view>(::fixpp::wire::get(ctx_.span, 9001, ctx_.hooks, ctx_.gen)); }
    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::wire::field_view>
    field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]
    { return ::fixpp::wire::get(ctx_.span, tag, ctx_.hooks, ctx_.gen); }
    private:
        ::fixpp::wire::entry_context ctx_{};
    };
    class G_78 {
    public:
        G_78() noexcept = default;
        explicit G_78(::fixpp::wire::entry_context ctx) noexcept
            : ctx_(ctx) {}
    [[nodiscard]] inline ::fixpp::core::expected_t<::std::string_view>
    alloc_account() const noexcept [[clang::lifetimebound]]
    { return ::fixpp::dict::decode_field<::std::string_view>(::fixpp::wire::get(ctx_.span, 79, ctx_.hooks, ctx_.gen)); }
    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::decimal_t>
    alloc_shares(::std::pmr::memory_resource* mr) const noexcept
    { auto fv = ::fixpp::wire::get(ctx_.span, 80, ctx_.hooks, ctx_.gen); if (!fv) return ::std::unexpected{fv.error()};
      return ::fixpp::decimal_t::parse(fv->bytes(), mr); }
    [[nodiscard]] inline ::fixpp::wire::group_view<G_539>
    nested_party_i_ds() const noexcept [[clang::lifetimebound]]
    { if (ctx_.parent_cache_owner == nullptr) return {};
      auto const r = ctx_.parent_cache_owner->nested_group_slices(ctx_.outer_occurrence_id, ctx_.span.size(), 539, ctx_.hooks, ctx_.gen, ctx_.group_ctx);
      ::fixpp::wire::entry_context child_ctx = ctx_;
      child_ctx.group_ctx = ctx_.group_ctx.pushed(539);
      return ::fixpp::wire::group_view<G_539>{r.slices, child_ctx, r.alloc_failed}; }
    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::wire::field_view>
    field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]
    { return ::fixpp::wire::get(ctx_.span, tag, ctx_.hooks, ctx_.gen); }
    private:
        ::fixpp::wire::entry_context ctx_{};
    };
}  // namespace fixpp::vx::groups
namespace fixpp::vx {
class owning_NewOrderSingle;
class NewOrderSingle {
public:
    static constexpr ::std::string_view msg_type_v = "D";
    static constexpr ::fixpp::dict::application_version version_v =
        ::fixpp::dict::application_version::v44;
    explicit NewOrderSingle(::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index> const& view [[clang::lifetimebound]]) noexcept
        : view_(view) {}
    [[nodiscard]] inline ::fixpp::core::expected_t<::std::string_view>
    account() const noexcept [[clang::lifetimebound]]
    { return ::fixpp::dict::decode_field<::std::string_view>(view_.template get<1>()); }
    [[nodiscard]] inline ::fixpp::core::expected_t<::std::string_view>
    cl_ord_id() const noexcept [[clang::lifetimebound]]
    { return ::fixpp::dict::decode_field<::std::string_view>(view_.template get<11>()); }
    [[nodiscard]] inline ::fixpp::core::expected_t<char>
    side() const noexcept
    { return ::fixpp::dict::decode_field<char>(view_.template get<54>()); }
    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::decimal_t>
    price(::std::pmr::memory_resource* mr) const noexcept
    { auto fv = view_.template get<44>(); if (!fv) return ::std::unexpected{fv.error()};
      return ::fixpp::decimal_t::parse(fv->bytes(), mr); }
    [[nodiscard]] inline ::fixpp::wire::group_view<::fixpp::vx::groups::G_78>
    allocs() const noexcept [[clang::lifetimebound]]
    { return view_.template group<78, ::fixpp::vx::groups::G_78>(); }
    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::wire::field_view>
    field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]
    { return view_.get(tag); }
    [[nodiscard]] inline ::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index> const&
    view() const noexcept [[clang::lifetimebound]] { return view_; }

private:
    ::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index> const& view_;
};
static_assert(sizeof(NewOrderSingle) == sizeof(::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index> const*));
}  // namespace fixpp::vx
)";

// Its Fields.hpp: data types 13 String, 11 Char, 5 Price, 6 Qty,
// 3 NumInGroup (field_ref.hpp's field_data_type order).
constexpr std::string_view kSyntheticFields = R"(#pragma once
namespace fixpp::vx::fields {
inline constexpr ::fixpp::dict::FieldRef NewOrderSingle_fields[] = {  // MsgType D
    { 1, static_cast<::fixpp::dict::field_data_type>(13), static_cast<::fixpp::dict::field_presence>(1), 0, 0, 0, 0, 0, 0 },
    { 11, static_cast<::fixpp::dict::field_data_type>(13), static_cast<::fixpp::dict::field_presence>(2), 0, 0, 0, 0, 0, 0 },
    { 44, static_cast<::fixpp::dict::field_data_type>(5), static_cast<::fixpp::dict::field_presence>(1), 0, 0, 0, 0, 0, 0 },
    { 54, static_cast<::fixpp::dict::field_data_type>(11), static_cast<::fixpp::dict::field_presence>(2), 0, 0, 0, 0, 0, 0 },
    { 78, static_cast<::fixpp::dict::field_data_type>(3), static_cast<::fixpp::dict::field_presence>(1), 0, 0, 0, 0, 0, 0 },
    { 79, static_cast<::fixpp::dict::field_data_type>(13), static_cast<::fixpp::dict::field_presence>(1), 0, 78, 0, 0, 0, 0 },
    { 80, static_cast<::fixpp::dict::field_data_type>(6), static_cast<::fixpp::dict::field_presence>(1), 0, 78, 0, 0, 0, 0 },
    { 539, static_cast<::fixpp::dict::field_data_type>(3), static_cast<::fixpp::dict::field_presence>(1), 0, 78, 0, 0, 0, 0 },
    { 524, static_cast<::fixpp::dict::field_data_type>(13), static_cast<::fixpp::dict::field_presence>(1), 0, 539, 0, 0, 0, 0 },
    { 9001, static_cast<::fixpp::dict::field_data_type>(13), static_cast<::fixpp::dict::field_presence>(1), 0, 539, 0, 0, 0, 0 },
};
}  // namespace fixpp::vx::fields
)";

// Its dictionary names. 9001's name differs from 524's only by an '_', so
// both give the accessor name nested_party_id.
tag_names synthetic_names() {
    return {{1, "Account"},         {11, "ClOrdID"},
            {44, "Price"},          {54, "Side"},
            {78, "NoAllocs"},       {79, "AllocAccount"},
            {80, "AllocShares"},    {539, "NoNestedPartyIDs"},
            {524, "NestedPartyID"}, {9001, "Nested_PartyID"}};
}

struct seeded {
    std::string_view what;
    std::string_view from;
    std::string_view to;
};

// Each mutation turns one accessor (or one line) into a runtime resolution,
// or adds a surface a lookup could hide behind. Every one must fail the
// grammar.
constexpr seeded kSeeded[] = {
    {"tag looked up by name at runtime", "view_.template get<1>()",
     "view_.get(tag_by_name(\"Account\"))"},
    {"tag read from a runtime-indexed table", "view_.template get<54>()",
     "view_.get(kTags[slot_])"},
    {"group-entry tag passed as a variable", "ctx_.span, 79, ctx_.hooks",
     "ctx_.span, tag_of_79_, ctx_.hooks"},
    {"value type chosen at runtime", "decode_field<char>(view_.template get<54>())",
     "decode_field_as(view_.template get<54>(), dict_->field_type(54))"},
    {"decode type disagrees with the return type", "decode_field<char>",
     "decode_field<::std::string_view>"},
    {"decimal tag looked up at runtime", "auto fv = view_.template get<44>();",
     "auto fv = view_.get(dict_->field_by_name(\"Price\").value_or(0));"},
    {"group accessor's tag and type disagree", "group<78, ::fixpp::vx::groups::G_78>",
     "group<79, ::fixpp::vx::groups::G_78>"},
    {"nested-group tag passed as a variable", "ctx_.span.size(), 539,",
     "ctx_.span.size(), nested_tag_,"},
    {"accessor without [[nodiscard]] inline",
     "    [[nodiscard]] inline ::fixpp::core::expected_t<char>\n    side()",
     "    ::fixpp::core::expected_t<char>\n    side()"},
    {"dictionary member added to a message",
     "    ::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index> const& view_;\n",
     "    ::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index> const& view_;\n"
     "    ::fixpp::dict::Dictionary const* dict_ = nullptr;\n"},
    {"include outside the pinned set", "#include <cstdint>\n",
     "#include <cstdint>\n#include <fixpp/dict/dictionary.hpp>\n"},
    {"namespace-scope lookup table", "namespace fixpp::vx {\n",
     "namespace fixpp::vx {\ninline constexpr ::std::uint16_t kTags[] = {54};\n"},
};

// Identity mutations: each keeps the grammar and the tag sets, so only the
// identity check can catch it. `edits` apply in order to the Messages text
// (or, when `fields` is set, to the Fields text); the "@SWAP@" sentinel lets
// two literals exchange places.
struct identity_seed {
    std::string_view what;
    bool fields;
    std::vector<std::pair<std::string_view, std::string_view>> edits;
};

std::vector<identity_seed> identity_seeds() {
    return {
        {"two same-type message accessors' tag literals swapped",
         false,
         {{"view_.template get<1>()", "@SWAP@"},
          {"view_.template get<11>()", "view_.template get<1>()"},
          {"@SWAP@", "view_.template get<11>()"}}},
        {"a scalar's return and decode_field<T> type changed together",
         false,
         {{"::fixpp::core::expected_t<char>\n    side()",
           "::fixpp::core::expected_t<bool>\n    side()"},
          {"decode_field<char>(view_.template get<54>())",
           "decode_field<bool>(view_.template get<54>())"}}},
        {"a message scalar renamed, tag and type kept",
         false,
         {{"    side() const", "    sides() const"}}},
        {"a group-entry scalar renamed", false, {{"alloc_account()", "allocation_account()"}}},
        {"a message group accessor named without dropping No",
         false,
         {{"allocs()", "no_allocs()"}}},
        {"a nested-group accessor renamed", false, {{"nested_party_i_ds()", "nested_parties()"}}},
        {"a collision suffix naming another tag",
         false,
         {{"nested_party_id_t9001()", "nested_party_id_t524()"}}},
        {"a collision suffix with no collision", false, {{"cl_ord_id()", "cl_ord_id_t11()"}}},
        {"Fields.hpp gives the char accessor's field Boolean",
         true,
         {{"{ 54, static_cast<::fixpp::dict::field_data_type>(11)",
           "{ 54, static_cast<::fixpp::dict::field_data_type>(12)"}}},
        {"Fields.hpp gives the decimal accessor's field String",
         true,
         {{"{ 44, static_cast<::fixpp::dict::field_data_type>(5)",
           "{ 44, static_cast<::fixpp::dict::field_data_type>(13)"}}},
    };
}

// A synthetic Reify.hpp in the shipped grammar: owning_NewOrderSingle with
// the same accessors as the synthetic message (three scalars, a decimal and
// a group accessor), its out-of-line definitions and its traits.
constexpr std::string_view kSyntheticReify = R"(// header comment
#pragma once
#include <cstdint>
namespace fixpp::vx {
class owning_NewOrderSingle {
public:
    static constexpr ::std::string_view msg_type_v = "D";
    static constexpr ::fixpp::dict::application_version version_v =
        ::fixpp::dict::application_version::v44;
    owning_NewOrderSingle(owning_NewOrderSingle const&) = delete;
    owning_NewOrderSingle& operator=(owning_NewOrderSingle const&) = delete;
    owning_NewOrderSingle(owning_NewOrderSingle &&) noexcept;
    owning_NewOrderSingle& operator=(owning_NewOrderSingle &&) noexcept;
    ~owning_NewOrderSingle() = default;
    [[nodiscard]] static constexpr ::fixpp::dict::application_version
    which() noexcept { return version_v; }
    [[nodiscard]] static ::fixpp::core::expected_t<owning_NewOrderSingle>
    from_view(::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index> const& src, ::std::pmr::memory_resource* mr);
    [[nodiscard]] ::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index> const&
    view() const noexcept [[clang::lifetimebound]];
    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::wire::field_view>
    field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]
    { return view().get(tag); }
    [[nodiscard]] inline ::fixpp::core::expected_t<::std::string_view>
    account() const noexcept [[clang::lifetimebound]]
    { return ::fixpp::dict::decode_field<::std::string_view>(view().template get<1>()); }
    [[nodiscard]] inline ::fixpp::core::expected_t<::std::string_view>
    cl_ord_id() const noexcept [[clang::lifetimebound]]
    { return ::fixpp::dict::decode_field<::std::string_view>(view().template get<11>()); }
    [[nodiscard]] inline ::fixpp::core::expected_t<char>
    side() const noexcept
    { return ::fixpp::dict::decode_field<char>(view().template get<54>()); }
    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::decimal_t>
    price(::std::pmr::memory_resource* mr) const noexcept
    { auto fv = view().template get<44>(); if (!fv) return ::std::unexpected{fv.error()};
      return ::fixpp::decimal_t::parse(fv->bytes(), mr); }
    [[nodiscard]] inline ::fixpp::wire::group_view<::fixpp::vx::groups::G_78>
    allocs() const noexcept [[clang::lifetimebound]]
    { return view().template group<78, ::fixpp::vx::groups::G_78>(); }
private:
    explicit owning_NewOrderSingle(::std::pmr::memory_resource* mr) : bytes_(mr) {}
    ::std::pmr::vector<::std::byte>                    bytes_;      // owned arena copy
    mutable ::std::optional<::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index>> view_cache_;  // lazy, nullopt after move
};
inline owning_NewOrderSingle::owning_NewOrderSingle(owning_NewOrderSingle && other) noexcept
    : bytes_(::std::move(other.bytes_))
    , view_cache_(::std::nullopt) {
    other.view_cache_ = ::std::nullopt;  // reset source cache (I-9/AC-R4)
}
inline owning_NewOrderSingle& owning_NewOrderSingle::operator=(owning_NewOrderSingle && other) noexcept {
    if (this != &other) {
        bytes_            = ::std::move(other.bytes_);
        view_cache_       = ::std::nullopt;  // reset dest cache (I-9/AC-R4)
        other.view_cache_ = ::std::nullopt;  // reset source cache
    }
    return *this;
}
inline ::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index> const&
owning_NewOrderSingle::view() const noexcept {
    if (!view_cache_) {
        ::fixpp::wire::pmr_carry_buffer carry{0, bytes_.get_allocator().resource()};
        ::fixpp::wire::Framer framer{};
        ::fixpp::wire::frame_view out_arr[1]{};
        auto framed = framer.feed(
            ::std::span<const ::std::byte>{bytes_.data(), bytes_.size()},
            carry, ::std::span<::fixpp::wire::frame_view>{out_arr, 1});
        if (framed && !framed->empty()) {
            view_cache_.emplace((*framed)[0], bytes_.get_allocator().resource());
        } else {
            view_cache_.emplace();
        }
    }
    return *view_cache_;
}
inline ::fixpp::core::expected_t<owning_NewOrderSingle>
owning_NewOrderSingle::from_view(::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index> const& src, ::std::pmr::memory_resource* mr) {
    try {
        owning_NewOrderSingle owned{mr};
        auto const sb = src.bytes();  // full validated frame span
        owned.bytes_.assign(sb.begin(), sb.end());  // deep copy into mr arena
        return owned;  // move (custom noexcept move ctor)
    } catch (::std::bad_alloc const&) {
        return ::std::unexpected{::fixpp::core::error::dict_reify_oom};
    }
}
}  // namespace fixpp::vx
template <>
struct fixpp::dict::owning_message_traits<fixpp::vx::NewOrderSingle> {
    using type = fixpp::vx::owning_NewOrderSingle;
};
static_assert(::std::is_same_v<
    fixpp::dict::owning_message_t<fixpp::vx::NewOrderSingle>,
    fixpp::vx::owning_NewOrderSingle>);  // AC-G7a
)";

// Runtime-lookup shapes in the Reify text; each must fail the grammar.
constexpr seeded kReifySeeded[] = {
    {"owning accessor's tag looked up by name at runtime", "view().template get<1>()",
     "view().get(tag_by_name(\"Account\"))"},
    {"owning accessor's tag read from a member", "view().template get<54>()",
     "view().get(side_tag_)"},
    {"namespace-scope lookup table in Reify.hpp", "namespace fixpp::vx {\n",
     "namespace fixpp::vx {\ninline constexpr ::std::uint16_t kTags[] = {54};\n"},
    {"an out-of-line definition changed", "auto const sb = src.bytes();",
     "auto const sb = src.bytes_for(dict_);"},
    {"the traits specialisation missing",
     "template <>\nstruct fixpp::dict::owning_message_traits<fixpp::vx::NewOrderSingle> {\n", ""},
};

// Identity mutations of the Reify text: each keeps the grammar and the tag
// set, so only the identity check can catch it.
std::vector<identity_seed> reify_identity_seeds() {
    return {
        {"two same-type owning accessors' tag literals swapped",
         false,
         {{"view().template get<1>()", "@SWAP@"},
          {"view().template get<11>()", "view().template get<1>()"},
          {"@SWAP@", "view().template get<11>()"}}},
        {"an owning scalar's return and decode_field<T> type changed together",
         false,
         {{"::fixpp::core::expected_t<char>\n    side()",
           "::fixpp::core::expected_t<bool>\n    side()"},
          {"decode_field<char>(view().template get<54>())",
           "decode_field<bool>(view().template get<54>())"}}},
        {"an owning scalar renamed, tag and type kept",
         false,
         {{"    side() const", "    sides() const"}}},
        {"an owning group accessor named without dropping No",
         false,
         {{"allocs()", "no_allocs()"}}},
    };
}

// A census over well-formed text reports a problem as an unclassified line,
// or as a class that does not carry exactly one passthrough (and, for a
// message, one view()).
bool census_rejects(census const& c) {
    if (!c.unclassified.empty()) return true;
    for (auto const& [name, rec] : c.classes) {
        if (rec.passthroughs != 1) return true;
        if (!rec.is_group && rec.views != 1) return true;
    }
    return false;
}

// Each codegen version, the namespace it generates, and the dictionary it is
// generated from (cmake/Codegen.cmake's --xml list; build_ir picks the loader
// by the root element).
struct version_input {
    std::string_view ns;
    std::string_view xml;  // under FIXPP_DICT_DATA_DIR
    bool orchestra;
};

std::vector<version_input> versions() {
    std::vector<version_input> v{{"v42", "FIX42.xml", false},
                                 {"v44", "FIX44.xml", false},
                                 {"v50sp2", "FIX50SP2.xml", false},
                                 {"vt11", "FIXT11.xml", false}};
#ifdef FIXPP_D008_CENSUS_HAS_VLATEST
    v.push_back({"vlatest", "orchestra/OrchestraFIXLatest.xml", true});
#endif
    return v;
}

// The field name of every tag Fields.hpp declares that the version's
// dictionary names, copied out before the dictionary and its arena go. A tag
// the dictionary leaves unnamed gets no entry, which the identity check
// reports for any accessor of that tag.
tag_names load_names(version_input const& v, field_rows const& f, std::string& error) {
    tag_names out;
    std::set<std::uint32_t> tags;
    for (auto const& [m, rows] : f.messages) {
        for (auto const& [tag, type] : rows) tags.insert(tag);
    }
    for (auto const& [g, rows] : f.groups) {
        tags.insert(g);
        for (auto const& [tag, type] : rows) tags.insert(tag);
    }
    std::string const path = std::string{FIXPP_DICT_DATA_DIR} + "/" + std::string{v.xml};
    try {
        std::pmr::monotonic_buffer_resource arena;
        fixpp::dict::Dictionary const dict = v.orchestra
                                                 ? fixpp::dict::OrchestraLoader{}.load(path, &arena)
                                                 : fixpp::dict::XmlLoader{}.load(path, &arena);
        for (std::uint32_t t : tags) {
            if (t > 0xFFFFU) continue;
            std::string_view const name = dict.field_name(static_cast<std::uint16_t>(t));
            if (!name.empty()) out.emplace(t, std::string{name});
        }
    } catch (std::exception const& e) {
        error = path + ": " + e.what();
    }
    return out;
}

TEST(CodegenAccessorResolutionCensus, EveryTypedAccessorResolvesTagAndTypeAtCompileTime) {
    // ---- Guard: the checks accept the real shapes and reject runtime
    // resolution and wrong identities, before any shipped header is read.
    {
        field_rows const gf = read_fields(split(kSyntheticFields));
        ASSERT_TRUE(gf.malformed.empty()) << gf.malformed.front();
        ASSERT_EQ(gf.rows, 10U) << "the synthetic Fields text holds ten rows";
        tag_names const gn = synthetic_names();
        census const good = run_census("vx", split(kSynthetic));
        ASSERT_TRUE(good.unclassified.empty())
            << "the synthetic well-formed text is rejected: " << good.unclassified.front();
        ASSERT_FALSE(census_rejects(good));
        ASSERT_EQ(good.typed, 10U) << "the synthetic text holds ten typed accessors";
        auto const good_pop = population_problems(good, gf);
        ASSERT_TRUE(good_pop.empty()) << first_of(good_pop);
        auto const good_id = identity_problems(good, gf, gn);
        ASSERT_TRUE(good_id.empty()) << first_of(good_id);

        for (auto const& m : kSeeded) {
            std::string const text{kSynthetic};
            ASSERT_NE(text.find(m.from), std::string::npos) << m.what << ": anchor not in the text";
            census const bad = run_census("vx", split(replace_all(text, m.from, m.to)));
            EXPECT_TRUE(census_rejects(bad)) << "seeded mutation accepted: " << m.what;
        }
        // A second passthrough-shaped member is rejected by the per-class count.
        std::string twice{kSynthetic};
        std::string_view const pt =
            "    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::wire::field_view>\n"
            "    field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]\n"
            "    { return view_.get(tag); }\n";
        ASSERT_NE(twice.find(pt), std::string::npos);
        twice.insert(twice.find(pt), pt);
        EXPECT_TRUE(census_rejects(run_census("vx", split(twice))))
            << "a second field_value passthrough is accepted";

        // Each identity mutation must pass the grammar and the population
        // check (a spurious hit for both) and fail the identity check.
        for (auto const& m : identity_seeds()) {
            std::string msgs{kSynthetic};
            std::string flds{kSyntheticFields};
            std::string& target = m.fields ? flds : msgs;
            for (auto const& [from, to] : m.edits) {
                ASSERT_NE(target.find(from), std::string::npos)
                    << m.what << ": anchor not in the text: " << from;
                target = replace_all(target, from, to);
            }
            ASSERT_NE(m.fields ? flds : msgs, m.fields ? kSyntheticFields : kSynthetic)
                << m.what << ": the mutation changed nothing";
            field_rows const bf = read_fields(split(flds));
            census const bad = run_census("vx", split(msgs));
            EXPECT_FALSE(census_rejects(bad))
                << m.what << ": the grammar rejects it, so this arm cannot show the identity check";
            EXPECT_TRUE(population_problems(bad, bf).empty())
                << m.what
                << ": the population check rejects it, so this arm cannot show the "
                   "identity check";
            EXPECT_FALSE(identity_problems(bad, bf, gn).empty())
                << "identity mutation accepted: " << m.what;
        }

        // The same three checks over the synthetic Reify.hpp.
        census const rgood = run_reify_census("vx", split(kSyntheticReify));
        ASSERT_TRUE(rgood.unclassified.empty())
            << "the synthetic Reify text is rejected: " << rgood.unclassified.front();
        ASSERT_FALSE(reify_rejects(rgood));
        ASSERT_EQ(rgood.typed, 5U) << "the synthetic Reify text holds five typed accessors";
        auto const rgood_pop = population_problems(rgood, gf, false);
        ASSERT_TRUE(rgood_pop.empty()) << first_of(rgood_pop);
        auto const rgood_id = identity_problems(rgood, gf, gn);
        ASSERT_TRUE(rgood_id.empty()) << first_of(rgood_id);
        for (auto const& m : kReifySeeded) {
            std::string const text{kSyntheticReify};
            ASSERT_NE(text.find(m.from), std::string::npos) << m.what << ": anchor not in the text";
            census const bad = run_reify_census("vx", split(replace_all(text, m.from, m.to)));
            EXPECT_TRUE(reify_rejects(bad)) << "seeded Reify mutation accepted: " << m.what;
        }
        for (auto const& m : reify_identity_seeds()) {
            std::string text{kSyntheticReify};
            for (auto const& [from, to] : m.edits) {
                ASSERT_NE(text.find(from), std::string::npos)
                    << m.what << ": anchor not in the text: " << from;
                text = replace_all(text, from, to);
            }
            ASSERT_NE(text, kSyntheticReify) << m.what << ": the mutation changed nothing";
            census const bad = run_reify_census("vx", split(text));
            EXPECT_FALSE(reify_rejects(bad))
                << m.what << ": the grammar rejects it, so this arm cannot show the identity check";
            EXPECT_TRUE(population_problems(bad, gf, false).empty())
                << m.what
                << ": the population check rejects it, so this arm cannot show the "
                   "identity check";
            EXPECT_FALSE(identity_problems(bad, gf, gn).empty())
                << "Reify identity mutation accepted: " << m.what;
        }
    }

    // ---- The census over every version's shipped Messages.hpp.
    std::size_t total_typed = 0;
    for (version_input const& v : versions()) {
        std::string const ns{v.ns};
        SCOPED_TRACE("version " + ns);
        std::string const dir = std::string{FIXPP_CODEGEN_INC} + "/fixpp/" + ns + "/";
        bool ok_m = false;
        bool ok_f = false;
        auto const msg_lines = read_lines(dir + "Messages.hpp", ok_m);
        auto const fld_lines = read_lines(dir + "Fields.hpp", ok_f);
        ASSERT_TRUE(ok_m && !msg_lines.empty()) << "missing or empty: " << dir << "Messages.hpp";
        ASSERT_TRUE(ok_f && !fld_lines.empty()) << "missing or empty: " << dir << "Fields.hpp";

        // Check 1: the closed grammar.
        census const c = run_census(ns, msg_lines);
        EXPECT_TRUE(c.unclassified.empty())
            << c.unclassified.size()
            << " line(s) outside the compile-time accessor grammar; first: "
            << (c.unclassified.empty() ? std::string{} : c.unclassified.front());
        for (auto const& [name, rec] : c.classes) {
            EXPECT_EQ(rec.passthroughs, 1) << name << ": field_value passthrough count";
            if (!rec.is_group) {
                EXPECT_EQ(rec.views, 1) << name << ": view() count";
            }
        }
        ASSERT_GT(c.typed, 0U) << "no typed accessor classified: the walk reached nothing";

        // Check 2: population against Fields.hpp.
        field_rows const f = read_fields(fld_lines);
        EXPECT_TRUE(f.malformed.empty())
            << "Fields.hpp rows outside the FieldRef grammar; first: "
            << (f.malformed.empty() ? std::string{} : f.malformed.front());
        ASSERT_GT(f.rows, 0U) << "Fields.hpp yielded no FieldRef row";
        auto const pop = population_problems(c, f);
        EXPECT_TRUE(pop.empty()) << pop.size()
                                 << " population mismatch(es); first:" << first_of(pop);

        // Check 3: identity against the version's dictionary and the mapping.
        std::string load_error;
        tag_names const names = load_names(v, f, load_error);
        ASSERT_TRUE(load_error.empty()) << "dictionary load failed: " << load_error;
        ASSERT_FALSE(names.empty()) << "the dictionary named no Fields.hpp tag";
        auto const ids = identity_problems(c, f, names);
        EXPECT_TRUE(ids.empty()) << ids.size()
                                 << " accessor identity mismatch(es); first:" << first_of(ids);
        total_typed += c.typed;

        // The same three checks over the version's Reify.hpp owning_<Msg>
        // accessors, against the same Fields.hpp rows and names.
        bool ok_r = false;
        auto const reify_lines = read_lines(dir + "Reify.hpp", ok_r);
        ASSERT_TRUE(ok_r && !reify_lines.empty()) << "missing or empty: " << dir << "Reify.hpp";
        census const rc = run_reify_census(ns, reify_lines);
        EXPECT_TRUE(rc.unclassified.empty())
            << rc.unclassified.size() << " Reify.hpp line(s) outside the grammar; first: "
            << (rc.unclassified.empty() ? std::string{} : rc.unclassified.front());
        for (auto const& [name, rec] : rc.classes) {
            EXPECT_EQ(rec.passthroughs, 1) << "owning_" << name << ": field_value count";
            EXPECT_EQ(rec.views, 1) << "owning_" << name << ": view() declaration count";
            EXPECT_EQ(rec.traits, 1) << "owning_" << name << ": owning_message_traits count";
        }
        ASSERT_GT(rc.typed, 0U) << "no owning_ accessor classified: the walk reached nothing";
        auto const rpop = population_problems(rc, f, false);
        EXPECT_TRUE(rpop.empty()) << rpop.size()
                                  << " Reify.hpp population mismatch(es); first:" << first_of(rpop);
        auto const rids = identity_problems(rc, f, names);
        EXPECT_TRUE(rids.empty()) << rids.size()
                                  << " Reify.hpp accessor identity mismatch(es); first:"
                                  << first_of(rids);
        total_typed += rc.typed;
    }
    EXPECT_GT(total_typed, 0U);
}

}  // namespace
