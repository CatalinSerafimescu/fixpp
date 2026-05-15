// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/emit_messages.cpp
//
// 003-dictionary-codegen — T024 — <vXX>/Messages.hpp typed-message flyweights
// (data-model Entity 1; AC-G1..G8/G11; the [2c §4.7] shape pinned by
// contracts/generated_message.hpp). One class per emitted standard message in
// namespace fixpp::<vXX>:
//   * static constexpr msg_type_v / version_v (AC-G2)
//   * explicit noexcept view-binding ctor, no validation (AC-G3)
//   * per-field [[nodiscard]] inline noexcept expected_t<T> accessor via
//     dict::decode_field<T> (string/int/char/bool); the decimal accessor
//     takes std::pmr::memory_resource* mr and routes through
//     fixpp::decimal_t::parse (v1.4 / RC#2 — NOT field_traits; AC-G4/I-16)
//   * repeating groups -> wire::group_view<<Msg>::<Grp>Group> + a nested
//     default-constructible flyweight with the same accessor discipline +
//     its own field_value(uint16_t) (AC-G5/AC-G6). Group structure is
//     reconstructed from FieldRef::group_no_tag (XmlLoader sets it to the
//     enclosing group's no_tag; 0 == top level) + type == NumInGroup (the
//     delimiter) — the RC#5 message_fields run carries this verbatim.
//   * field_value(uint16_t) forwarder + view() bridge (AC-G6)
//   * static_assert(sizeof == one pointer) (AC-G7; seam #18)
// owning_<Msg> is forward-declared here (defined in Reify.hpp, US2/T032) so
// the per-message owning_message_traits specialisation is well-formed (AC-G7a).
// Per-tag accessors are inline noexcept, NOT constexpr (AC-G11). Deterministic
// LF-only emission over the bytewise-sorted message list (NFR-003-7).
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "emit.hpp"
#include "gen_util.hpp"
#include "template_writer.hpp"

