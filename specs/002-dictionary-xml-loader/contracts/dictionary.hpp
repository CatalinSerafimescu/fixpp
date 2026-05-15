// SPDX-License-Identifier: AGPL-3.0-or-later
// extract from .specify/2c-codegen.md v1.3 §4.3 (signed off 2026-05-10) —
// LOADER-MVS SUBSET ONLY. This file is a /plan Phase 1 contract extract; the
// canonical declaration lives at include/fixpp/dict/dictionary.hpp once
// 002-dictionary-xml-loader reaches /implement.
//
// What this PR's `Dictionary` exposes (loader-MVS surface, per spec.md §5):
//   - the public lookup methods AC-D1..AC-D7 expect, in both their canonical
//     `[2c §4.3]` names AND the spec.md §4.2 descriptive aliases (see
//     research.md D-20 — both surfaces ship; descriptive names are thin
//     wrappers).
//   - move-only value semantics with heap-pinned metadata handle per
//     `[2c §4.3]` / RC-3.
//
// What is OUT of scope in this PR:
//   - `Dictionary::with_overlay(...)` — deferred with F2 (spec.md §10).
//   - `Dictionary::as_table_view()` — deferred; `dict::table_view` itself
//     is out of scope per spec.md §5.
//   - `Dictionary::resolve_application_version(...)` — deferred to the
//     wire/session integration feature where FIXT.1.1 cross-vocabulary
//     dispatch is implemented.
//   - `Dictionary::was_dialect_promoted(...)` — N/A without overlay
//     support; deferred with F2.

#pragma once

#include <fixpp/dict/component_ref.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/group_ref.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>

namespace fixpp::dict {

namespace detail {
    // Heap-pinned metadata block. Holds the PMR-allocated FieldRef /
    // ComponentRef / GroupRef tables plus the message-type list and the
    // FIX version this `Dictionary` was loaded for. Stable address across
    // `Dictionary` moves per `[2c §4.3]` / RC-3.
    //
    // In v1.0 (this PR), every byte of `dict_metadata_handle` and the
    // tables it owns is allocated through `std::allocate_shared` over
    // `std::pmr::polymorphic_allocator<dict_metadata_handle>` so the
    // shared-control-block deallocator returns memory to the originating
    // PMR resource. Per `[2c §4.3]` / C-R2-P1-1.
    //
    // Reserved internal-layout slot for F2 (DialectOverlay, spec.md §10):
    // `[2c §4.3]` (canonical at `2c-codegen.md:1676`) requires a
    // `base_keepalive_` slot inside the merged-handle of a
    // `Dictionary::with_overlay(...)` result — a copy of the base's
    // `handle_` so the base's metadata lifetime is pinned by the merged
    // handle. The loader-MVS form here OMITS that slot because
    // `with_overlay` is deferred to F2. When F2 lands, the slot will be
    // added to `dict_metadata_handle`'s private layout; this is a
    // private-implementation change with no public-API impact, but is
    // flagged here so the F2 author is not surprised by the layout
    // growth.
    class dict_metadata_handle;

    // Loader-MVS form of `dict_metadata_handle_ptr`: a shared_ptr<const>
    // over the heap-pinned metadata. Move is no-throw and touches no
    // atomics (matches `unique_ptr` move semantics on the hot path).
    using dict_metadata_handle_ptr =
        std::shared_ptr<dict_metadata_handle const>;
}  // namespace detail

// Per-message entry returned by `Dictionary::messages()`. The MsgType
// string aliases the metadata-handle's name-string pool — same lifetime
// as the `Dictionary`'s metadata block (survives `Dictionary` moves).
struct MessageEntry {
    std::string_view msg_type;  // FIX MsgType string (e.g., "D" for
                                // NewOrderSingle).
    std::string_view name;      // English message name (e.g.,
                                // "NewOrderSingle"). Diagnostics-only.
};

class Dictionary {
public:
    // Lifetime: typically constructed once at engine init from
    // `XmlLoader::load(...)`, then frozen for the session's lifetime per
    // `[arch §5.6]`.
    //
    // Move-only per `[2c §4.3]`; copying is deleted (would silently
    // duplicate the metadata-handle storage; deep-copy is available via
    // a separate explicit method if ever needed, deferred to a future
    // feature).
    Dictionary(Dictionary const&) = delete;
    Dictionary& operator=(Dictionary const&) = delete;
    Dictionary(Dictionary&&) noexcept = default;
    Dictionary& operator=(Dictionary&&) noexcept = default;
    ~Dictionary();

    // -----------------------------------------------------------------
    // Version accessors
    // -----------------------------------------------------------------

    // Returns the FIX session version this Dictionary was loaded for.
    // Derived from the XML's `<fix major minor [servicepack]>` header at
    // load time per AC-L4 (rejected with `dict::unknown_version_error` if
    // outside the v1.0 supported nine).
    //
    // The full `version_profile` struct from `[2c §4.3]` (carrying both
    // session and default-application versions plus the
    // has-per-message-override flag) is deferred to the wire/session
    // integration feature; in this PR `Dictionary` only carries the
    // session version since the FIXT.1.1 application-resolution algorithm
    // is downstream of the loader.
    [[nodiscard]] session_version which_session_version() const noexcept;

    // -----------------------------------------------------------------
    // Canonical lookup surface — from `[2c §4.3]` directly
    // -----------------------------------------------------------------

    // Look up a single field by tag in the *MsgType context*. Returns the
    // composed FieldRef; `presence` reflects the rule for this message
    // type. Returns a FieldRef with `rule == field_presence::NotDeclared`
    // if the tag is not part of this MsgType's grammar.
    //
    // Per `[2c §4.3]`. AC-D2 satisfied.
    [[nodiscard]] FieldRef
    field_ref(std::string_view msg_type,
              std::uint16_t tag) const noexcept;

