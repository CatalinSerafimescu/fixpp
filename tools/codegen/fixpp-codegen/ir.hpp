// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/ir.hpp
//
// 003-dictionary-codegen — T014. XML→IR: F1 Candidate A reuses the merged 002
// XmlLoader/Dictionary as the single XML truth (no second QuickFIX-XML parser
// — research.md D-1). build_ir() calls XmlLoader::load(path, mr) and walks the
// Dictionary metadata into a deterministic, codegen-ready IR. Build-only host
// tool ([const §III.5]).
//
// Foundational-checkpoint scope: the IR captures version + bytewise-sorted
// message list + Length+Data pairs (the determinism-relevant spine). Full
// per-message field/group enumeration is completed with the US1 emitters
// (T023-T027), which extend this IR.
#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <memory_resource>
#include <string>
#include <vector>

namespace fixpp::codegen {

// One declared field of a message (RC#5 — sourced via the additive
// Dictionary::message_fields / Dictionary::field_name accessors; D-24).
// Carries the full 002-shipped FieldRef verbatim (16-byte trivially-copyable
// POD) so emit_fields can emit complete constexpr FieldRef arrays (AC-V1/V6)
// and emit_messages can reconstruct repeating-group structure from
// FieldRef::group_no_tag (set by XmlLoader to the enclosing group's no_tag;
// 0 == top level) + FieldRef::type == NumInGroup (the delimiter). `name` is
// the FIX field name used to emit the typed accessor identifier.
struct FieldIR {
    fixpp::dict::FieldRef ref{};
    std::string name;  // FIX field name, e.g. "ClOrdID"
};

// One member of a repeating-group occurrence's DECLARATION-order member list
// (067-codegen-writer-emitter T004/R9). `is_group` marks a nested repeating
// group (its own occurrence lives elsewhere in MessageIR::group_order, keyed
// by this entry's no_tag appended to the current occurrence's parent_path).
struct GroupOrderMember {
    std::uint16_t tag = 0;
    bool is_group = false;
};

// One repeating-group OCCURRENCE within a single message, keyed
// (message [implicit — owning MessageIR], parent_path, no_tag) — mirrors
// 063's per-context keying. `parent_path` is the chain of enclosing groups'
// no_tags from outermost to (excluding) this group; empty for a top-level
// group. `delimiter_tag` = the group's first DECLARED member (research R9,
// R7 delimiter note) = `members.front().tag` when non-empty. `members` is
// this level's OWN members in raw XML declaration order (component refs
// resolved inline; a nested `<group>` child contributes ONE marker member
// here with `is_group=true`, and its own members are a SEPARATE
// GroupOrderEntry in this same vector, recursively). NOT tag-sorted, NOT
// tag-deduped (side-steps the `append_run` collapse — N3/plan.md).
struct GroupOrderEntry {
    std::vector<std::uint16_t> parent_path;
    std::uint16_t no_tag = 0;
    std::uint16_t delimiter_tag = 0;
    std::vector<GroupOrderMember> members;
};

// 076-fix-latest-typed-codegen T005 (research R2b output 4 / data-model.md
// Entity 3, "Emitted set"): one field/count-field OCCURRENCE within a
// single message's raw declaration-order structure — the lossless
// occurrence-path record the census manifest (T012) is built from.
// Distinct from FieldIR (tag-deduped; `FieldRef::group_no_tag` is a single
// immediate-parent tag, so it cannot express a multi-level `group_path` or
// a tag reused under different parents at depth > 1): a tag occurring twice
// under two different group parents produces two OccurrenceIR entries here,
// each with its own `group_path`. `group_path` is the chain of enclosing
// groups' no_tags from outermost to innermost (empty = top-level) — the
// SAME parent-path convention as `GroupOrderEntry::parent_path` above.
// Populated only for an Orchestra-sourced `VersionIR` (empty for `<fix>`-
// schema versions — the census manifest is vlatest-only per research R2b).
struct OccurrenceIR {
    std::vector<std::uint16_t> group_path;
    std::uint16_t tag = 0;
    fixpp::dict::field_presence rule{};
    fixpp::dict::field_data_type datatype{};
};

struct MessageIR {
    std::string msg_type;         // FIX MsgType (e.g. "D")
    std::string name;             // English name (diagnostics / NormativeRefs)
    std::vector<FieldIR> fields;  // full per-message run (required + optional)

