// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/dictionary.hpp
//
// `fixpp::dict::Dictionary` — move-only owner of FIX-data-dictionary
// metadata. Canonical declaration; mirrors the loader-MVS subset of
// `[2c §4.3]` recorded at
// `specs/002-dictionary-xml-loader/contracts/dictionary.hpp` and the
// data-model.md Entity 4 entry. Constructed by `fixpp::dict::XmlLoader::load*`.
//
// Loader-MVS surface in v1.0 (this PR, per spec.md §5):
//   - Canonical lookup methods AC-D1..AC-D7 expect (`field_ref`,
//     `required_fields`, `field_valid_for`, `group_first_field`,
//     `length_pair_data_tag`).
//   - spec.md §4.2 descriptive aliases (`field`, `field_by_name`,
//     `component`, `group`, `messages`).
//   - Move-only value semantics with heap-pinned metadata handle.
//
// OUT of scope this PR: `with_overlay`,
// `resolve_application_version`, `was_dialect_promoted` — deferred.
// `as_table_view()` implemented in 041-validation-gate-wiring (T008).

#pragma once

#include <cstdint>
#include <fixpp/dict/component_ref.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/group_ref.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace fixpp::dict {

class XmlLoader;

namespace detail {
// Heap-pinned metadata block. Defined in src/dictionary/dictionary.cpp.
// Allocated via `std::allocate_shared` over
// `std::pmr::polymorphic_allocator<dict_metadata_handle>` so the
// shared-control-block deallocator returns memory to the originating PMR
// resource per `[2c §4.3]` / C-R2-P1-1.
class dict_metadata_handle;

// shared_ptr<const> over the heap-pinned metadata. Move is no-throw and
// touches no atomics.
using dict_metadata_handle_ptr = std::shared_ptr<dict_metadata_handle const>;
}  // namespace detail

// Per-message entry returned by `Dictionary::messages()`. The string_views
// alias the metadata-handle's name-string pool.
struct MessageEntry {
    // cppcheck-suppress unusedStructMember  // read by tests via Dictionary::messages()
    std::string_view msg_type;  // FIX MsgType string (e.g., "D" for
                                // NewOrderSingle).
    // cppcheck-suppress unusedStructMember  // public surface (data-model.md): diagnostics
    std::string_view name;  // English message name. Diagnostics-only.
};

// 074-orchestra-native-reader (FR-002): one enumerated {value, description}
// pair of a codeset-backed field, returned by `Dictionary::enum_values(tag)`.
// The string_views alias the metadata-handle's name-string pool. `description`
// is the Orchestra `<fixr:code name=...>` symbolic name (the
// QuickFIX-`description`-equivalent). Populated by OrchestraLoader AND, since
// 075-live-wire-enum-validation, by XmlLoader (all nine QuickFIX-XML
// dictionaries also carry their declared `<value>` code sets now).
struct EnumValueRef {
    std::string_view value;        // the `<fixr:code value=...>` wire bytes
    std::string_view description;  // the `<fixr:code name=...>` symbolic name
};

class Dictionary {
public:
    // Move-only per `[2c §4.3]`; copying deleted (would silently duplicate
    // the metadata-handle storage).
    Dictionary(Dictionary const&) = delete;
    Dictionary& operator=(Dictionary const&) = delete;
    Dictionary(Dictionary&&) noexcept = default;
    Dictionary& operator=(Dictionary&&) noexcept = default;
    ~Dictionary();

    // ---- Version accessors ----

    // Returns the FIX session version this Dictionary was loaded for.
    // Derived from `<fix major minor [servicepack]>` at load time per AC-L4
    // (rejected with `dict::unknown_version_error` if outside the v1.0
    // supported nine). Never returns `Unknown` after a successful load.
    [[nodiscard]] session_version which_session_version() const noexcept;

    // ---- Canonical lookup surface — from `[2c §4.3]` ----

    // Look up a single field by tag in the *MsgType context*. Returns a
    // composed FieldRef; `rule == NotDeclared` if the tag is not part of
    // this MsgType's grammar. Per AC-D1.
    [[nodiscard]] FieldRef field_ref(std::string_view msg_type, std::uint16_t tag) const noexcept;

    // Required-field set for a given MsgType. Returned span aliases the
    // metadata-handle storage (survives `Dictionary` moves; ends when this
    // `Dictionary` is destroyed).
    [[nodiscard]] std::span<std::uint16_t const> required_fields(
        std::string_view msg_type) const noexcept [[clang::lifetimebound]];

    // Is `tag` declared for `msg_type` per the loaded dictionary?
    // Equivalent to `field_ref(msg_type, tag).rule != NotDeclared`.
    [[nodiscard]] bool field_valid_for(std::string_view msg_type, std::uint16_t tag) const noexcept;

    // First-field-of-group rule per `[FIX50SP2 §3]`. Returns 0 if `no_tag`
    // is not a `NoXxx` tag in this dictionary.
    [[nodiscard]] std::uint16_t group_first_field(std::uint16_t no_tag) const noexcept;

    // Length+Data pair lookup. Returns the paired DATA tag for a
    // LENGTH-typed field, or 0 if `length_tag` is not a paired LENGTH.
    [[nodiscard]] std::uint16_t length_pair_data_tag(std::uint16_t length_tag) const noexcept;

    // ---- spec.md §4.2 descriptive aliases ----

    // AC-D2: descriptive alias for `field_ref(msg_type, tag)`. Returns
    // `std::nullopt` if `field_ref(...).rule == NotDeclared`.
    [[nodiscard]] std::optional<FieldRef> field(std::string_view msg_type,
                                                std::uint16_t tag) const noexcept;

