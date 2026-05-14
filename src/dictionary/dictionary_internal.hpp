// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/dictionary_internal.hpp
//
// Private to `src/dictionary/`. Defines `detail::dict_metadata_handle` — the
// heap-pinned metadata block holding every PMR-allocated table the public
// `Dictionary` accessors look into. Allocated by `XmlLoader::load*` via
// `std::allocate_shared` over `std::pmr::polymorphic_allocator` so the
// control-block deallocator returns memory to the originating `mr` per
// `[2c §4.3]` C-R2-P1-1.
//
// Not exposed from `include/fixpp/dict/`; included only by
// `src/dictionary/dictionary.cpp` and `src/dictionary/xml_loader.cpp`.

#pragma once

#include <fixpp/dict/component_ref.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/group_ref.hpp>
#include <fixpp/dict/version_profile.hpp>

#include <cstdint>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fixpp::dict::detail {

// (offset, length) into the metadata-handle's name string pool. Indices are
// stable for the handle's lifetime because the pool is reserved to its final
// size before any string_view is bound (see xml_loader.cpp build sequence).
struct NameSlice {
    std::uint32_t offset{0};
    std::uint32_t length{0};
};

// Per-MsgType run inside the flat `fields_` array.
struct MsgFieldsRun {
    std::uint32_t start{0};
    std::uint32_t count{0};
};

// Lookup helper: a name (slice into the pool) bound to a payload index.
struct NamedIndex {
    NameSlice name{};
    std::uint32_t index{0};
};

// Field-name → tag entry (sorted by name bytewise).
struct FieldNameEntry {
    NameSlice name{};
    std::uint16_t tag{0};
};

class dict_metadata_handle {
public:
    explicit dict_metadata_handle(std::pmr::memory_resource* mr) noexcept
        : mr_(mr),
          name_pool_(mr),
          fields_(mr),
          per_msg_field_offsets_(mr),
          required_fields_pool_(mr),
          per_msg_required_offsets_(mr),
          components_(mr),
          components_by_name_(mr),
          groups_(mr),
          messages_(mr),
          field_by_name_(mr) {}

    // Resolve a NameSlice against the (now-stable) name_pool_ data pointer.
    [[nodiscard]] std::string_view name_at(NameSlice ns) const noexcept {
        if (ns.length == 0) {
            return std::string_view{};
        }
        return std::string_view{name_pool_.data() + ns.offset, ns.length};
    }

    // Per-MsgType field-array lookup. Returns the run; `count == 0` if the
    // MsgType is not in the dictionary.
    [[nodiscard]] MsgFieldsRun find_msg_fields(std::string_view msg_type) const noexcept;

    // Per-MsgType required-fields run.
    [[nodiscard]] MsgFieldsRun find_msg_required(std::string_view msg_type) const noexcept;

    [[nodiscard]] std::pmr::memory_resource* mr() const noexcept { return mr_; }

    // ---- Public-API-shaped accessors used by Dictionary methods ----

    [[nodiscard]] FieldRef field_ref_impl(std::string_view msg_type,
                                          std::uint16_t tag) const noexcept;

    [[nodiscard]] std::span<std::uint16_t const>
    required_fields_impl(std::string_view msg_type) const noexcept;

    [[nodiscard]] std::uint16_t group_first_field_impl(std::uint16_t no_tag) const noexcept;

    [[nodiscard]] std::uint16_t
    length_pair_data_tag_impl(std::uint16_t length_tag) const noexcept;

    [[nodiscard]] std::optional<std::uint16_t>
    field_by_name_impl(std::string_view name) const noexcept;

    [[nodiscard]] std::optional<ComponentRef>
    component_impl(std::string_view name) const noexcept;

    [[nodiscard]] std::optional<GroupRef> group_impl(std::uint16_t no_tag) const noexcept;

    [[nodiscard]] std::span<MessageEntry const> messages_impl() const noexcept {
        return std::span<MessageEntry const>{messages_};
    }

    [[nodiscard]] session_version version_impl() const noexcept { return version_; }

    // ---- Build-time state (populated by XmlLoader) ----

    std::pmr::memory_resource* mr_;

    // Name string pool. Appended-to during build. Reserved to its final
    // capacity before any `std::string_view` is bound into it (xml_loader.cpp
    // builds an exact byte budget in a first pass over the DOM).
    std::pmr::vector<char> name_pool_;

    // Per-message field runs concatenated; per-msg run pointed to by
    // `per_msg_field_offsets_[msg_index]`.
    std::pmr::vector<FieldRef> fields_;

    // Parallel to `messages_`. `per_msg_field_offsets_[i]` is the run for the
    // i-th message in `messages_`.
    std::pmr::vector<MsgFieldsRun> per_msg_field_offsets_;

    // Per-message required-fields tag list (flat, with runs).
    std::pmr::vector<std::uint16_t> required_fields_pool_;
    std::pmr::vector<MsgFieldsRun> per_msg_required_offsets_;

    // Components (sorted by component_id == declaration order).
    std::pmr::vector<ComponentRef> components_;
    // Component-name → index lookup (sorted bytewise by name).
    std::pmr::vector<NamedIndex> components_by_name_;

    // Groups (sorted by `no_tag` ascending).
    std::pmr::vector<GroupRef> groups_;

    // Messages (sorted bytewise by `msg_type`).
    std::pmr::vector<MessageEntry> messages_;

    // Field-name → tag (sorted by name bytewise).
    std::pmr::vector<FieldNameEntry> field_by_name_;

    session_version version_{session_version::Unknown};
};

}  // namespace fixpp::dict::detail
