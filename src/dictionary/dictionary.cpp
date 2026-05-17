// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/dictionary.cpp
//
// `fixpp::dict::Dictionary` accessor bodies + `detail::dict_metadata_handle`
// lookup implementations. Every public accessor is `const` and `noexcept` per
// `[2c §4.3]` / AC-D8. The heap-pinned metadata-handle is allocated by
// `XmlLoader::load*` via `std::allocate_shared` over
// `std::pmr::polymorphic_allocator` so the control-block deallocator routes
// memory back to the originating `mr` (`[2c §4.3]` / C-R2-P1-1).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "dictionary_internal.hpp"
#include "fixpp/dict/component_ref.hpp"
#include "fixpp/dict/field_ref.hpp"
#include "fixpp/dict/group_ref.hpp"
#include "fixpp/dict/version_profile.hpp"

namespace fixpp::dict {

// `detail::bytewise_compare` (the locale-independent name sort key shared with
// xml_loader.cpp) lives in dictionary_internal.hpp per research.md D-6.

// ============================================================================
// detail::dict_metadata_handle — sorted-array lookup helpers
// ============================================================================

namespace detail {

namespace {

// Shared message-row binary search for the two per-MsgType run lookups below.
// Returns SIZE_MAX when `msg_type` is absent (keeps the hot path allocation-
// and exception-free; the callers map the sentinel to an empty run).
[[nodiscard]] std::size_t find_msg_index(std::pmr::vector<MessageEntry> const& messages,
                                          std::string_view msg_type) noexcept {
    auto const it = std::ranges::lower_bound(
        messages, msg_type, {}, [](MessageEntry const& m) noexcept { return m.msg_type; });
    if (it == messages.end() || it->msg_type != msg_type) {
        return SIZE_MAX;
    }
    return static_cast<std::size_t>(it - messages.begin());
}

}  // namespace

MsgFieldsRun dict_metadata_handle::find_msg_fields(std::string_view msg_type) const noexcept {
    auto const idx = find_msg_index(messages_, msg_type);
    return idx == SIZE_MAX ? MsgFieldsRun{} : per_msg_field_offsets_[idx];
}

MsgFieldsRun dict_metadata_handle::find_msg_required(std::string_view msg_type) const noexcept {
    auto const idx = find_msg_index(messages_, msg_type);
    return idx == SIZE_MAX ? MsgFieldsRun{} : per_msg_required_offsets_[idx];
}

FieldRef dict_metadata_handle::field_ref_impl(std::string_view msg_type,
                                              std::uint16_t tag) const noexcept {
    auto const run = find_msg_fields(msg_type);
    if (run.count == 0) {
        return FieldRef{};
    }
    const auto* const begin = fields_.data() + run.start;
    const auto* const end = begin + run.count;
    const auto* const it = std::lower_bound(
        begin, end, tag, [](FieldRef const& a, std::uint16_t t) noexcept { return a.tag < t; });
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
    return std::span<std::uint16_t const>{required_fields_pool_.data() + run.start, run.count};
}

std::uint16_t dict_metadata_handle::group_first_field_impl(std::uint16_t no_tag) const noexcept {
    auto const it = std::ranges::lower_bound(groups_, no_tag, {},
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
        // cppcheck-suppress useStlAlgorithm  // raw loop chosen for early-exit + readability
        if (f.tag == length_tag && f.length_pair_data_tag != 0) {
            return f.length_pair_data_tag;
        }
    }
    return 0;
}

std::optional<std::uint16_t> dict_metadata_handle::field_by_name_impl(
    std::string_view name) const noexcept {
    // NOLINTNEXTLINE(modernize-use-ranges)
    auto const it = std::lower_bound(field_by_name_.begin(), field_by_name_.end(), name,
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
    // NOLINTNEXTLINE(modernize-use-ranges) — asymmetric comparator.
    auto const it = std::lower_bound(components_by_name_.begin(), components_by_name_.end(), name,
                                     [this](NamedIndex const& e, std::string_view n) noexcept {
                                         return bytewise_compare(name_at(e.name), n) < 0;
                                     });
    if (it == components_by_name_.end() || name_at(it->name) != name) {
        return std::nullopt;
    }
    return components_[it->index];
}

std::optional<GroupRef> dict_metadata_handle::group_impl(std::uint16_t no_tag) const noexcept {
    auto const it = std::ranges::lower_bound(groups_, no_tag, {},
                                             [](GroupRef const& g) noexcept { return g.no_tag; });
    if (it == groups_.end() || it->no_tag != no_tag) {
        return std::nullopt;
    }
    return *it;
}

std::span<FieldRef const> dict_metadata_handle::component_fields_impl(
    std::uint16_t component_id) const noexcept {
    if (component_id >= components_.size()) {
        return {};
    }
    auto const& cr = components_[component_id];
    if (cr.field_count == 0 || cr.first_field_index + cr.field_count > component_fields_.size()) {
        return {};
    }
    return std::span<FieldRef const>{component_fields_.data() + cr.first_field_index,
                                     cr.field_count};
}

std::span<FieldRef const> dict_metadata_handle::group_fields_impl(
    std::uint16_t no_tag) const noexcept {
    auto const it = std::ranges::lower_bound(groups_, no_tag, {},
                                             [](GroupRef const& g) noexcept { return g.no_tag; });
    if (it == groups_.end() || it->no_tag != no_tag) {
        return {};
    }
    auto const& gr = *it;
    if (gr.field_count == 0 || gr.first_field_index + gr.field_count > group_fields_.size()) {
        return {};
    }
    return std::span<FieldRef const>{group_fields_.data() + gr.first_field_index, gr.field_count};
}

// 003-dictionary-codegen (RC#5 — F1 IR data path). Build-time codegen-
// enumeration; not on any runtime hot path.
std::span<FieldRef const> dict_metadata_handle::message_fields_impl(
    std::string_view msg_type) const noexcept {
    MsgFieldsRun const run = find_msg_fields(msg_type);
    if (run.count == 0 || run.start + run.count > fields_.size()) {
        return {};
    }
    return std::span<FieldRef const>{fields_.data() + run.start, run.count};
}

std::string_view dict_metadata_handle::field_name_impl(std::uint16_t tag) const noexcept {
    // field_by_name_ is sorted by name (not tag); a linear scan is fine —
    // this runs once per tag at codegen time, never on a runtime path.
    for (auto const& e : field_by_name_) {
        if (e.tag == tag) {
            return name_at(e.name);
        }
    }
    return {};
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

FieldRef Dictionary::field_ref(std::string_view msg_type, std::uint16_t tag) const noexcept {
    return handle_ ? handle_->field_ref_impl(msg_type, tag) : FieldRef{};
}

std::span<std::uint16_t const> Dictionary::required_fields(
    std::string_view msg_type) const noexcept {
    return handle_ ? handle_->required_fields_impl(msg_type) : std::span<std::uint16_t const>{};
}

bool Dictionary::field_valid_for(std::string_view msg_type, std::uint16_t tag) const noexcept {
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

std::optional<std::uint16_t> Dictionary::field_by_name(std::string_view name) const noexcept {
    return handle_ ? handle_->field_by_name_impl(name) : std::nullopt;
}

std::optional<ComponentRef> Dictionary::component(std::string_view name) const noexcept {
    return handle_ ? handle_->component_impl(name) : std::nullopt;
}

std::optional<GroupRef> Dictionary::group(std::uint16_t no_tag) const noexcept {
    return handle_ ? handle_->group_impl(no_tag) : std::nullopt;
}

std::span<MessageEntry const> Dictionary::messages() const noexcept {
    return handle_ ? handle_->messages_impl() : std::span<MessageEntry const>{};
}

std::span<FieldRef const> Dictionary::component_fields(std::string_view name) const noexcept {
    if (!handle_) {
        return {};
    }
    auto const cr = handle_->component_impl(name);
    if (!cr) {
        return {};
    }
    return handle_->component_fields_impl(cr->component_id);
}

std::span<FieldRef const> Dictionary::group_fields(std::uint16_t no_tag) const noexcept {
    return handle_ ? handle_->group_fields_impl(no_tag) : std::span<FieldRef const>{};
}

// 003-dictionary-codegen (RC#5 — F1 IR data path; additive, source-compatible
// read accessors — [arch §9.3] stable-from-v1.0, [const §X.4]-style).
std::span<FieldRef const> Dictionary::message_fields(std::string_view msg_type) const noexcept {
    return handle_ ? handle_->message_fields_impl(msg_type) : std::span<FieldRef const>{};
}

std::string_view Dictionary::field_name(std::uint16_t tag) const noexcept {
    return handle_ ? handle_->field_name_impl(tag) : std::string_view{};
}

}  // namespace fixpp::dict
