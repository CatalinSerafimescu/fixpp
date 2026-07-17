// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/emit_builders.cpp
//
// 067-codegen-writer-emitter — the write emitter: build_<Msg>(out,args) /
// <Msg>Args over wire::body_builder, for every OFFICIAL message
// (data-model.md §1-§3; contracts/generated-builder.md G1-G4/G9). T016/T017:
// per-message GROUP PLANNER + <Msg>Args/build_<Msg> emission. This file also
// emits `validate_` (US3/T022-T025): the `writer_traits<T>` specializations
// and the thin `validate_<Msg>` wrappers over
// `::fixpp::wire::validate_required<T>` (see below), off the serialize path.
//
// Scope: v44 only (research.md R6 — the 33-OFFICIAL-MsgTypes set is verified
// against FIX44.xml specifically; other codegen versions get no Builders.hpp
// at this feature's scope, matching `write_file`'s empty-skip).
//
// Per-message group plan (delimiter + member ORDER) comes from
// MessageIR.group_order (R9/T008), NOT the tag-sorted m.fields, NOT a
// version-wide MemberMap (R7/RC#1). Required-ness of a group at its OWN
// level is read off the group's own FieldRef.rule (m.fields, tag-keyed —
// N3 census: tags are unique per message, so this lookup is unambiguous at
// every level). Top-level scalar/group emission order is tag-ascending
// (served by the tag-sorted m.fields run via collect_top_fields), with the
// framing-tag exclusion set {8,9,10,34,35,49,52,56} (data-model.md §2.1).
//
// A single message's serialization body is emitted as ONE recursive INLINE
// block inside build_<Msg> (nested for-loops for nested groups), NOT
// factored into separate per-group functions: every field/group call for a
// message's whole tree lives textually inside build_<Msg>, in declaration
// order at every depth (G3/INV-ORDER). group_end is always issued via `bb`
// (the root body_builder local) — group_handle::owner_ always points at the
// root body_builder regardless of nesting depth (061 body_builder.hpp), so
// a nested group_handle still closes correctly via `bb.group_end(...)`.
//
// 077-builder-args-dedup T008 — group Args STRUCT TYPES are no longer
// message-rooted. Each distinct repeating-group STRUCTURAL PLAN (data-model
// Entity 1: keyed by `(no_tag, recursive_signature)` — delimiter + ordered
// members, each `(tag, required, {child-signature}?)`, computed bottom-up)
// is interned ONCE per version and emitted ONCE in `fixpp::<ns>::groups` as
// `G_<no_tag>Args` (a `no_tag` with exactly one distinct signature) or
// ordinaled `G_<no_tag>_1Args..G_<no_tag>_kArgs` (>=2 signatures, no bare
// name — G1a). Per-message top-level `<Msg>Args` stays per-message (not
// deduped) and references shared group plans by qualified name
// (`groups::G_...Args`); a plan's own nested-group members reference their
// children unqualified (same `groups` namespace — mirrors emit_messages.cpp's
// `G_<no_tag>` flyweight convention, generalized with a signature so
// structurally-distinct occurrences of the same `no_tag` stay separate
// plans — research.md R2/R3: a bare `no_tag` key is NOT sound, up to 8
// distinct plans/no_tag observed). Interning is bottom-up (a child's plan is
// always fully interned before its parent's signature — which embeds the
// child's signature — is computed), so `PlanIntern::plans` is already
// children-before-parents post-order by construction (FR-011/G1c) — no
// separate topological pass is needed.
#include <algorithm>
#include <array>
#include <cstddef>
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

// research.md R6 — the exact 33-element OFFICIAL MsgType set (v44).
constexpr std::array<std::string_view, 33> kOfficial33 = {
    "D", "E", "F", "G", "H", "8", "9", "q", "r", "AF", "AC", "t", "u", "V", "W", "X", "Y", "c",
    "d", "e", "f", "g", "h", "i", "b", "S", "R", "AG", "Z", "a", "J", "P", "AS"};

bool is_official(std::string_view msg_type) {
    return std::find(kOfficial33.begin(), kOfficial33.end(), msg_type) != kOfficial33.end();
}

// 069-v44-all-families (data-model.md Entity "N-002/N-003 exclusion set"):
// session-FSM-dispatch messages, a different work class, remain the separate
// v1.0-tagging gate (FR-003). They are `msgcat='app'` so the app/admin filter
// alone does not exclude them; BW/BX/BY are FIX 5.0 messages absent from the
// vendored FIX44 dictionary (a harmless no-op here, kept for forward-compat).
constexpr std::array<std::string_view, 5> kN002N003Excluded = {"BE", "BF", "BW", "BX", "BY"};

// 077-builder-args-dedup T017 (research.md R4 / data-model.md Entity 4):
// kN002N003Excluded is v44-SPECIFIC (frozen for its 067/069 golden). Other
// builder-bearing versions do not inherit it -- BW/BX/BY are genuine FIX 5.0
// SP2 application messages (ApplicationMessageRequest/Ack/Report), not a
// session-FSM-dispatch class there, so v50sp2/vlatest emit their full
// `is_application` set unfiltered.
bool is_n002_n003_excluded(std::string_view ns, std::string_view msg_type) {
    return ns == "v44" &&
           std::find(kN002N003Excluded.begin(), kN002N003Excluded.end(), msg_type) !=
               kN002N003Excluded.end();
}

