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
#include <fixpp/dict/field_ref.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "emit.hpp"
#include "gen_util.hpp"
#include "ir.hpp"
#include "template_writer.hpp"

namespace fixpp::codegen {

namespace {

// kMessageView (the flyweight binding target) + app_version_enum() are the
// shared codegen helpers in gen_util.hpp (were verbatim-duplicated here and
// in emit_reify.cpp).

std::string_view kind_cpp_type(TypeKind k) {
    switch (k) {
        case TypeKind::String:
            return "::std::string_view";
        case TypeKind::Char:
            return "char";
        case TypeKind::Bool:
            return "bool";
        case TypeKind::Int32:
            return "::std::int32_t";
        default:
            return "::std::string_view";
    }
}

// One scalar typed accessor. `ptr` selects the access form: a top-level
// flyweight holds `view_` by reference (direct get<Tag>()); a group-entry
// flyweight (062 T012) holds `ctx_` by value and span-scans via the
// `fixpp::wire::get(span, tag, gen)` free helper — a default-constructed
// `ctx_` has an empty span, so the scan reports the field absent with no
// null-guard needed (unlike the retired `view_` pointer form).
void emit_scalar(TemplateWriter& w, std::string_view name, std::uint16_t tag, TypeKind k,
                 bool ptr) {
    if (k == TypeKind::Decimal) {
        w.raw("    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::decimal_t>\n    ");
        w.raw(name);
        w.raw("(::std::pmr::memory_resource* mr) const noexcept\n    { ");
        if (ptr) {
            w.raw("auto fv = ::fixpp::wire::get(ctx_.span, ");
            w.num(tag);
            w.raw(", ctx_.gen);");
        } else {
            w.raw("auto fv = view_.template get<");
            w.num(tag);
            w.raw(">();");
        }
        w.raw(
            " if (!fv) return ::std::unexpected{fv.error()};\n      return "
            "::fixpp::decimal_t::parse(fv->bytes(), mr); }");
        w.line();
        return;
    }
    std::string_view const ct = kind_cpp_type(k);
    w.raw("    [[nodiscard]] inline ::fixpp::core::expected_t<");
    w.raw(ct);
    w.raw(">\n    ");
    w.raw(name);
    w.raw("() const noexcept");
    if (k == TypeKind::String) {
        w.raw(" [[clang::lifetimebound]]");
    }
    w.raw("\n    { return ::fixpp::dict::decode_field<");
    w.raw(ct);
    w.raw(">(");
    if (ptr) {
        w.raw("::fixpp::wire::get(ctx_.span, ");
        w.num(tag);
        w.raw(", ctx_.gen)");
    } else {
        w.raw("view_.template get<");
        w.num(tag);
        w.raw(">()");
    }
    w.raw("); }");
    w.line();
}

void emit_field_value(TemplateWriter& w, bool ptr) {
    w.raw("    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::wire::field_view>\n");
    w.raw("    field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]\n    { ");
    if (ptr) {
        w.raw("return ::fixpp::wire::get(ctx_.span, tag, ctx_.gen); }");
    } else {
        w.raw("return view_.get(tag); }");
    }
    w.line();
}

// Version-wide member map: FieldRef::group_no_tag (0 == top level) -> the
// distinct member fields under that group, deduped by tag in first-encounter
// order over the bytewise-sorted message list. Deduping + emitting each
// distinct group no_tag ONCE (in fixpp::<ns>::groups) is what tames FIX50SP2:
// component reuse otherwise re-nests the same subtree per parent per message
// (a 100 MB combinatorial blow-up). Built inline in emit_messages().
using MemberMap = std::unordered_map<std::uint16_t, std::vector<FieldIR const*>>;

// Defensive nesting cap: real FIX group nesting is shallow; the cap plus the
// on-path guard break any pathological edge from component reuse (a dropped
// edge's group stays field_value-reachable — AC-G6).
constexpr int kMaxGroupDepth = 16;

std::string group_cls(std::uint16_t no_tag) { return "G_" + std::to_string(no_tag); }

// Per-message group plan: each distinct group no_tag is emitted ONCE as a
// nested class (not re-nested per parent — that is the FIX50SP2 blow-up),
// in dependency order (a sub-group's class is complete before the parent
// that returns group_view over it). Cyclic / too-deep edges are dropped
// (that sub-group stays reachable via field_value — AC-G6); deterministic
// in message field order.
struct GroupPlan {
    std::vector<std::uint16_t> order;                                    // emit order, deps first
    std::unordered_map<std::uint16_t, std::vector<std::uint16_t>> kids;  // emittable sub no_tags
};

// Deliberate recursive DFS for group dependency ordering.
// De-recursing risks subtle ordering changes that alter the deterministic group
// emission order pinned by the 4 golden headers (ctest -R determinism).
// Recursion depth is bounded by kMaxGroupDepth (16) — stack overflow excluded.
// NOLINTNEXTLINE(misc-no-recursion)
void plan_dfs(std::uint16_t g, MemberMap const& mm, std::unordered_set<std::uint16_t>& onpath,
              std::unordered_set<std::uint16_t>& done, GroupPlan& gp, int depth) {
    if (done.contains(g)) {
        return;
    }
    onpath.insert(g);
    auto const it = mm.find(g);
    if (it != mm.end()) {
        for (auto const* f : it->second) {
            if (f->ref.type != fixpp::dict::field_data_type::NumInGroup) {
                continue;
            }
            std::uint16_t const c = f->ref.tag;
            if (depth + 1 >= kMaxGroupDepth || onpath.contains(c)) {
                continue;
            }
            plan_dfs(c, mm, onpath, done, gp, depth + 1);  // NOLINT(misc-no-recursion)
            gp.kids[g].push_back(c);
        }
    }
    onpath.erase(g);
    if (done.insert(g).second) {
        gp.order.push_back(g);
    }
}

// no_tag (parent group key) and tag (member field key) are semantically distinct;
// adjacent uint16_t params suppressed intentionally.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
FieldIR const* find_member(MemberMap const& mm, std::uint16_t no_tag, std::uint16_t tag) {
    auto const it = mm.find(no_tag);
    if (it == mm.end()) {
        return nullptr;
    }
    for (auto const* f : it->second) {
        if (f->ref.tag == tag) {
            return f;
        }
    }
    return nullptr;
}

// Emit one nested repeating-group flyweight class `G_<no_tag>`. Sub-group
// classes it references (gp.kids[no_tag]) are already defined earlier in
// gp.order, so referencing them by sibling name is well-formed.
void emit_group_class(TemplateWriter& w, MemberMap const& mm, GroupPlan const& gp,
                      std::uint16_t no_tag) {
    std::string const cls = group_cls(no_tag);
    w.raw("    class ");
    w.raw(cls);
    w.line(" {");
    w.line("    public:");
    w.raw("        ");
    w.raw(cls);
    w.line("() noexcept = default;");
    // 062 T012: the entry stores `entry_context` by value (FR-004
    // zero-alloc-by-value) — no null-guard needed, a default-constructed
    // `ctx_` has an empty span and every accessor below calls
    // `wire::get(ctx_.span, ...)` unconditionally, which degrades to
    // `wire_required_field_missing` (the field-absent sentinel), NOT the
    // retired `view_` pointer's `dict_xml_parse_failed` null-guard. A
    // default-constructed `G_<n>` is out-of-contract/unspecified —
    // `group_view::operator[]` never hands a caller one — so this is an
    // incidental degrade, not a guaranteed error code.
    w.raw("        explicit ");
    w.raw(cls);
    w.line("(::fixpp::wire::entry_context ctx) noexcept");
    w.line("            : ctx_(ctx) {}");

    std::unordered_set<std::string> used;
    auto uniq = [&](std::string base, std::uint16_t tag) {
        return uniquify_accessor(used, std::move(base), tag);
    };

    auto const it = mm.find(no_tag);
    if (it != mm.end()) {
        for (auto const* f : it->second) {
            if (f->ref.type == fixpp::dict::field_data_type::NumInGroup) {
                continue;
            }
            TypeKind const k = kind_of(f->ref.type);
            if (k == TypeKind::Skip) {
                continue;
            }
            emit_scalar(w, uniq(to_accessor(f->name), f->ref.tag), f->ref.tag, k, true);
        }
    }
    auto const kit = gp.kids.find(no_tag);
    if (kit != gp.kids.end()) {
        for (std::uint16_t const c : kit->second) {
            FieldIR const* d = find_member(mm, no_tag, c);
            std::string const acc =
                uniq(to_accessor(strip_no_prefix(d != nullptr ? d->name : group_cls(c))), c);
            // 062 T016: descend via the ROOT OffsetTable's flat nested-cache
            // seam (T006), keyed by (this entry's own occurrence identity,
            // nested no_tag) — build-once / fetch-cached. `parent_cache_owner`
            // is threaded UNCHANGED into the returned group_view's base
            // context (recursive descent stays keyed off the SAME root at
            // every depth — INV-G3/N2).
            w.raw("    [[nodiscard]] inline ::fixpp::wire::group_view<");
            w.raw(group_cls(c));
            w.raw(">\n    ");
            w.raw(acc);
            w.raw(
                "() const noexcept [[clang::lifetimebound]]\n    { if (ctx_.parent_cache_owner "
                "== nullptr) return {};\n      auto const nested = "
                "ctx_.parent_cache_owner->nested_group_slices(ctx_.outer_occurrence_id, "
                "ctx_.span.size(), ");
            w.num(c);
            w.raw(
                ", ctx_.opaque_dict, ctx_.group_member_fn, ctx_.gen);\n      return "
                "::fixpp::wire::group_view<");
            w.raw(group_cls(c));
            w.raw(">{nested, ctx_}; }");
            w.line();
        }
    }
    emit_field_value(w, true);
    w.line("    private:");
    w.line("        ::fixpp::wire::entry_context ctx_{};");
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
    w.raw(kMessageView);
    w.line(" const& view [[clang::lifetimebound]]) noexcept");
    w.line("        : view_(view) {}");
    w.line();

    std::unordered_set<std::string> used;
    auto uniq = [&](std::string base, std::uint16_t tag) {
        return uniquify_accessor(used, std::move(base), tag);
    };

    // Per-message top-level fields (group_no_tag == 0), deduped by tag.
    // Repeating groups are NOT re-nested per message — they live once in
    // the shared fixpp::<ns>::groups namespace (emitted by emit_messages);
    // the message references groups::G_<no_tag> by qualified name.
    std::vector<FieldIR const*> const top = collect_top_fields(m);
    std::string gq = "::fixpp::";
    gq += ns;
    gq += "::groups::";

    for (auto const* f : top) {
        if (f->ref.type == fixpp::dict::field_data_type::NumInGroup) {
            continue;
        }
        TypeKind const k = kind_of(f->ref.type);
        if (k == TypeKind::Skip) {
            continue;
        }
        emit_scalar(w, uniq(to_accessor(f->name), f->ref.tag), f->ref.tag, k, false);
    }
    for (auto const* f : top) {
        if (f->ref.type != fixpp::dict::field_data_type::NumInGroup) {
            continue;
        }
        w.raw("    [[nodiscard]] inline ::fixpp::wire::group_view<");
        w.raw(gq);
        w.raw(group_cls(f->ref.tag));
        w.raw(">\n    ");
        w.raw(uniq(to_accessor(strip_no_prefix(f->name)), f->ref.tag));
        w.raw("() const noexcept [[clang::lifetimebound]]\n    { return view_.template group<");
        w.num(f->ref.tag);
        w.raw(", ");
        w.raw(gq);
        w.raw(group_cls(f->ref.tag));
        w.raw(">(); }");
        w.line();
    }
    emit_field_value(w, false);
    w.raw("    [[nodiscard]] inline ");
    w.raw(kMessageView);
    w.line(" const&\n    view() const noexcept [[clang::lifetimebound]] { return view_; }");
    w.line();
    w.line("private:");
    w.raw("    ");
    w.raw(kMessageView);
    w.line(" const& view_;");
    w.line("};");
    w.raw("static_assert(sizeof(");
    w.raw(id);
    w.raw(") == sizeof(");
    w.raw(kMessageView);
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

    // Version-wide group member map: every distinct group no_tag is emitted
    // ONCE in fixpp::<ns>::groups (a group reused across N messages was being
    // re-nested N times — the FIX50SP2 100MB blow-up). Members are the union
    // (deduped by tag, first-encounter over the bytewise-sorted message list
    // then field order) so the shared class is consistent and deterministic.
    MemberMap gmm;
    {
        std::unordered_map<std::uint16_t, std::unordered_set<std::uint16_t>> seen;
        for (auto const& m : ir.messages) {
            for (auto const& f : m.fields) {
                std::uint16_t const parent = f.ref.group_no_tag;
                if (seen[parent].insert(f.ref.tag).second) {
                    gmm[parent].push_back(&f);
                }
            }
        }
    }
    std::vector<std::uint16_t> group_tags;
    {
        std::unordered_set<std::uint16_t> gseen;
        for (auto const& m : ir.messages) {
            for (auto const& f : m.fields) {
                if (f.ref.type == fixpp::dict::field_data_type::NumInGroup &&
                    gseen.insert(f.ref.tag).second) {
                    group_tags.push_back(f.ref.tag);
                }
            }
        }
    }
    GroupPlan gp;
    {
        std::unordered_set<std::uint16_t> onpath;
        std::unordered_set<std::uint16_t> done;
        for (std::uint16_t const gt : group_tags) {
            plan_dfs(gt, gmm, onpath, done, gp, 0);
        }
    }
    w.raw("namespace fixpp::");
    w.raw(ir.ns);
    w.line("::groups {  // shared repeating-group flyweights (AC-G5/AC-G6)");
    w.line();
    for (std::uint16_t const g : gp.order) {
        emit_group_class(w, gmm, gp, g);
    }
    w.line();
    w.raw("}  // namespace fixpp::");
    w.raw(ir.ns);
    w.line("::groups");
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
    for (auto const& m : ir.messages) {
        emit_message(w, ir.ns, m);
    }
    w.raw("}  // namespace fixpp::");
    w.line(ir.ns);
    return std::move(w).take();
}

}  // namespace fixpp::codegen
