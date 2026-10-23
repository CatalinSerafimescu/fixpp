// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/accessor_resolution_census_test.cpp
//
// 094-v1-tier0-bookkeeping FR-022c — D-008's "zero-runtime-cost field
// lookup" clause, witnessed over the SHIPPED generated text.
//
// THE PROPERTY. A generated typed field accessor resolves its standard
// field's tag and value type at compile time: the tag is an integer literal
// (the template argument of `view_.template get<N>()` / `group<N, G_N>()`,
// or the literal argument of `::fixpp::wire::get(ctx_.span, N, ...)` and of
// `nested_group_slices(..., N, ...)`), and the value type is fixed by the
// emitted text (`decode_field<T>` with T equal to the accessor's
// `expected_t<T>`, `decimal_t::parse` for a decimal_t accessor, or the
// group flyweight type). It never consults a runtime `Dictionary`, a
// name->tag or tag->type table, or any dictionary lookup function.
// `.specify/2c-codegen.md`: "Eager codegen with no runtime dictionary path."
//
// THE MECHANISM: a closed line grammar, not a forbidden-token list. Every
// line of each version's `Messages.hpp` must be one of the shapes below;
// every member function must match one of the accessor templates in full
// (signature and body), or one of the two named exclusions. Anything else —
// a new include, a namespace-scope table, a member without
// `[[nodiscard]] inline`, a body that computes its tag — is reported as an
// unclassified line and fails the cell. A deliberate emitter change that
// adds a new accessor shape must therefore be added here too, by hand.
//
// EXCLUSIONS. `field_value(::std::uint16_t tag)` is the runtime-tag
// passthrough (its tag is the caller's), and `view()` returns the bound
// view; neither is a typed field accessor. Each is accepted only with its
// exact signature and body, and only once per class, so a second member of
// that shape cannot carry a lookup past the census.
//
// SCOPE. `Messages.hpp` of v42, v44, v50sp2 and vt11, and of vlatest when
// the build generates it (FIXPP_CODEGEN_FIX_LATEST, which defines
// FIXPP_D008_CENSUS_HAS_VLATEST). A listed version whose header is missing
// fails; it is never skipped. `Reify.hpp`'s `owning_<Msg>` accessors
// (emit_reify.cpp) are not in the population.
//
// POPULATION CONTROL. For every class, the set of tags its typed accessors
// bind must equal the set `Fields.hpp` declares for it: a message's
// `<Msg>_fields[]` rows with group_no_tag 0, and a group flyweight G_K's
// rows with group_no_tag K across every message. `Fields.hpp` is written by
// a different emitter (emit_fields.cpp) from the same IR, so this proves the
// walk reached every accessor the version declares; it says nothing about
// dictionary completeness, which is the dictionary censuses' subject.
// A guard runs first: the classifier must accept each real shape and reject
// seeded runtime-lookup shapes, so a classifier that accepts everything
// cannot pass.
//
// RE-DERIVE the population by hand: for one version,
//   grep -c '\[\[nodiscard\]\] inline' build/<preset>/_codegen/include/fixpp/<v>/Messages.hpp
// counts the member functions the census classifies (accessors plus the two
// exclusions per class).

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef FIXPP_CODEGEN_INC
#error "FIXPP_CODEGEN_INC must be set by CMake target_compile_definitions"
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

// The value types decode_field<T> is emitted with (emit_messages.cpp,
// kind_cpp_type). A T outside this set is unclassified.
constexpr std::string_view kScalarTypes[] = {"::std::string_view", "char", "bool",
                                             "::std::int32_t"};

constexpr std::string_view kMsgView =
    "::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index>";

// ---------------------------------------------------------------------------
// One member function: the return type (after "[[nodiscard]] inline "), the
// name line, and the body lines, each stripped, the body joined by '\n'.
// ---------------------------------------------------------------------------
enum class kind { scalar, decimal, group, nested, passthrough, view, unclassified };

struct classified {
    kind k = kind::unclassified;
    std::uint32_t tag = 0;
};

classified classify_message_member(std::string_view ns, std::string_view ret, std::string_view name,
                                   std::string_view body) {
    if (ret == "::fixpp::core::expected_t<::fixpp::wire::field_view>" &&
        name == "field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]" &&
        body == "{ return view_.get(tag); }") {
        return {kind::passthrough, 0};
    }
    if (ret == std::string{kMsgView} + " const&" &&
        name == "view() const noexcept [[clang::lifetimebound]]" && body == "{ return view_; }") {
        return {kind::view, 0};
    }
    for (std::string_view t : kScalarTypes) {
        if (ret != "::fixpp::core::expected_t<" + std::string{t} + ">") continue;
        std::string const name_pat = t == "::std::string_view"
                                         ? "@I() const noexcept [[clang::lifetimebound]]"
                                         : "@I() const noexcept";
        caps c;
        if (match(name_pat, name) && match("{ return ::fixpp::dict::decode_field<" +
                                               std::string{t} + ">(view_.template get<@N>()); }",
                                           body, c)) {
            return {kind::scalar, c.nums.front()};
        }
    }
    if (ret == "::fixpp::core::expected_t<::fixpp::decimal_t>") {
        caps c;
        if (match("@I(::std::pmr::memory_resource* mr) const noexcept", name) &&
            match("{ auto fv = view_.template get<@N>(); if (!fv) return "
                  "::std::unexpected{fv.error()};\n"
                  "return ::fixpp::decimal_t::parse(fv->bytes(), mr); }",
                  body, c)) {
            return {kind::decimal, c.nums.front()};
        }
    }
    {
        std::string const groups = "::fixpp::" + std::string{ns} + "::groups::G_";
        caps c;
        if (match("::fixpp::wire::group_view<" + groups + "@N>", ret, c) &&
            match("@I() const noexcept [[clang::lifetimebound]]", name) &&
            match("{ return view_.template group<@N, " + groups + "@N>(); }", body, c) &&
            all_equal(c.nums)) {
            return {kind::group, c.nums.front()};
        }
    }
    return {};
}