// Defensive floor: the 8-tag framer envelope — BeginString(8), BodyLength(9),
// CheckSum(10), MsgSeqNum(34), MsgType(35, stamped by the body_builder ctor),
// SenderCompID(49), SendingTime(52), TargetCompID(56). The PRIMARY exclusion
// is now provenance-based (VersionIR::header_trailer_tags, is_header_trailer
// below), which is a superset of this floor for any dict with a <header>;
// kFramingTags stays as a floor for a dict with no <header> at all.
constexpr std::array<std::uint16_t, 8> kFramingTags = {8, 9, 10, 34, 35, 49, 52, 56};

bool is_framing_tag(std::uint16_t tag) {
    return std::find(kFramingTags.begin(), kFramingTags.end(), tag) != kFramingTags.end();
}

// Provenance-based exclusion (data-model.md §2.1 / contract G5): true if
// `tag` is declared under the source dict's top-level <header>/<trailer>
// (VersionIR::header_trailer_tags — sorted, unique, recursively resolved
// through <component>/<group> refs by ir.cpp). Excludes the FULL
// header/trailer envelope (Signature(89), SecureData(91),
// SignatureLength(93), routing fields, etc.), not just the 8-tag framer
// floor above.
bool is_header_trailer(std::uint16_t tag, std::vector<std::uint16_t> const& header_trailer_tags) {
    return std::binary_search(header_trailer_tags.begin(), header_trailer_tags.end(), tag);
}

std::string_view args_cpp_type(TypeKind k) {
    switch (k) {
        case TypeKind::Decimal:
            return "::fixpp::decimal_t";
        case TypeKind::Char:
            return "char";
        case TypeKind::Bool:
            return "bool";
        case TypeKind::Int32:
            return "::std::int64_t";
        case TypeKind::String:
        default:
            return "::std::string_view";
    }
}

struct LevelItem;
using LevelPlan = std::vector<LevelItem>;

// One resolved member of a level's Args struct / write-body, in DECLARATION
// order (R9 for groups; tag-ascending for top-level).
struct LevelItem {
    bool is_group = false;
    std::string accessor;

    // ── scalar ──
    std::uint16_t tag = 0;  // primary tag (the LENGTH tag when coupled)
    TypeKind kind = TypeKind::Skip;
    bool coupled = false;  // Length+Data pair folded into one Args member
    std::uint16_t data_tag = 0;
    // 067 US3/T025 (R3/G5): this scalar's own FieldRef.rule==Required (the
    // Length tag's rule, OR'd with the Data tag's rule when coupled — either
    // half being Required makes the ONE coupled Args member required).
    // group_no_tag/header-exclusion are already baked in by construction:
    // top-level items are the framing-excluded run (top_level_synthetic_
    // members), group items never carry a framing tag at all.
    bool required = false;

    // ── group ──
    // Reference to the shared plan this occurrence resolves to (filled by
    // fill_group_type_names, PASS 3 below): the BARE `G_<no_tag>[_ord]Args`
    // stem when referenced from within `fixpp::<ns>::groups` itself (a
    // nested group's own members), or the `groups::`-qualified name when
    // referenced from a per-message top-level `<Msg>Args` (G1/G2).
    std::string group_type_name;
    std::uint16_t no_tag = 0;
    std::uint16_t delimiter_tag = 0;
    bool group_required = false;
    // Index into PlanIntern::plans identifying this occurrence's interned
    // structural plan (data-model Entity 1) — replaces the pre-077
    // message-rooted `child_plan` pointer.
    std::size_t plan_id = 0;
};

// One distinct structural plan (data-model Entity 1), interned once per
// version. `members` are this plan's OWN resolved members (a group item
// within references its child by `plan_id`, not by name — `name` is filled
// in only after every plan is known, PASS 2/assign_plan_names below, since
// bare-vs-ordinaled depends on the FINAL count of distinct signatures under
// the same `no_tag`).
struct InternedPlan {
    std::uint16_t no_tag = 0;
    std::uint16_t delimiter_tag = 0;
    std::string signature;
    LevelPlan members;
    std::string name;  // "G_<no_tag>Args" or "G_<no_tag>_<ordinal>Args"
};

// PASS 1 intern table: `(no_tag, recursive_signature)` -> plan index
// (data-model Entity 1 dedup key). Append-only — `LevelItem::plan_id` are
// indices into `plans`, so plans must never be reordered after interning.
class PlanIntern {
public:
    std::vector<InternedPlan> plans;

    // Interns one group occurrence's already-resolved members. Returns the
    // existing plan's index on a signature collision (the freshly-resolved
    // `members` passed in is discarded — a byte-identical signature implies
    // byte-identical members, since accessor names derive deterministically
    // from tag -> field name within one version); otherwise appends a new
    // plan and returns its (newly last) index.
    std::size_t intern(std::uint16_t no_tag, std::uint16_t delimiter_tag,
                       std::string signature, LevelPlan members) {
        std::string key = std::to_string(no_tag) + ":" + signature;
        auto const it = key_to_index_.find(key);
        if (it != key_to_index_.end()) {
            return it->second;
        }
        std::size_t const idx = plans.size();
        plans.push_back(InternedPlan{no_tag, delimiter_tag, std::move(signature),
                                     std::move(members), /*name=*/{}});
        key_to_index_.emplace(std::move(key), idx);
        return idx;
    }

private:
    std::unordered_map<std::string, std::size_t> key_to_index_;
};

