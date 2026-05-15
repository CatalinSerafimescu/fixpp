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

}  // namespace fixpp::codegen
