// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/dictionary.cpp
//
// `fixpp::dict::Dictionary` accessor bodies + `detail::dict_metadata_handle`
// lookup implementations. Every public accessor is `const` and `noexcept` per
// `[2c §4.3]` / AC-D8. The heap-pinned metadata-handle is allocated by
// `XmlLoader::load*` via `std::allocate_shared` over
// `std::pmr::polymorphic_allocator` so the control-block deallocator routes
// memory back to the originating `mr` (`[2c §4.3]` / C-R2-P1-1).

#include "dictionary_internal.hpp"

#include <fixpp/dict/dictionary.hpp>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>

namespace fixpp::dict {

// Bytewise (locale-independent) `unsigned char` comparator over `string_view`
// per research.md D-6 / Gate A round 1 P2.4.
namespace {

[[nodiscard]] inline int bytewise_compare(std::string_view a,
                                          std::string_view b) noexcept {
    auto const n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        auto const lhs = static_cast<unsigned char>(a[i]);
        auto const rhs = static_cast<unsigned char>(b[i]);
        if (lhs != rhs) {
            return lhs < rhs ? -1 : 1;
        }
    }
    if (a.size() == b.size()) {
        return 0;
    }
    return a.size() < b.size() ? -1 : 1;
}

}  // namespace

// ============================================================================
// detail::dict_metadata_handle — sorted-array lookup helpers
// ============================================================================

namespace detail {

MsgFieldsRun dict_metadata_handle::find_msg_fields(
    std::string_view msg_type) const noexcept {
    auto const it = std::ranges::lower_bound(
        messages_, msg_type, {},
        [](MessageEntry const& m) noexcept { return m.msg_type; });
    if (it == messages_.end() || it->msg_type != msg_type) {
        return {};
    }
    auto const idx = static_cast<std::size_t>(it - messages_.begin());
    return per_msg_field_offsets_[idx];
}

MsgFieldsRun dict_metadata_handle::find_msg_required(
    std::string_view msg_type) const noexcept {
    auto const it = std::ranges::lower_bound(
        messages_, msg_type, {},
        [](MessageEntry const& m) noexcept { return m.msg_type; });
    if (it == messages_.end() || it->msg_type != msg_type) {
        return {};
    }
    auto const idx = static_cast<std::size_t>(it - messages_.begin());
    return per_msg_required_offsets_[idx];
}

FieldRef dict_metadata_handle::field_ref_impl(std::string_view msg_type,
                                              std::uint16_t tag) const noexcept {
    auto const run = find_msg_fields(msg_type);
    if (run.count == 0) {
        return FieldRef{};
    }
    auto const begin = fields_.data() + run.start;
    auto const end = begin + run.count;
    auto const it = std::lower_bound(
        begin, end, tag,
        [](FieldRef const& a, std::uint16_t t) noexcept { return a.tag < t; });
    if (it == end || it->tag != tag) {
        return FieldRef{};
    }
    return *it;
}

std::span<std::uint16_t const> dict_metadata_handle::required_fields_impl(
    std::string_view msg_type) const noexcept {
    auto const run = find_msg_required(msg_type);
    if (run.count == 0) {
        return {};
    }
    return std::span<std::uint16_t const>{required_fields_pool_.data() + run.start,
                                          run.count};
}

std::uint16_t dict_metadata_handle::group_first_field_impl(
    std::uint16_t no_tag) const noexcept {
    auto const it = std::ranges::lower_bound(
        groups_, no_tag, {},
        [](GroupRef const& g) noexcept { return g.no_tag; });
    if (it == groups_.end() || it->no_tag != no_tag) {
        return 0;
    }
    return it->first_field_tag;
}

std::uint16_t dict_metadata_handle::length_pair_data_tag_impl(
    std::uint16_t length_tag) const noexcept {
    // Walk the global fields table (one row per (MsgType, tag); we only need
    // the first hit because length_pair_data_tag is a per-tag property not a
    // per-(MsgType,tag) property in v1.0).
    for (auto const& f : fields_) {
        if (f.tag == length_tag && f.length_pair_data_tag != 0) {
            return f.length_pair_data_tag;
        }
    }
    return 0;
}

std::optional<std::uint16_t> dict_metadata_handle::field_by_name_impl(
    std::string_view name) const noexcept {
    auto const it = std::lower_bound(
        field_by_name_.begin(), field_by_name_.end(), name,
        [this](FieldNameEntry const& e, std::string_view n) noexcept {
            return bytewise_compare(name_at(e.name), n) < 0;
        });
    if (it == field_by_name_.end() || name_at(it->name) != name) {
        return std::nullopt;
    }
    return it->tag;
}

std::optional<ComponentRef> dict_metadata_handle::component_impl(
    std::string_view name) const noexcept {
    auto const it = std::lower_bound(
        components_by_name_.begin(), components_by_name_.end(), name,
        [this](NamedIndex const& e, std::string_view n) noexcept {
            return bytewise_compare(name_at(e.name), n) < 0;
        });
    if (it == components_by_name_.end() || name_at(it->name) != name) {
        return std::nullopt;
    }
    return components_[it->index];
}

std::optional<GroupRef> dict_metadata_handle::group_impl(
    std::uint16_t no_tag) const noexcept {
    auto const it = std::ranges::lower_bound(
        groups_, no_tag, {},
        [](GroupRef const& g) noexcept { return g.no_tag; });
    if (it == groups_.end() || it->no_tag != no_tag) {
        return std::nullopt;
    }
    return *it;
}

}  // namespace detail