// Recursive structural signature (data-model Entity 1): `delimiter + ordered
// members, each (tag, required, {child-signature}?)`. Does NOT include this
// group's OWN `no_tag` (that is the other half of the dedup key, kept
// separate so a `no_tag` with a single distinct signature can name bare —
// G1a). A nested-group member's contribution embeds its CHILD's already-
// interned signature (`intern.plans[item.plan_id].signature`) — available
// because interning is bottom-up (the child is always interned before this,
// its parent's, signature is computed). `kind`/`coupled`/`data_tag` are
// technically redundant with the tag (deterministic per version) but are
// included for a self-describing, unambiguous key.
std::string compute_signature(std::uint16_t delimiter_tag, LevelPlan const& plan,
                              PlanIntern const& intern) {
    std::string sig = "D";
    sig += std::to_string(delimiter_tag);
    sig += ";";
    for (auto const& item : plan) {
        if (item.is_group) {
            sig += "G";
            sig += std::to_string(item.no_tag);
            sig += ":";
            sig += item.group_required ? "1" : "0";
            sig += ":{";
            sig += intern.plans[item.plan_id].signature;
            sig += "};";
        } else {
            sig += "S";
            sig += std::to_string(item.tag);
            sig += ":";
            sig += item.required ? "1" : "0";
            sig += ":";
            sig += std::to_string(static_cast<int>(item.kind));
            sig += ":";
            sig += item.coupled ? "1" : "0";
            if (item.coupled) {
                sig += ":";
                sig += std::to_string(item.data_tag);
            }
            sig += ";";
        }
    }
    return sig;
}

// PASS 2 — name assignment (data-model Entity 2 / contract G1a). Must run
// AFTER discovery is complete: bare-vs-ordinaled depends on the FINAL
// distinct-signature count per `no_tag`. Ordinal order = first-encounter,
// i.e. `intern.plans`' own (already bytewise-sorted-message x declaration-
// order) append order, filtered by `no_tag` — filtering an append-ordered
// vector preserves relative order, so no separate sort is needed.
void assign_plan_names(PlanIntern& intern) {
    std::unordered_map<std::uint16_t, std::vector<std::size_t>> by_no_tag;
    for (std::size_t i = 0; i < intern.plans.size(); ++i) {
        by_no_tag[intern.plans[i].no_tag].push_back(i);
    }
    for (auto const& [no_tag, idxs] : by_no_tag) {
        if (idxs.size() == 1) {
            intern.plans[idxs[0]].name = "G_" + std::to_string(no_tag) + "Args";
            continue;
        }
        for (std::size_t k = 0; k < idxs.size(); ++k) {
            intern.plans[idxs[k]].name =
                "G_" + std::to_string(no_tag) + "_" + std::to_string(k + 1) + "Args";
        }
    }
}

// PASS 3 — reference resolution (G1/G2): fills every group item's
// `group_type_name` from its interned plan's now-final `name`. `qualified`
// selects the `groups::`-qualified form (per-message top-level `<Msg>Args`,
// emitted in `fixpp::<ns>`) vs the bare form (a plan's own members, emitted
// inside `fixpp::<ns>::groups` itself, referencing a sibling by unqualified
// name).
void fill_group_type_names(LevelPlan& plan, PlanIntern const& intern, bool qualified) {
    for (auto& item : plan) {
        if (!item.is_group) {
            continue;
        }
        std::string const& name = intern.plans[item.plan_id].name;
        item.group_type_name = qualified ? ("groups::" + name) : name;
    }
}

void emit_args_struct(TemplateWriter& w, std::string const& type_name, LevelPlan const& plan) {
    w.raw("struct ");
    w.raw(type_name);
    w.line(" {");
    for (auto const& item : plan) {
        if (item.is_group) {
            if (item.group_required) {
                w.raw("    ::std::span<const ");
                w.raw(item.group_type_name);
                w.raw("> ");
            } else {
                w.raw("    ::std::optional<::std::span<const ");
                w.raw(item.group_type_name);
                w.raw(">> ");
            }
        } else {
            w.raw("    ::std::optional<");
            w.raw(args_cpp_type(item.kind));
            w.raw("> ");
        }
        w.raw(item.accessor);
        w.line("{};");
    }
    w.line("};");
    w.line();
}

