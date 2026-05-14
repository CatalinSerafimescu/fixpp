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
// OUT of scope this PR: `with_overlay`, `as_table_view`,
// `resolve_application_version`, `was_dialect_promoted` — all deferred.

#pragma once

#include <cstdint>
#include <fixpp/dict/component_ref.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/group_ref.hpp>
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
    std::string_view msg_type;  // FIX MsgType string (e.g., "D" for
                                // NewOrderSingle).
    std::string_view name;      // English message name. Diagnostics-only.
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

private:
    friend class XmlLoader;

    // Constructed from inside XmlLoader::load*; ctor body lives in the .cpp.
    Dictionary() noexcept = default;
    explicit Dictionary(detail::dict_metadata_handle_ptr handle) noexcept;

    detail::dict_metadata_handle_ptr handle_;
};

}  // namespace fixpp::dict