namespace fixpp::codegen {

namespace {

constexpr std::string_view kView =
    "::fixpp::wire::MessageView<::fixpp::wire::access_mode::Index>";

std::string_view app_version_enum(std::string_view ns) {
    // vt11 = FIXT.1.1 session layer: application axis Unknown (admin frames
    // resolve {session_admin, vt11, Unknown}); v42/v44/v50sp2 map verbatim.
    if (ns == "vt11") return "Unknown";
    return ns;
}

std::string_view kind_cpp_type(TypeKind k) {
    switch (k) {
        case TypeKind::String: return "::std::string_view";
        case TypeKind::Char:   return "char";
        case TypeKind::Bool:   return "bool";
        case TypeKind::Int32:  return "::std::int32_t";
        default:               return "::std::string_view";
    }
}

// One scalar typed accessor. `ptr` selects the access form: a top-level
// flyweight holds `view_` by reference (direct); a nested group flyweight
// holds `view_` as a nullable pointer (null-guarded).
void emit_scalar(TemplateWriter& w, std::string_view name, std::uint16_t tag,
                 TypeKind k, bool ptr) {
    if (k == TypeKind::Decimal) {
        w.raw("    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::decimal_t>\n    ");
        w.raw(name);
        w.raw("(::std::pmr::memory_resource* mr) const noexcept\n    { ");
        if (ptr) {
            w.raw("if (!view_) return ::std::unexpected{::fixpp::core::error::dict_xml_parse_failed}; auto fv = view_->template get<");
        } else {
            w.raw("auto fv = view_.template get<");
        }
        w.num(tag);
        w.raw(">(); if (!fv) return ::std::unexpected{fv.error()};\n      return ::fixpp::decimal_t::parse(fv->bytes(), mr); }");
        w.line();
        return;
    }
    std::string_view const ct = kind_cpp_type(k);
    w.raw("    [[nodiscard]] inline ::fixpp::core::expected_t<");
    w.raw(ct);
    w.raw(">\n    ");
    w.raw(name);
    w.raw("() const noexcept");
    if (k == TypeKind::String) w.raw(" [[clang::lifetimebound]]");
    w.raw("\n    { ");
    if (ptr) {
        w.raw("if (!view_) return ::std::unexpected{::fixpp::core::error::dict_xml_parse_failed};\n      return ::fixpp::dict::decode_field<");
        w.raw(ct);
        w.raw(">(view_->template get<");
    } else {
        w.raw("return ::fixpp::dict::decode_field<");
        w.raw(ct);
        w.raw(">(view_.template get<");
    }
    w.num(tag);
    w.raw(">()); }");
    w.line();
}

void emit_field_value(TemplateWriter& w, bool ptr) {
    w.raw("    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::wire::field_view>\n");
    w.raw("    field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]\n    { ");
    if (ptr) {
        w.raw("if (!view_) return ::std::unexpected{::fixpp::core::error::dict_xml_parse_failed}; return view_->get(tag); }");
    } else {
        w.raw("return view_.get(tag); }");
    }
    w.line();
}

// Reconstruct + emit one repeating-group flyweight (nested class). `members`
// = the fields whose FieldRef::group_no_tag == this group's no_tag, in IR
// order. Sub-groups (members that are themselves NumInGroup) recurse —
// defined before the accessor that returns group_view over them.
void emit_group(TemplateWriter& w, std::vector<FieldIR> const& all,
                std::uint16_t group_tag, std::string const& cls) {
    w.raw("    class ");
    w.raw(cls);
    w.line(" {");
    w.line("    public:");
    w.raw("        ");
    w.raw(cls);
    w.line("() noexcept = default;");
    w.raw("        explicit ");
    w.raw(cls);
    w.raw("(");
    w.raw(kView);
    w.line(" const& v [[clang::lifetimebound]]) noexcept");
    w.line("            : view_(&v) {}");

    std::unordered_set<std::string> used;
    auto uniq = [&](std::string base, std::uint16_t tag) {
        if (used.insert(base).second) return base;
        base += "_t";
        base += std::to_string(tag);
        used.insert(base);
        return base;
    };

    // Sub-groups first (must be defined before their group_view accessor).
    struct Sub { std::uint16_t tag; std::string cls; std::string acc; };
    std::vector<Sub> subs;
    for (auto const& f : all) {
        if (f.ref.group_no_tag != group_tag) continue;
        if (f.ref.type != fixpp::dict::field_data_type::NumInGroup) continue;
        std::string sub_cls =
            to_identifier(strip_no_prefix(f.name)) + "Group";
        std::string sub_acc = uniq(to_accessor(strip_no_prefix(f.name)), f.ref.tag);
        TemplateWriter sw;
        emit_group(sw, all, f.ref.tag, sub_cls);
        w.raw(sw.str());
        subs.push_back({f.ref.tag, std::move(sub_cls), std::move(sub_acc)});
    }

    for (auto const& f : all) {
        if (f.ref.group_no_tag != group_tag) continue;
        if (f.ref.type == fixpp::dict::field_data_type::NumInGroup) continue;
        TypeKind const k = kind_of(f.ref.type);
        if (k == TypeKind::Skip) continue;
        emit_scalar(w, uniq(to_accessor(f.name), f.ref.tag), f.ref.tag, k, true);
    }
    for (auto const& s : subs) {
        w.raw("    [[nodiscard]] inline ::fixpp::wire::group_view<");
        w.raw(cls);
        w.raw("::");
        w.raw(s.cls);
        w.raw(">\n    ");
        w.raw(s.acc);
        w.raw("() const noexcept [[clang::lifetimebound]]\n    { if (!view_) return {}; return view_->template group<");
        w.num(s.tag);
        w.raw(", ");
        w.raw(cls);
        w.raw("::");
        w.raw(s.cls);
        w.raw(">(); }");
        w.line();
    }
    emit_field_value(w, true);
    w.line("    private:");
    w.raw("        ");
    w.raw(kView);
    w.line(" const* view_ = nullptr;");
    w.line("    };");
}

void emit_message(TemplateWriter& w, std::string_view ns, MessageIR const& m) {
    std::string const id = to_identifier(m.name);
    w.raw("class ");
    w.raw(id);
    w.line(" {");
    w.line("public:");
    w.raw("    static constexpr ::std::string_view msg_type_v = \"");
    w.raw(m.msg_type);
    w.line("\";");
    w.raw("    static constexpr ::fixpp::dict::application_version version_v =\n");
    w.raw("        ::fixpp::dict::application_version::");
    w.raw(app_version_enum(ns));
    w.line(";");
    w.line();
    w.raw("    explicit ");
    w.raw(id);
    w.raw("(");
    w.raw(kView);
    w.line(" const& view [[clang::lifetimebound]]) noexcept");
    w.line("        : view_(view) {}");
    w.line();

    std::unordered_set<std::string> used;
    auto uniq = [&](std::string base, std::uint16_t tag) {
        if (used.insert(base).second) return base;
        base += "_t";
        base += std::to_string(tag);
        used.insert(base);
        return base;
    };

    struct Grp { std::uint16_t tag; std::string cls; std::string acc; };
    std::vector<Grp> groups;
    for (auto const& f : m.fields) {
        if (f.ref.group_no_tag != 0) continue;
        if (f.ref.type != fixpp::dict::field_data_type::NumInGroup) continue;
        std::string gcls = to_identifier(strip_no_prefix(f.name)) + "Group";
        std::string gacc = uniq(to_accessor(strip_no_prefix(f.name)), f.ref.tag);
        emit_group(w, m.fields, f.ref.tag, gcls);
        groups.push_back({f.ref.tag, std::move(gcls), std::move(gacc)});
    }
    if (!groups.empty()) w.line();

    for (auto const& f : m.fields) {
        if (f.ref.group_no_tag != 0) continue;
        if (f.ref.type == fixpp::dict::field_data_type::NumInGroup) continue;
        TypeKind const k = kind_of(f.ref.type);
        if (k == TypeKind::Skip) continue;
        emit_scalar(w, uniq(to_accessor(f.name), f.ref.tag), f.ref.tag, k, false);
    }
    for (auto const& g : groups) {
        w.raw("    [[nodiscard]] inline ::fixpp::wire::group_view<");
        w.raw(id);
        w.raw("::");
        w.raw(g.cls);
        w.raw(">\n    ");
        w.raw(g.acc);
        w.raw("() const noexcept [[clang::lifetimebound]]\n    { return view_.template group<");
        w.num(g.tag);
        w.raw(", ");
        w.raw(id);
        w.raw("::");
        w.raw(g.cls);
        w.raw(">(); }");
        w.line();
    }
    emit_field_value(w, false);
    w.raw("    [[nodiscard]] inline ");
    w.raw(kView);
    w.line(" const&\n    view() const noexcept [[clang::lifetimebound]] { return view_; }");
    w.line();
    w.line("private:");
    w.raw("    ");
    w.raw(kView);
    w.line(" const& view_;");
    w.line("};");
    w.raw("static_assert(sizeof(");
    w.raw(id);
    w.raw(") == sizeof(");
    w.raw(kView);
    w.line(" const*));");
    w.line();
}

}  // namespace

std::string emit_messages(VersionIR const& ir) {
    TemplateWriter w;
    w.line("// SPDX-License-Identifier: AGPL-3.0-or-later");
    w.line("// GENERATED by fixpp-codegen (003-dictionary-codegen, T024). DO NOT EDIT.");
    w.line("// <vXX>/Messages.hpp — typed-message flyweights ([2c §4.7];");
    w.line("// data-model Entity 1; AC-G1..G8/G11). Shape oracle:");
    w.line("// specs/003-dictionary-codegen/contracts/generated_message.hpp.");
    w.line("#pragma once");
    w.line("#include <fixpp/core/decimal_alias.hpp>");
    w.line("#include <fixpp/core/error.hpp>");
    w.line("#include <fixpp/dict/field_traits.hpp>");
    w.line("#include <fixpp/dict/version_profile.hpp>");
    w.line("#include <fixpp/wire/message_view_contract.hpp>");
    w.line("#include <cstdint>");
    w.line("#include <expected>");
    w.line("#include <memory_resource>");
    w.line("#include <string_view>");
    w.line("#include <utility>");
    w.line();
    w.raw("namespace fixpp::");
    w.raw(ir.ns);
    w.line(" {");
    w.line();
    w.line("// owning_<Msg> defined in <vXX>/Reify.hpp (US2/T032); forward-declared");
    w.line("// here so the per-message owning_message_traits specialisation is");
    w.line("// well-formed (AC-G7a / seam #18).");
    for (auto const& m : ir.messages) {
        w.raw("class owning_");
        w.raw(to_identifier(m.name));
        w.line(";");
    }
    w.line();
    for (auto const& m : ir.messages) emit_message(w, ir.ns, m);
    w.raw("}  // namespace fixpp::");
    w.line(ir.ns);
    return std::move(w).take();
}

}  // namespace fixpp::codegen