// Emits ONE level's field()/set_*()/group_begin()-loop-group_end() calls,
// recursing INLINE for nested groups (NOT via a separate function — R1/G3:
// keeps the whole message's serialization declaration-order-visible in one
// textual block, and group_end is always `bb.group_end(...)`).
// `accessor_expr` names the Args-shaped object holding this level's members
// ("args" at top level; the per-entry loop variable at any nested level).
// `owner_expr` names the object scalar calls are issued through
// ("bb" at top level via `.field(...)`; the current entry_handle at any
// nested level via `.set_*(...)`). `uid` is a per-message monotonically
// increasing counter giving every generated local (group handle / entry
// handle / loop variable) at every depth a distinct name (nested groups
// really do nest textually here, so shadowing must be avoided by
// construction, not relied upon as legal-but-noisy shadowing).
// `intern` resolves a nested group item's own members for the recursive
// call, by `item.plan_id` (077-builder-args-dedup T008 — replaces the
// pre-077 `item.child_plan` pointer; the shared plan's members are the same
// regardless of which occurrence is currently being serialized).
// NOLINTNEXTLINE(misc-no-recursion)
void emit_level_body(TemplateWriter& w, LevelPlan const& plan, std::string const& accessor_expr,
                     std::string const& owner_expr, bool top_level, int& uid,
                     PlanIntern const& intern) {
    for (auto const& item : plan) {
        if (!item.is_group) {
            w.raw("    if (");
            w.raw(accessor_expr);
            w.raw(".");
            w.raw(item.accessor);
            w.line(") {");
            if (item.coupled) {
                std::string const len_call = top_level ? "bb.field(" : (owner_expr + ".set_int(");
                std::string const data_call =
                    top_level ? "bb.field(" : (owner_expr + ".set_string(");
                w.raw("        auto r_len = ");
                w.raw(len_call);
                w.num(item.tag);
                w.raw(", static_cast<::std::int64_t>(");
                w.raw(accessor_expr);
                w.raw(".");
                w.raw(item.accessor);
                w.line("->size()));");
                w.line("        if (!r_len) return ::std::unexpected(r_len.error());");
                w.raw("        auto r_data = ");
                w.raw(data_call);
                w.num(item.data_tag);
                w.raw(", *");
                w.raw(accessor_expr);
                w.raw(".");
                w.raw(item.accessor);
                w.line(");");
                w.line("        if (!r_data) return ::std::unexpected(r_data.error());");
            } else {
                std::string const call =
                    top_level
                        ? std::string{"bb.field("}
                        : (owner_expr + "." +
                           std::string{entry_set_name(builder_call_kind(item.kind))} + "(");
                w.raw("        auto r = ");
                w.raw(call);
                w.num(item.tag);
                w.raw(", ");
                if (item.kind == TypeKind::Bool) {
                    w.raw("(*");
                    w.raw(accessor_expr);
                    w.raw(".");
                    w.raw(item.accessor);
                    w.raw(") ? 'Y' : 'N'");
                } else {
                    w.raw("*");
                    w.raw(accessor_expr);
                    w.raw(".");
                    w.raw(item.accessor);
                }
                w.line(");");
                w.line("        if (!r) return ::std::unexpected(r.error());");
            }
            w.line("    }");
            continue;
        }

        int const id = uid++;
        std::string const gh = "gh" + std::to_string(id);
        std::string const en = "en" + std::to_string(id);
        std::string const eh = "eh" + std::to_string(id);
        std::string const loop_var = "item" + std::to_string(id);
        bool const optional_group = !item.group_required;

        if (optional_group) {
            w.raw("    if (");
            w.raw(accessor_expr);
            w.raw(".");
            w.raw(item.accessor);
            w.line(") {");
        } else {
            w.line("    {");
        }
        w.raw("        auto ");
        w.raw(gh);
        w.raw(" = ");
        w.raw(top_level ? "bb" : owner_expr);
        w.raw(".group_begin(");
        w.num(item.no_tag);
        w.raw(", ");
        w.num(item.delimiter_tag);
        w.line(");");
        w.raw("        if (!");
        w.raw(gh);
        w.raw(") return ::std::unexpected(");
        w.raw(gh);
        w.line(".error());");
        w.raw("        for (auto const& ");
        w.raw(loop_var);
        w.raw(" : ");
        if (optional_group) {
            w.raw("*");
        }
        w.raw(accessor_expr);
        w.raw(".");
        w.raw(item.accessor);
        w.line(") {");
        w.raw("            auto ");
        w.raw(en);
        w.raw(" = ");
        w.raw(gh);
        w.line("->add_entry();");
        w.raw("            if (!");
        w.raw(en);
        w.raw(") return ::std::unexpected(");
        w.raw(en);
        w.line(".error());");
        w.raw("            auto& ");
        w.raw(eh);
        w.raw(" = *");
        w.raw(en);
        w.line(";");
        LevelPlan const& child_members = intern.plans[item.plan_id].members;
        if (!child_members.empty()) {
            emit_level_body(w, child_members, loop_var, eh, /*top_level=*/false, uid, intern);
        }
        w.line("        }");
        std::string const ge = "ge" + std::to_string(id);
        w.line("        auto " + ge + " = bb.group_end(*" + gh + ");");
        w.line("        if (!" + ge + ") return ::std::unexpected(" + ge + ".error());");
        w.line("    }");
    }
}

void emit_build_fn(TemplateWriter& w, std::string const& msg_id, std::string const& msg_type,
                   LevelPlan const& plan, PlanIntern const& intern) {
    w.raw("inline ::fixpp::core::expected_t<::std::span<::std::byte>> build_");
    w.raw(msg_id);
    w.raw("(::std::span<::std::byte> out, ");
    w.raw(msg_id);
    w.raw("Args const& args) noexcept {\n");
    w.raw("    ::fixpp::wire::body_builder bb{\"");
    w.raw(msg_type);
    w.line("\"};");
    int uid = 0;
    emit_level_body(w, plan, "args", "bb", /*top_level=*/true, uid, intern);
    w.line("    return bb.commit(out);");
    w.line("}");
    w.line();
}