    // Required-field set for a given MsgType. Returned span aliases the
    // metadata-handle storage; lifetime is the metadata block's (i.e.,
    // survives `Dictionary` moves; ends when this `Dictionary` is
    // destroyed).
    //
    // Per `[2c §4.3]`. Used by `wire::Validator` once that feature ships;
    // also exercised by AC-D2's required-rule assertions in this PR.
    [[nodiscard]] std::span<std::uint16_t const>
    required_fields(std::string_view msg_type) const noexcept
        [[clang::lifetimebound]];

    // Is `tag` declared for `msg_type` per the loaded dictionary?
    // Equivalent to `field_ref(msg_type, tag).rule != NotDeclared`.
    [[nodiscard]] bool
    field_valid_for(std::string_view msg_type,
                    std::uint16_t tag) const noexcept;

    // First-field-of-group rule per `[FIX50SP2 §3]`. Returns 0 if
    // `no_tag` is not a `NoXxx` tag in this dictionary.
    [[nodiscard]] std::uint16_t
    group_first_field(std::uint16_t no_tag) const noexcept;

    // Length+Data pair lookup. Returns the paired DATA tag for a
    // LENGTH-typed field, or 0 if `length_tag` is not a paired LENGTH.
    [[nodiscard]] std::uint16_t
    length_pair_data_tag(std::uint16_t length_tag) const noexcept;

    // -----------------------------------------------------------------
    // Spec.md §4.2 descriptive aliases (AC-D2..D5) — thin wrappers over
    // the canonical surface. Both forms ship in v1.0 per research.md D-20.
    //
    // The context-free `field(std::uint16_t)` short form was removed in
    // Gate A round 1 (per Opus root cause #2): one `FieldRef` exists per
    // `(MsgType, tag)` pair, not per global tag, so a context-free lookup
    // has no canonical answer (e.g., `OrderID(37)` appears in both
    // `ExecutionReport` and `OrderCancelRequest` with potentially
    // different `rule`s per `[2c §4.1]`). AC-D1 was rewritten in spec.md
    // round 1 to canonicalize on `field_ref(msg_type, tag)` above.
    // -----------------------------------------------------------------

    // AC-D2: `Dictionary::field(MsgType, uint16_t tag)` — descriptive
    // alias for `field_ref(msg_type, tag)`. Returns `std::nullopt` if
    // `field_ref(...).rule == field_presence::NotDeclared`. AC-D1 also
    // resolves to this overload (the spec's AC-D1 short form is removed;
    // see spec.md round-1 AC-D1 update).
    [[nodiscard]] std::optional<FieldRef>
    field(std::string_view msg_type,
          std::uint16_t tag) const noexcept;

    // AC-D3: tag-by-name lookup. Case-sensitive exact match against the
    // XML `<field name="...">` attribute per research.md D-11 / spec.md
    // §A2. Returns `std::nullopt` if `name` is not a declared field
    // name.
    [[nodiscard]] std::optional<std::uint16_t>
    field_by_name(std::string_view name) const noexcept;

    // AC-D4: component lookup by name. Returns the ComponentRef for the
    // named component, or `std::nullopt` if not declared. Same
    // case-sensitivity discipline as `field_by_name`.
    [[nodiscard]] std::optional<ComponentRef>
    component(std::string_view name) const noexcept;

    // AC-D4: group lookup by delimiter tag. Returns the GroupRef for the
    // group whose `NoXxx` delimiter equals `no_tag`, or `std::nullopt` if
    // not declared.
    [[nodiscard]] std::optional<GroupRef>
    group(std::uint16_t no_tag) const noexcept;

    // AC-D5: iterable view of `(MsgType, message-name)` pairs sorted by
    // MsgType (per research.md D-6 determinism). Returned span aliases
    // the metadata-handle storage.
    [[nodiscard]] std::span<MessageEntry const>
    messages() const noexcept [[clang::lifetimebound]];

    // -----------------------------------------------------------------
    // AC-D4 runtime extension — component / group field-list walks
    // (Gate B round 2 addition, 2026-05-15)
    // -----------------------------------------------------------------

    // Walk a component's contiguous field list by name. Returns a span over
    // the per-component FieldRef side table; empty if `name` is not declared
    // or the component has no fields. Returned span aliases metadata storage.
    //
    // NOTE: first_field_index on ComponentRef indexes into THIS side table
    // (not the per-MsgType-concatenated fields_ array that field_ref() uses).
    // See spec.md §Clarifications Q4 for the runtime-vs-codegen layout note.
    [[nodiscard]] std::span<FieldRef const>
    component_fields(std::string_view name) const noexcept [[clang::lifetimebound]];

    // Walk a group's contiguous field list by NoXxx tag. Returns a span over
    // the per-group FieldRef side table; empty if `no_tag` is not a declared
    // group delimiter or the group has no recorded fields.
    [[nodiscard]] std::span<FieldRef const>
    group_fields(std::uint16_t no_tag) const noexcept [[clang::lifetimebound]];

    // -----------------------------------------------------------------
    // Construction by `XmlLoader` (friend) — public ctor is not in scope.
    // -----------------------------------------------------------------
private:
    friend class XmlLoader;
    // Constructed from inside XmlLoader::load*; the explicit signature is
    // implementation-private and lives in the .cpp file.
    Dictionary() noexcept = default;

    detail::dict_metadata_handle_ptr handle_;
};

}  // namespace fixpp::dict
