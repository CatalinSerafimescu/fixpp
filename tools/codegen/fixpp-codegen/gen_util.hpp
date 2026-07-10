// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/gen_util.hpp
//
// 003-dictionary-codegen — T023/T024 shared, deterministic, locale-free
// name/type helpers used by every emitter. Header-only (no new .cpp / no
// CMakeLists churn — same discipline as template_writer.hpp). Build-only host
// tool ([const §III.5]).
//
//  * to_accessor  — FIX field name -> snake_case C++ accessor identifier
//                    (ClOrdID -> cl_ord_id; SecurityIDSource ->
//                    security_id_source). C++-keyword guarded.
//  * to_identifier — FIX message/group name -> a valid C++ class identifier
//                    (case preserved; leading digit / bad chars sanitised).
//  * kind_of      — field_data_type -> the typed-accessor category the
//                    [2c §4.7] template emits (string/char/bool/int/decimal;
//                    decimal is the PMR route, NOT field_traits — AC-G4/I-16).
#pragma once
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/field_ref.hpp>
#include <string>
#include <string_view>
#include <unordered_set>

namespace fixpp::codegen {

inline bool is_cpp_keyword(std::string_view s) {
    static std::unordered_set<std::string_view> const kw = {
        "alignas",       "alignof",     "and",
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
    return kw.contains(s);
}

inline std::string to_accessor(std::string_view fix_name) {
    std::string out;
    out.reserve(fix_name.size() + 8);
    auto is_upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    auto is_lower = [](char c) { return c >= 'a' && c <= 'z'; };
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    auto is_alnum = [&](char c) { return is_upper(c) || is_lower(c) || is_digit(c); };
    for (std::size_t i = 0; i < fix_name.size(); ++i) {
        char const c = fix_name[i];
        if (!is_alnum(c)) {
            if (!out.empty() && out.back() != '_') {
                out.push_back('_');
            }
            continue;
        }
        if (is_upper(c) && !out.empty() && out.back() != '_') {
            char const prev = fix_name[i - 1];
            bool const next_lower = i + 1 < fix_name.size() && is_lower(fix_name[i + 1]);
            if (is_lower(prev) || is_digit(prev) || (is_upper(prev) && next_lower)) {
                out.push_back('_');
            }
        }
        out.push_back(is_upper(c) ? static_cast<char>(c - 'A' + 'a') : c);
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    std::size_t b = 0;
    while (b < out.size() && out[b] == '_') {
        ++b;
    }
    out.erase(0, b);
    if (out.empty()) {
        out = "field";
    }
    if (out[0] >= '0' && out[0] <= '9') {
        out.insert(out.begin(), '_');
    }
    if (is_cpp_keyword(out)) {
        out.push_back('_');
    }
    return out;
}

inline std::string to_identifier(std::string_view fix_name) {
    std::string out;
    out.reserve(fix_name.size() + 1);
    auto ok = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_';
    };
    for (char const c : fix_name) {
        out.push_back(ok(c) ? c : '_');
    }
    if (out.empty()) {
        out = "Msg";
    }
    if (out[0] >= '0' && out[0] <= '9') {
        out.insert(out.begin(), '_');
    }
    if (is_cpp_keyword(out)) {
        out.push_back('_');
    }
    return out;
}

// The group accessor conventionally drops the "No" NumInGroup prefix
// (NoLegs -> legs, NoPartyIDs -> party_i_ds); the nested flyweight class is
// <Stripped>Group. Deterministic and self-consistent with the generated
// golden (T042) — exact FIX singularisation ("Leg") is cosmetic and not
// shape-pinned (the shape oracle pins sizeof + owning_message_traits, AC-G7/G7a).
inline std::string_view strip_no_prefix(std::string_view fix_name) {
    if (fix_name.size() > 2 && fix_name[0] == 'N' && fix_name[1] == 'o' && fix_name[2] >= 'A' &&
        fix_name[2] <= 'Z') {
        return fix_name.substr(2);
    }
    return fix_name;
}

enum class TypeKind { String, Char, Bool, Int32, Decimal, Skip };

inline TypeKind kind_of(fixpp::dict::field_data_type t) noexcept {
    using D = fixpp::dict::field_data_type;
    switch (t) {
        case D::Price:
        case D::Qty:
        case D::Amt:
        case D::PriceOffset:
        case D::Percentage:
        case D::Float:
            return TypeKind::Decimal;
        case D::Char:
        case D::MultiCharValue:
            return TypeKind::Char;
        case D::Boolean:
            return TypeKind::Bool;
        case D::Int:
        case D::Length:
        case D::SeqNum:
        case D::NumInGroup:
        case D::DayOfMonth:
            return TypeKind::Int32;
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
            return TypeKind::String;
        case D::DialectExtension:
            return TypeKind::Skip;
    }
    return TypeKind::Skip;
}

// The flyweight binding target — fully-qualified `MessageView<Index>`.
// Shared by emit_messages.cpp (was `kView`) and emit_reify.cpp (was `kMV`).
inline constexpr std::string_view kMessageView =
    "::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index>";

// 067-codegen-writer-emitter T006/R7: which wire::body_builder overload /
// entry_handle set_* variant serves a given TypeKind at serialize time.
// String/Char/Int32/Decimal route to the like-named overload; Bool ALSO
// routes to Char — FIX Boolean is the literal wire byte `Y`/`N`
// (`x ? 'Y' : 'N'`), never the int64 `1`/`0` path (FR-007a). Skip
// (DialectExtension) has no setter — the emitter omits it entirely. The
// Length+Data pair is NOT a TypeKind of its own: the DATA half's `kind_of()`
// is already String (auto-derived Length is emitted alongside it, coupled,
// by the emitter's Length+Data pairing logic — data-model.md §1.1/§3), so
// this map does not need a distinct case for it.
enum class BuilderCallKind { String, Char, Int32, Decimal, None };

inline BuilderCallKind builder_call_kind(TypeKind k) noexcept {
    switch (k) {
        case TypeKind::String:
            return BuilderCallKind::String;
        case TypeKind::Char:
            return BuilderCallKind::Char;
        case TypeKind::Bool:
            return BuilderCallKind::Char;  // Y/N via the char overload — FR-007a
        case TypeKind::Int32:
            return BuilderCallKind::Int32;
        case TypeKind::Decimal:
            return BuilderCallKind::Decimal;
        case TypeKind::Skip:
            return BuilderCallKind::None;
    }
    return BuilderCallKind::None;
}

// Top-level (`body_builder::field(tag, ...)`) is overloaded on the C++
// parameter type, so no name-suffix is needed there — overload resolution
// picks the right one once the emitter passes the right argument shape
// (string_view / char / int64_t / decimal_t). `entry_handle` (group-entry
// setters) is NOT overloaded — it exposes distinctly-named
// set_string/set_char/set_int/set_decimal (body_builder.hpp) — this gives
// the emitter that name.
inline std::string_view entry_set_name(BuilderCallKind k) noexcept {
    switch (k) {
        case BuilderCallKind::String:
            return "set_string";
        case BuilderCallKind::Char:
            return "set_char";
        case BuilderCallKind::Int32:
            return "set_int";
        case BuilderCallKind::Decimal:
            return "set_decimal";
        case BuilderCallKind::None:
            return "";
    }
    return "";
}

// FIX application-version enum token for a codegen namespace tag. vt11
// (FIXT.1.1 session layer) has application axis Unknown; v42/v44/v50sp2
// map verbatim. Was a verbatim copy in emit_messages.cpp + emit_reify.cpp.
inline std::string_view app_version_enum(std::string_view ns) {
    if (ns == "vt11") {
        return "Unknown";
    }
    return ns;
}

// Collision-suffix accessor-name deduper. First occurrence of `base` is
// returned as-is; subsequent collisions get a `_t<tag>` suffix. `used` is
// caller-owned so each emission scope keeps its own set (identifiers stay
// byte-identical to the prior inlined lambda). Was triplicated as an inline
// lambda in emit_messages.cpp (×2) + emit_reify.cpp (×1).
inline std::string uniquify_accessor(std::unordered_set<std::string>& used, std::string base,
                                     std::uint16_t tag) {
    if (used.insert(base).second) {
        return base;
    }
    base += "_t";
    base += std::to_string(tag);
    used.insert(base);
    return base;
}

}  // namespace fixpp::codegen