// Top-level (group_no_tag==0) synthetic declaration-order member list: the
// tag-sorted, header/trailer-excluded run from collect_top_fields (R1/R7 —
// the top-level regime IS tag-ascending, served by the tag-sorted m.fields
// run). Exclusion is by PROVENANCE (`header_trailer_tags`), with
// `kFramingTags` retained as a defensive floor.
std::vector<GroupOrderMember> top_level_synthetic_members(
    MessageIR const& m, std::vector<std::uint16_t> const& header_trailer_tags) {
    std::vector<GroupOrderMember> out;
    for (FieldIR const* f : collect_top_fields(m)) {
        if (is_framing_tag(f->ref.tag) || is_header_trailer(f->ref.tag, header_trailer_tags)) {
            continue;
        }
        bool const is_grp = f->ref.type == fixpp::dict::field_data_type::NumInGroup;
        out.push_back(GroupOrderMember{.tag = f->ref.tag, .is_group = is_grp});
    }
    return out;
}

GroupOrderEntry const* find_group_entry(MessageIR const& m, std::vector<std::uint16_t> const& path,
                                        std::uint16_t no_tag) {
    for (auto const& g : m.group_order) {
        if (g.no_tag == no_tag && g.parent_path == path) {
            return &g;
        }
    }
    return nullptr;
}

// Resolves ONE level's DECLARATION-order member list into a LevelPlan.
// 077-builder-args-dedup T008 — PASS 1 (discovery): no longer emits
// anything (the pre-077 message-rooted `emit_args_struct` inline call is
// gone); a nested group's own members are resolved recursively FIRST, then
// its recursive signature (data-model Entity 1) is computed and interned
// into `intern` (bottom-up — a child is always interned before its
// parent's own signature, which embeds the child's, is computed). `path` is
// this level's own GroupOrderEntry key (empty for top-level).
// NOLINTNEXTLINE(misc-no-recursion)
LevelPlan resolve_level(MessageIR const& m,
                        std::unordered_map<std::uint16_t, FieldIR const*> const& field_by_tag,
                        std::vector<std::uint16_t> const& path,
                        std::vector<GroupOrderMember> const& members, PlanIntern& intern) {
    std::unordered_set<std::uint16_t> level_tags;
    for (auto const& gm : members) {
        level_tags.insert(gm.tag);
    }

    // Length+Data coupling map for THIS level, order-independent (a
    // pre-scan, so a Length field is coupled regardless of whether it is
    // declared before or after its Data partner — R7/FR-007a).
    std::unordered_map<std::uint16_t, std::uint16_t> length_to_data;
    std::unordered_set<std::uint16_t> data_tags_to_skip;
    for (auto const& gm : members) {
        if (gm.is_group) {
            continue;
        }
        auto const it = field_by_tag.find(gm.tag);
        if (it == field_by_tag.end()) {
            continue;
        }
        std::uint16_t const data_tag = it->second->ref.length_pair_data_tag;
        if (data_tag != 0 && level_tags.contains(data_tag)) {
            length_to_data.emplace(gm.tag, data_tag);
            data_tags_to_skip.insert(data_tag);
        }
    }

    LevelPlan plan;
    std::unordered_set<std::string> used_accessors;

    for (auto const& gm : members) {
        if (!gm.is_group && data_tags_to_skip.contains(gm.tag)) {
            continue;  // consumed by its Length partner (below)
        }
        if (gm.is_group) {
            GroupOrderEntry const* nested = find_group_entry(m, path, gm.tag);
            if (nested == nullptr) {
                continue;  // defensive; every group_order member has a matching entry.
            }
            auto const fit = field_by_tag.find(gm.tag);
            if (fit == field_by_tag.end()) {
                continue;  // defensive.
            }
            FieldIR const* gf = fit->second;
            std::string const stripped{strip_no_prefix(gf->name)};

            std::vector<std::uint16_t> child_path = path;
            child_path.push_back(gm.tag);
            LevelPlan child_members =
                resolve_level(m, field_by_tag, child_path, nested->members, intern);
            std::string const signature =
                compute_signature(nested->delimiter_tag, child_members, intern);
            std::size_t const plan_id =
                intern.intern(gm.tag, nested->delimiter_tag, signature, std::move(child_members));

            LevelItem item;
            item.is_group = true;
            item.accessor = uniquify_accessor(used_accessors, to_accessor(stripped), gm.tag);
            item.no_tag = gm.tag;
            item.delimiter_tag = nested->delimiter_tag;
            item.group_required = gf->ref.rule == fixpp::dict::field_presence::Required;
            item.plan_id = plan_id;
            plan.push_back(std::move(item));
            continue;
        }

        auto const fit = field_by_tag.find(gm.tag);
        if (fit == field_by_tag.end()) {
            continue;  // defensive.
        }
        FieldIR const* f = fit->second;

        auto const lit = length_to_data.find(gm.tag);
        if (lit != length_to_data.end()) {
            std::uint16_t const data_tag = lit->second;
            auto const dit = field_by_tag.find(data_tag);
            if (dit == field_by_tag.end()) {
                continue;  // defensive.
            }
            LevelItem item;
            item.is_group = false;
            item.tag = gm.tag;
            item.coupled = true;
            item.data_tag = data_tag;
            item.kind = TypeKind::String;
            item.accessor =
                uniquify_accessor(used_accessors, to_accessor(dit->second->name), data_tag);
            item.required = f->ref.rule == fixpp::dict::field_presence::Required ||
                            dit->second->ref.rule == fixpp::dict::field_presence::Required;
            plan.push_back(std::move(item));
            continue;
        }

        TypeKind const k = kind_of(f->ref.type);
        if (k == TypeKind::Skip) {
            continue;
        }
        LevelItem item;
        item.is_group = false;
        item.tag = gm.tag;
        item.kind = k;
        item.accessor = uniquify_accessor(used_accessors, to_accessor(f->name), gm.tag);
        item.required = f->ref.rule == fixpp::dict::field_presence::Required;
        plan.push_back(std::move(item));
    }
    return plan;
}