    // AC-D3: tag-by-name lookup. Case-sensitive exact match against the
    // XML `<field name="...">` attribute per research.md D-11.
    [[nodiscard]] std::optional<std::uint16_t> field_by_name(std::string_view name) const noexcept;

    // AC-D4: component lookup by name. `std::nullopt` if not declared.
    [[nodiscard]] std::optional<ComponentRef> component(std::string_view name) const noexcept;

    // AC-D4: group lookup by delimiter tag. `std::nullopt` if not declared.
    [[nodiscard]] std::optional<GroupRef> group(std::uint16_t no_tag) const noexcept;

    // AC-D5: iterable view of `(MsgType, message-name)` pairs sorted by
    // MsgType bytewise (per research.md D-6 determinism). Returned span
    // aliases the metadata-handle storage.
    [[nodiscard]] std::span<MessageEntry const> messages() const noexcept [[clang::lifetimebound]];

    // AC-D4 runtime extension: walk a component's contiguous field list.
    // Returns a span over the per-component FieldRef side table maintained
    // by XmlLoader. The span is empty if `name` is not declared or the
    // component has no fields.
    //
    // NOTE on index space: ComponentRef::first_field_index and field_count
    // index into THIS side table — NOT into the per-MsgType-concatenated
    // fields_ array that field_ref() searches. The XmlLoader's per-MsgType-
    // concatenated layout (supporting O(log N) field_ref lookups per [2c
    // §6.2]) does not produce per-component contiguity, so a dedicated flat
    // side table is used for component/group field walks (runtime-MVS shape;
    // codegen-emitted per-version headers use a different layout where
    // first_field_index indexes the per-version Fields.hpp array directly).
    [[nodiscard]] std::span<FieldRef const> component_fields(std::string_view name) const noexcept
        [[clang::lifetimebound]];

    // AC-D4 runtime extension: walk a group's contiguous field list.
    // Returns a span over the per-group FieldRef side table maintained by
    // XmlLoader. The span is empty if `no_tag` is not a declared group
    // delimiter tag or the group has no recorded fields.
    //
    // Same index-space note as component_fields() above: GroupRef::
    // first_field_index indexes into this side table, not the per-MsgType
    // fields_ array.
    [[nodiscard]] std::span<FieldRef const> group_fields(std::uint16_t no_tag) const noexcept
        [[clang::lifetimebound]];

    // 003-dictionary-codegen (RC#5 — F1 IR data path). Additive, source-
    // compatible read accessors the `fixpp-codegen` host tool consumes to
    // emit per-version typed messages ([arch §9.3] stable-from-v1.0;
    // [const §X.4]-style additive — no existing slot/signature changed).
    // Build-time codegen-enumeration only; NOT a runtime hot path.
    //
    // Full per-message field run (required + optional, in the per-MsgType
    // concatenated `fields_` order — distinct from required_fields(), which
    // is required-only). Empty span if `msg_type` is not declared. Aliases
    // the metadata-handle storage (lifetime = this Dictionary).
    [[nodiscard]] std::span<FieldRef const> message_fields(std::string_view msg_type) const noexcept
        [[clang::lifetimebound]];

    // FIX field name for `tag` (e.g. 11 → "ClOrdID"), used to emit named
    // typed accessors. Empty view if `tag` is unknown to this dictionary.
    // Aliases the metadata-handle name pool (lifetime = this Dictionary).
    [[nodiscard]] std::string_view field_name(std::uint16_t tag) const noexcept
        [[clang::lifetimebound]];

    // 074-orchestra-native-reader (FR-002): the enumerated {value, description}
    // pairs of a codeset-backed field, keyed by `tag`. Empty span if `tag` has
    // no codeset. Since 075-live-wire-enum-validation, ALL ten dictionaries
    // (the nine XmlLoader/QuickFIX dictionaries plus OrchestraLoader) populate
    // the enum store. Additive, read-only; `table_view::enum_valid` (see
    // `as_table_view()` below) now READS this same store — no longer the
    // Phase-1 stub. Aliases the metadata-handle name pool (lifetime = this
    // Dictionary).
    [[nodiscard]] std::span<EnumValueRef const> enum_values(std::uint16_t tag) const noexcept
        [[clang::lifetimebound]];

    // 041-validation-gate-wiring T008: build a `dict::table_view` from this
    // Dictionary for use by `wire::dictionary_driven_validator`. The returned
    // `table_view` owns its tables; all validator method calls on it are
    // alloc-free (tables are built once here, not per-message). Most are
    // O(1); `enum_valid` (see 075 below) binary-searches its sorted per-tag
    // code set, O(log C).
    //
    // Builds: field-valid sets, required-field lists, group first/member tags,
    // global tag→`field_type` map (field_data_type collapsed via
    // `field_type_from_data_type()`), and — since
    // 075-live-wire-enum-validation — the enum-domain table `enum_valid()`
    // checks against (store-driven projection of `enum_values()` above; no
    // longer a stub).
    //
    // [const §XV.1]: construction only at config-time; the returned table_view
    // is immutable and must not be rebuilt on the per-message hot path.
    [[nodiscard]] table_view as_table_view() const;

private:
    friend class XmlLoader;
    friend class OrchestraLoader;  // 074: native Orchestra reader shares the private handle-ctor

    // Constructed from inside XmlLoader::load*; ctor body lives in the .cpp.
    Dictionary() noexcept = default;
    explicit Dictionary(detail::dict_metadata_handle_ptr handle) noexcept;

    detail::dict_metadata_handle_ptr handle_;
};

}  // namespace fixpp::dict