classified classify_group_member(std::string_view ret, std::string_view name,
                                 std::string_view body) {
    if (ret == "::fixpp::core::expected_t<::fixpp::wire::field_view>" &&
        name == "field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]" &&
        body == "{ return ::fixpp::wire::get(ctx_.span, tag, ctx_.hooks, ctx_.gen); }") {
        return {kind::passthrough, 0};
    }
    for (std::string_view t : kScalarTypes) {
        if (ret != "::fixpp::core::expected_t<" + std::string{t} + ">") continue;
        std::string const name_pat = t == "::std::string_view"
                                         ? "@I() const noexcept [[clang::lifetimebound]]"
                                         : "@I() const noexcept";
        caps c;
        if (match(name_pat, name) &&
            match("{ return ::fixpp::dict::decode_field<" + std::string{t} +
                      ">(::fixpp::wire::get(ctx_.span, @N, ctx_.hooks, ctx_.gen)); }",
                  body, c)) {
            return {kind::scalar, c.nums.front()};
        }
    }
    if (ret == "::fixpp::core::expected_t<::fixpp::decimal_t>") {
        caps c;
        if (match("@I(::std::pmr::memory_resource* mr) const noexcept", name) &&
            match("{ auto fv = ::fixpp::wire::get(ctx_.span, @N, ctx_.hooks, ctx_.gen); if (!fv) "
                  "return ::std::unexpected{fv.error()};\n"
                  "return ::fixpp::decimal_t::parse(fv->bytes(), mr); }",
                  body, c)) {
            return {kind::decimal, c.nums.front()};
        }
    }
    {
        caps c;
        if (match("::fixpp::wire::group_view<G_@N>", ret, c) &&
            match("@I() const noexcept [[clang::lifetimebound]]", name) &&
            match("{ if (ctx_.parent_cache_owner == nullptr) return {};\n"
                  "auto const r = ctx_.parent_cache_owner->nested_group_slices("
                  "ctx_.outer_occurrence_id, ctx_.span.size(), @N, ctx_.hooks, ctx_.gen, "
                  "ctx_.group_ctx);\n"
                  "::fixpp::wire::entry_context child_ctx = ctx_;\n"
                  "child_ctx.group_ctx = ctx_.group_ctx.pushed(@N);\n"
                  "return ::fixpp::wire::group_view<G_@N>{r.slices, child_ctx, r.alloc_failed}; }",
                  body, c) &&
            all_equal(c.nums)) {
            return {kind::nested, c.nums.front()};
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// The census over one Messages.hpp text.
// ---------------------------------------------------------------------------
struct class_record {
    bool is_group = false;
    std::vector<std::uint32_t> tags;  // typed accessors' tags, in order
    int passthroughs = 0;
    int views = 0;
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
                    rec.tags.push_back(r.tag);
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
// The independent side: Fields.hpp's FieldRef rows.
//   message name -> its rows' tags with group_no_tag 0
//   group no_tag -> its member rows' tags, across every message
// ---------------------------------------------------------------------------
struct field_sets {
    std::map<std::string, std::set<std::uint32_t>> messages;
    std::map<std::uint32_t, std::set<std::uint32_t>> groups;
    std::size_t rows = 0;
    std::vector<std::string> malformed;
};

field_sets read_fields(std::vector<std::string> const& lines) {
    field_sets out;
    std::string cur;
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
        if (nums.size() != 9 || !s.starts_with("{ ") || !s.ends_with(" },")) {
            out.malformed.push_back("line " + std::to_string(i + 1) + ": " + lines[i]);
            continue;
        }
        ++out.rows;
        if (nums[4] == 0) {
            out.messages[cur].insert(nums[0]);
        } else {
            out.groups[nums[4]].insert(nums[0]);
        }
    }
    return out;
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

// A synthetic Messages.hpp in the shipped grammar: one group flyweight with
// a scalar, a decimal and a nested-group accessor, and one message with a
// scalar, a decimal and a group accessor. The guard checks it is accepted,
// then that each seeded mutation of it is not.
constexpr std::string_view kSynthetic = R"(// header comment
#pragma once
#include <cstdint>
namespace fixpp::vx::groups {  // shared repeating-group flyweights (AC-G5/AC-G6)
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
    nested_parties() const noexcept [[clang::lifetimebound]]
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

struct seeded {
    std::string_view what;
    std::string_view from;
    std::string_view to;
};

// Each mutation turns one accessor (or one line) into a runtime resolution,
// or adds a surface a lookup could hide behind. Every one must be rejected.
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

std::vector<std::string> versions() {
    std::vector<std::string> v{"v42", "v44", "v50sp2", "vt11"};
#ifdef FIXPP_D008_CENSUS_HAS_VLATEST
    v.emplace_back("vlatest");
#endif
    return v;
}

TEST(CodegenAccessorResolutionCensus, EveryTypedAccessorResolvesTagAndTypeAtCompileTime) {
    // ---- Guard: the classifier accepts the real shapes and rejects runtime
    // resolution, before any shipped header is read.
    {
        census const good = run_census("vx", split(kSynthetic));
        ASSERT_TRUE(good.unclassified.empty())
            << "the synthetic well-formed text is rejected: " << good.unclassified.front();
        ASSERT_FALSE(census_rejects(good));
        ASSERT_EQ(good.typed, 7U) << "the synthetic text holds seven typed accessors";
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
    }

    // ---- The census over every version's shipped Messages.hpp.
    std::size_t total_typed = 0;
    for (std::string const& v : versions()) {
        SCOPED_TRACE("version " + v);
        std::string const dir = std::string{FIXPP_CODEGEN_INC} + "/fixpp/" + v + "/";
        bool ok_m = false;
        bool ok_f = false;
        auto const msg_lines = read_lines(dir + "Messages.hpp", ok_m);
        auto const fld_lines = read_lines(dir + "Fields.hpp", ok_f);
        ASSERT_TRUE(ok_m && !msg_lines.empty()) << "missing or empty: " << dir << "Messages.hpp";
        ASSERT_TRUE(ok_f && !fld_lines.empty()) << "missing or empty: " << dir << "Fields.hpp";

        census const c = run_census(v, msg_lines);
        EXPECT_TRUE(c.unclassified.empty())
            << c.unclassified.size()
            << " line(s) outside the compile-time accessor grammar; first: "
            << (c.unclassified.empty() ? std::string{} : c.unclassified.front());
        for (auto const& [name, rec] : c.classes) {
            EXPECT_EQ(rec.passthroughs, 1) << name << ": field_value passthrough count";
            if (!rec.is_group) EXPECT_EQ(rec.views, 1) << name << ": view() count";
        }
        ASSERT_GT(c.typed, 0U) << "no typed accessor classified: the walk reached nothing";

        // ---- Population control against Fields.hpp.
        field_sets const f = read_fields(fld_lines);
        EXPECT_TRUE(f.malformed.empty())
            << "Fields.hpp rows outside the FieldRef grammar; first: "
            << (f.malformed.empty() ? std::string{} : f.malformed.front());
        ASSERT_GT(f.rows, 0U) << "Fields.hpp yielded no FieldRef row";
        std::size_t expected_typed = 0;
        for (auto const& [m, tags] : f.messages) {
            expected_typed += tags.size();
            auto const it = c.classes.find(m);
            if (it == c.classes.end() || it->second.is_group) {
                ADD_FAILURE() << "Fields.hpp declares " << m
                              << "_fields[] but Messages.hpp has no class " << m;
                continue;
            }
            std::set<std::uint32_t> const got(it->second.tags.begin(), it->second.tags.end());
            EXPECT_EQ(got.size(), it->second.tags.size()) << m << ": a tag bound by two accessors";
            EXPECT_EQ(got, tags) << m << ": accessor tags differ from its top-level FieldRef rows";
        }
        for (auto const& [no_tag, tags] : f.groups) {
            expected_typed += tags.size();
            std::string const g = "G_" + std::to_string(no_tag);
            auto const it = c.classes.find(g);
            if (it == c.classes.end() || !it->second.is_group) {
                ADD_FAILURE() << "Fields.hpp has rows under group " << no_tag
                              << " but Messages.hpp has no " << g;
                continue;
            }
            std::set<std::uint32_t> const got(it->second.tags.begin(), it->second.tags.end());
            EXPECT_EQ(got.size(), it->second.tags.size()) << g << ": a tag bound by two accessors";
            EXPECT_EQ(got, tags) << g << ": accessor tags differ from its member FieldRef rows";
        }
        for (auto const& [name, rec] : c.classes) {
            bool const known =
                rec.is_group
                    ? f.groups.contains(static_cast<std::uint32_t>(std::stoul(name.substr(2))))
                    : f.messages.contains(name);
            if (!known && !rec.tags.empty()) {
                ADD_FAILURE() << name << " has typed accessors but no FieldRef rows in Fields.hpp";
            }
        }
        EXPECT_EQ(c.typed, expected_typed)
            << "typed accessors classified vs FieldRef-derived count";
        total_typed += c.typed;
    }
    EXPECT_GT(total_typed, 0U);
}

}  // namespace