// 067 US3/T025 — emits, for ONE level (top-level `<Msg>Args` OR one shared
// `groups::G_...Args` plan), the required-field presence-check functions,
// the group count()/validate_entry() functions, and the `writer_traits<T>`
// specialization (data-model.md §1.4) that binds them — all THREE pieces
// sourced purely from `plan` (already derived from IR
// `FieldRef.rule`/`group_no_tag` by resolve_level — R3, no new IR/table
// source). MUST be called inside `namespace fixpp::wire { ... }` (writer_
// traits is declared there; an explicit specialization must be declared in
// a namespace enclosing its primary template's namespace — [temp.expl.spec]
// — `fixpp::<ns>` is a SIBLING of `fixpp::wire`, not an enclosing namespace,
// so this cannot be emitted from inside the `fixpp::<ns> { ... }` block).
// 077-builder-args-dedup T008 — the caller now supplies the fully-qualified
// `qtype` directly (a shared group plan's is `::fixpp::<ns>::groups::G_...`,
// a per-message top-level Args' is `::fixpp::<ns>::<Msg>Args`); `type_name`
// is the bare identifier stem used to derive this level's helper function
// names (no `::` — must be a valid identifier fragment). The CALLER is
// responsible for calling this exactly ONCE per distinct `plan` (once per
// interned group plan, once per message's top-level Args) — this is what
// makes G3/FR-003's "no ODR conflict" hold under dedup.
void emit_writer_traits_for_level(TemplateWriter& w, std::string const& qtype,
                                  std::string const& type_name, LevelPlan const& plan) {
    // Required-field presence-check functions for THIS level (top-level
    // body OR one group-entry level — both already framing-excluded /
    // level-scoped by construction of `plan`, R3/data-model §2).
    for (auto const& item : plan) {
        if (item.is_group || !item.required) {
            continue;
        }
        w.raw("inline bool ");
        w.raw(type_name);
        w.raw("_required_");
        w.num(item.tag);
        w.raw("(");
        w.raw(qtype);
        w.raw(" const& a) noexcept { return a.");
        w.raw(item.accessor);
        w.line(".has_value(); }");
    }

    // Group count()/validate_entry() functions for THIS level's own group
    // children (per-occurrence — data-model.md §2.2).
    for (auto const& item : plan) {
        if (!item.is_group) {
            continue;
        }
        w.raw("inline ::std::optional<::std::size_t> ");
        w.raw(type_name);
        w.raw("_count_");
        w.raw(item.accessor);
        w.raw("(");
        w.raw(qtype);
        w.raw(" const& a) noexcept { ");
        if (item.group_required) {
            w.raw("return a.");
            w.raw(item.accessor);
            w.line(".size(); }");
        } else {
            w.raw("if (!a.");
            w.raw(item.accessor);
            w.raw(") { return ::std::nullopt; } return a.");
            w.raw(item.accessor);
            w.line("->size(); }");
        }

        w.raw("inline ::fixpp::core::expected_t<void> ");
        w.raw(type_name);
        w.raw("_validate_entry_");
        w.raw(item.accessor);
        w.raw("(");
        w.raw(qtype);
        w.raw(" const& a, ::std::size_t i) noexcept { return ::fixpp::wire::validate_required(");
        if (item.group_required) {
            w.raw("a.");
            w.raw(item.accessor);
            w.line("[i]); }");
        } else {
            w.raw("(*a.");
            w.raw(item.accessor);
            w.line(")[i]); }");
        }
    }

    std::size_t const n_required =
        static_cast<std::size_t>(std::count_if(plan.begin(), plan.end(), [](LevelItem const& it) {
            return !it.is_group && it.required;
        }));
    std::size_t const n_groups = static_cast<std::size_t>(
        std::count_if(plan.begin(), plan.end(), [](LevelItem const& it) { return it.is_group; }));

    w.raw("template <> struct writer_traits<");
    w.raw(qtype);
    w.line("> {");

    w.raw("    static constexpr ::std::array<::fixpp::wire::required_check<");
    w.raw(qtype);
    w.raw(">, ");
    w.num(n_required);
    w.raw("> required_checks = ");
    if (n_required == 0) {
        w.line("{};");
    } else {
        w.line("{{");
        for (auto const& item : plan) {
            if (item.is_group || !item.required) {
                continue;
            }
            w.raw("        {");
            w.num(item.tag);
            w.raw(", &");
            w.raw(type_name);
            w.raw("_required_");
            w.num(item.tag);
            w.line("},");
        }
        w.line("    }};");
    }

    w.raw("    static constexpr ::std::array<::fixpp::wire::group_check<");
    w.raw(qtype);
    w.raw(">, ");
    w.num(n_groups);
    w.raw("> group_checks = ");
    if (n_groups == 0) {
        w.line("{};");
    } else {
        w.line("{{");
        for (auto const& item : plan) {
            if (!item.is_group) {
                continue;
            }
            w.raw("        {");
            w.raw(item.group_required ? "true" : "false");
            w.raw(", &");
            w.raw(type_name);
            w.raw("_count_");
            w.raw(item.accessor);
            w.raw(", &");
            w.raw(type_name);
            w.raw("_validate_entry_");
            w.raw(item.accessor);
            w.line("},");
        }
        w.line("    }};");
    }
    w.line("};");
    w.line();
}

}  // namespace