// ============================================================================
// Dictionary — public accessor bodies (every method const + noexcept)
// ============================================================================

Dictionary::Dictionary(detail::dict_metadata_handle_ptr handle) noexcept
    : handle_(std::move(handle)) {}

Dictionary::~Dictionary() = default;

session_version Dictionary::which_session_version() const noexcept {
    return handle_ ? handle_->version_impl() : session_version::Unknown;
}

FieldRef Dictionary::field_ref(std::string_view msg_type,
                               std::uint16_t tag) const noexcept {
    return handle_ ? handle_->field_ref_impl(msg_type, tag) : FieldRef{};
}

std::span<std::uint16_t const> Dictionary::required_fields(
    std::string_view msg_type) const noexcept {
    return handle_ ? handle_->required_fields_impl(msg_type)
                   : std::span<std::uint16_t const>{};
}

bool Dictionary::field_valid_for(std::string_view msg_type,
                                 std::uint16_t tag) const noexcept {
    return field_ref(msg_type, tag).rule != field_presence::NotDeclared;
}

std::uint16_t Dictionary::group_first_field(std::uint16_t no_tag) const noexcept {
    return handle_ ? handle_->group_first_field_impl(no_tag) : 0;
}

std::uint16_t Dictionary::length_pair_data_tag(std::uint16_t length_tag) const noexcept {
    return handle_ ? handle_->length_pair_data_tag_impl(length_tag) : 0;
}

std::optional<FieldRef> Dictionary::field(std::string_view msg_type,
                                          std::uint16_t tag) const noexcept {
    auto const fr = field_ref(msg_type, tag);
    if (fr.rule == field_presence::NotDeclared) {
        return std::nullopt;
    }
    return fr;
}

std::optional<std::uint16_t> Dictionary::field_by_name(
    std::string_view name) const noexcept {
    return handle_ ? handle_->field_by_name_impl(name) : std::nullopt;
}

std::optional<ComponentRef> Dictionary::component(
    std::string_view name) const noexcept {
    return handle_ ? handle_->component_impl(name) : std::nullopt;
}

std::optional<GroupRef> Dictionary::group(std::uint16_t no_tag) const noexcept {
    return handle_ ? handle_->group_impl(no_tag) : std::nullopt;
}

std::span<MessageEntry const> Dictionary::messages() const noexcept {
    return handle_ ? handle_->messages_impl() : std::span<MessageEntry const>{};
}

}  // namespace fixpp::dict