    // 069-v44-all-families T003: the <message msgcat='app'|'admin'> XML
    // attribute (data-model.md Entity 1). Populated by build_ir()'s
    // codegen-tool-local pugixml re-parse (ir.cpp); every <message> in a
    // FIX44 dictionary MUST carry msgcat — a message missing it (or carrying
    // an unrecognized value) is a build_ir() loader error (fail-closed, no
    // default-guess — data-model.md Entity 1 Validation). For an Orchestra-
    // sourced VersionIR this is instead derived from `category=` (data-model
    // Entity 5): `category=="Session"` -> false (admin); anything else ->
    // true (app) — same fail-closed discipline, different source attribute.
    bool is_application = false;

    // Codegen-tool-local declaration-order group plan (067 T004/R9): one
    // GroupOrderEntry per repeating-group occurrence rooted at THIS
    // message's own XML definition (recursive to every nesting depth). NOT
    // derivable from `fields` above, which the loader tag-sorts + tag-dedups
    // (xml_loader.cpp:695-702) — declaration order is lost there. Populated
    // by build_ir()'s codegen-tool-local pugixml re-parse (ir.cpp, T008).
    // Codegen-tool-local only: no runtime Dictionary/GroupRef/C-ABI change
    // (FR-009 intact).
    std::vector<GroupOrderEntry> group_order;

    // 076-fix-latest-typed-codegen T005 (research R2b output 4): the
    // lossless per-occurrence record for THIS message — populated ONLY by
    // the Orchestra-native projection (`populate_orchestra_projection`,
    // ir.cpp), empty for `<fix>`-schema versions. Feeds the T012 census
    // manifest; NOT sourced from `fields` (lossy — see OccurrenceIR banner).
    std::vector<OccurrenceIR> occurrences;
};

struct LengthPairIR {
    std::uint16_t length_tag;
    std::uint16_t data_tag;
};

struct VersionIR {
    fixpp::dict::session_version session{};
    fixpp::dict::application_version application{};
    std::string ns;                   // "v42" / "v44" / "v50sp2" / "vt11"
    std::vector<MessageIR> messages;  // bytewise-sorted (002 D-6)
    std::vector<LengthPairIR> length_pairs;

    // Tags declared under the top-level <header>/<trailer> elements,
    // resolved recursively through <component>/<group> refs (sorted,
    // unique) — excluded from body-only <Msg>Args by provenance (the write
    // emitter's header/trailer-exclusion follow-up; supersedes the 8-tag
    // kFramingTags floor as the primary exclusion set).
    std::vector<std::uint16_t> header_trailer_tags;

    // 082-structural-group-detection D-3/C1.2: the structural repeating-
    // group-count-tag set for this version — sorted, unique union of
    // `{e.no_tag : e ∈ m.group_order}` over every message, PLUS any
    // <header>/<trailer> (StandardHeader/StandardTrailer)-declared group
    // no_tags (populate_group_order/populate_orchestra_projection union
    // those in directly — group_order's own walk is deliberately body-only,
    // INV-2, but the read-tier emitters test membership against
    // `m.fields`, which IS header/trailer-inclusive via
    // `dict.message_fields()`; NoHops(627) in FIX44's/FIXT11's own
    // <header> is the confirmed case). Every emitter discovery/branch site
    // that used to test `FieldRef::type == NumInGroup` consults THIS set
    // instead (D-7); `FieldRef::type` itself is not modified (D-4).
    std::vector<std::uint16_t> group_tags;
};

// Per-message top-level fields (group_no_tag == 0), deduped by tag in
// first-encounter order. Shared by emit_messages.cpp + emit_reify.cpp (was
// a verbatim inline block in each); the returned pointers alias `m.fields`.
[[nodiscard]] std::vector<FieldIR const*> collect_top_fields(MessageIR const& m);

// D-3/D-7: the single membership test every emitter discovery/branch site
// uses in place of `FieldRef::type == NumInGroup`. `ir.group_tags` is
// sorted+unique (build_ir()), so a binary search suffices.
[[nodiscard]] inline bool is_group_tag(VersionIR const& ir, std::uint16_t tag) noexcept {
    return std::binary_search(ir.group_tags.begin(), ir.group_tags.end(), tag);
}

// Loads `xml_path` via XmlLoader and projects the Dictionary into VersionIR.
// Throws dict::xml_* on malformed input (host tool — exceptions allowed,
// [arch §5.3] / [const §III.5]); the user-facing library hot path is
// unaffected. `mr` backs the loader's PMR allocations.
[[nodiscard]] VersionIR build_ir(std::filesystem::path const& xml_path,
                                 std::pmr::memory_resource* mr);

}  // namespace fixpp::codegen