std::string emit_builders(VersionIR const& ir, CoverageMode mode) {
    // 077-builder-args-dedup T009 — the per-message, non-deduplicated Args
    // emitter that made 076 explode combinatorially on FIX Latest's depth-7
    // reused components (StandardHeader/Instrument/Underlying/Leg) is gone:
    // T008 introduced `PlanIntern`, which interns each distinct structural
    // group plan ONCE (per no_tag+signature) and emits it as a shared
    // `groups::G_<no_tag>[_ordN]Args` — the 53,590-struct/137MB blowup does
    // not recur. The v44-only gate is therefore removed; this is now a
    // single version-agnostic emitter. Per-version app-message scoping is
    // handled entirely by the `in_scope` predicate below (already existed
    // pre-T009) — no namespace/`ir.ns` gate is reintroduced. See
    // specs/077-builder-args-dedup/spec.md + research.md.
    TemplateWriter w;
    w.line("// SPDX-License-Identifier: AGPL-3.0-or-later");
    w.line("// GENERATED by fixpp-codegen (067-codegen-writer-emitter). DO NOT EDIT.");
    w.line("// <vXX>/Builders.hpp — generated typed write builders over");
    w.line("// wire::body_builder (data-model.md §1-§3;");
    w.line("// contracts/generated-builder.md G1-G4/G9).");
    w.line("#pragma once");
    w.line("#include <array>");
    w.line("#include <cstddef>");
    w.line("#include <cstdint>");
    w.line("#include <fixpp/core/decimal_alias.hpp>");
    w.line("#include <fixpp/core/error.hpp>");
    w.line("#include <fixpp/wire/body_builder.hpp>");
    w.line("#include <fixpp/wire/builder_validate.hpp>");
    w.line("#include <optional>");
    w.line("#include <span>");
    w.line("#include <string_view>");
    w.line();

    // 077-builder-args-dedup T008 — PASS 1: discover + intern every distinct
    // group structural plan across the in-scope message set, in `ir.messages`
    // order (already bytewise-sorted) x declaration-order `group_order`
    // (deterministic — data-model Entity 1/2). Nothing is written to `w` yet.
    PlanIntern intern;
    std::vector<std::string> registry_msg_types;
    std::vector<std::string> official_msg_ids;
    std::vector<LevelPlan> top_level_plans;  // parallel to official_msg_ids

    for (auto const& m : ir.messages) {
        // 069-v44-all-families (data-model.md Entity "Coverage mode"):
        // `official` reproduces the frozen kOfficial33-only gate byte-for-
        // byte (FR-005/SC-003); `all` widens to every application message
        // minus the N-002/N-003 exclusion set (FR-002/FR-003).
        bool const in_scope = mode == CoverageMode::Official
                                  ? is_official(m.msg_type)
                                  : (m.is_application && !is_n002_n003_excluded(ir.ns, m.msg_type));
        if (!in_scope) {
            continue;
        }
#ifdef FIXPP_CODEGEN_DROP_BUILDER_MSGTYPE
        // 077-builder-args-dedup T024 (C3b) -- THE committed mutation seam
        // for the builder-completeness census's (T023) proven-red witness
        // (T025). Compiled ONLY into the fixpp-codegen-drop-witness binary
        // (tools/codegen/fixpp-codegen/CMakeLists.txt), never the normal
        // fixpp-codegen: drops exactly one already-in-scope message from
        // the emitted builders + registry, so
        // builder_completeness_mutation_witness_test.cpp can observe the
        // completeness census go RED with the expected missing msg_type.
        if (m.msg_type == FIXPP_CODEGEN_DROP_BUILDER_MSGTYPE) {
            continue;
        }
#endif
        std::unordered_map<std::uint16_t, FieldIR const*> field_by_tag;
        for (auto const& f : m.fields) {
            field_by_tag.emplace(f.ref.tag, &f);
        }

        std::string const msg_id = to_identifier(m.name);
        std::vector<GroupOrderMember> const top_members =
            top_level_synthetic_members(m, ir.header_trailer_tags);
        LevelPlan plan = resolve_level(m, field_by_tag, /*path=*/{}, top_members, intern);

        registry_msg_types.push_back(m.msg_type);
        official_msg_ids.push_back(msg_id);
        top_level_plans.push_back(std::move(plan));
    }

    // 077-builder-args-dedup T009 (G4a) — vt11 has 0 application messages, so
    // `in_scope` above never fires and the registry stays empty. Discard the
    // header-only `w` content written above and return truly empty so
    // `main.cpp`'s `write_file` empty-skip (content.empty()) leaves no
    // `Builders.hpp` on disk for vt11, instead of an empty-but-nonempty-bytes
    // file (empty groups namespace + a 0-entry builder_registry).
    if (registry_msg_types.empty()) {
        return {};
    }

    // PASS 2 — name assignment (bare vs ordinaled, G1a): must run only after
    // discovery is complete (the final per-no_tag distinct-signature count is
    // now known).
    assign_plan_names(intern);

    // PASS 3 — reference resolution (G1/G2): fill every group item's
    // `group_type_name` now that every plan's final name is known. A shared
    // plan's OWN members reference their (already-interned) children
    // unqualified — both sides live in `fixpp::<ns>::groups`; a per-message
    // top-level Args references a shared plan `groups::`-qualified.
    for (auto& p : intern.plans) {
        fill_group_type_names(p.members, intern, /*qualified=*/false);
    }
    for (auto& plan : top_level_plans) {
        fill_group_type_names(plan, intern, /*qualified=*/true);
    }

    // PASS 4a — emit every distinct group plan exactly once, in
    // `fixpp::<ns>::groups` (G1). `intern.plans` is already children-before-
    // parents post-order by construction (a child is always interned before
    // the parent whose signature embeds it — G1c/FR-011).
    w.raw("namespace fixpp::");
    w.raw(ir.ns);
    w.line("::groups {");
    w.line();
    for (auto const& p : intern.plans) {
        emit_args_struct(w, p.name, p.members);
    }
    w.raw("}  // namespace fixpp::");
    w.raw(ir.ns);
    w.line("::groups");
    w.line();

    // PASS 4b — per-message top-level `<Msg>Args` + `build_<Msg>` (G2,
    // unchanged shape/serialize-logic; only the referenced group type names
    // changed, resolved in PASS 3).
    w.raw("namespace fixpp::");
    w.raw(ir.ns);
    w.line(" {");
    w.line();
    for (std::size_t i = 0; i < official_msg_ids.size(); ++i) {
        std::string const& msg_id = official_msg_ids[i];
        LevelPlan const& plan = top_level_plans[i];
        emit_args_struct(w, msg_id + "Args", plan);
        emit_build_fn(w, msg_id, registry_msg_types[i], plan, intern);
    }

    w.line("struct builder_registry_entry { ::std::string_view msg_type; };");
    w.raw("inline constexpr ::std::array<builder_registry_entry, ");
    w.num(registry_msg_types.size());
    w.line("> builder_registry = {{");
    for (auto const& mt : registry_msg_types) {
        w.raw("    {\"");
        w.raw(mt);
        w.line("\"},");
    }
    w.line("}};");
    w.line();

    w.raw("}  // namespace fixpp::");
    w.line(ir.ns);
    w.line();

    // PASS 4c (067 US3/T025, data-model.md §1.3/§1.4, G5) — writer_traits<T>
    // specializations + their required-field/group helper functions, ONCE
    // PER DISTINCT PLAN (G3/FR-003 — no ODR conflict under dedup), shared
    // group plans FIRST (post-order — a plan's child's writer_traits must be
    // a complete specialization before the parent's `_validate_entry_`
    // function, which instantiates `validate_required<Child>`, is defined —
    // [temp.expl.spec]), then each message's own top-level Args (which may
    // reference a shared plan's writer_traits via its group_check). MUST
    // live in `fixpp::wire` (writer_traits's own namespace) and MUST come
    // after `fixpp::<ns>`/`fixpp::<ns>::groups` close (the Args types it
    // specializes over must already be complete) and BEFORE the
    // validate_<Msg> thin wrappers below.
    w.line("namespace fixpp::wire {");
    w.line();
    for (auto const& p : intern.plans) {
        std::string const qtype = "::fixpp::" + std::string{ir.ns} + "::groups::" + p.name;
        emit_writer_traits_for_level(w, qtype, p.name, p.members);
    }
    for (std::size_t i = 0; i < official_msg_ids.size(); ++i) {
        std::string const type_name = official_msg_ids[i] + "Args";
        std::string const qtype = "::fixpp::" + std::string{ir.ns} + "::" + type_name;
        emit_writer_traits_for_level(w, qtype, type_name, top_level_plans[i]);
    }
    w.line("}  // namespace fixpp::wire");
    w.line();

    // validate_<Msg> — the per-message public entry point (FR-006), a THIN
    // wrapper over the generic ::fixpp::wire::validate_required<T> walk
    // (builder_validate.hpp, T024) driven by the writer_traits<T>
    // specialization just emitted above.
    w.raw("namespace fixpp::");
    w.raw(ir.ns);
    w.line(" {");
    w.line();
    for (auto const& msg_id : official_msg_ids) {
        w.raw("inline ::fixpp::core::expected_t<void> validate_");
        w.raw(msg_id);
        w.raw("(");
        w.raw(msg_id);
        w.line("Args const& args) noexcept {");
        w.line("    return ::fixpp::wire::validate_required(args);");
        w.line("}");
    }
    w.line();
    w.raw("}  // namespace fixpp::");
    w.line(ir.ns);
    return std::move(w).take();
}

}  // namespace fixpp::codegen
