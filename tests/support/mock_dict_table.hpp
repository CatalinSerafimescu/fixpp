#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/mock_dict_table.hpp
// Seam #1 — a concrete, test-owned definition of fixpp::dict::table_view.
//
// The real value-typed metadata contract is owned by 2c (not yet merged);
// wire/ headers only FORWARD-declare `namespace fixpp::dict { class
// table_view; }`. Wire's Parser/Validator are header-mostly and only
// *instantiated* where a complete table_view exists — in tests, that is this
// double. When 2c lands, the real table_view replaces this header at the
// test call sites with no wire surface change (it is held by value, the
// member set below is the contract Validator/Parser bind: required_fields /
// field_valid_for / group_first_field / field_type / enum membership).
//
// Single-definition rule: ONLY tests include this. Production wire code never
// sees a table_view definition (it never instantiates without one).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fixpp::dict {

enum class field_type : std::uint8_t {
    String, Int, Float, Char, Boolean, Data, Length
};

class table_view {
public:
    table_view() = default;

    // ---- builder surface (test-only; the real 2c type is loaded from XML)
    table_view& add_required(std::string_view msg_type, std::uint16_t tag) {
        auto& v = required_[std::string{msg_type}];
        v.push_back(tag);
        valid_[std::string{msg_type}].insert(tag);
        return *this;
    }
    table_view& add_valid(std::string_view msg_type, std::uint16_t tag) {
        valid_[std::string{msg_type}].insert(tag);
        return *this;
    }
    table_view& set_type(std::uint16_t tag, field_type t) {
        types_[tag] = t;
        return *this;
    }
    table_view& set_group_first(std::uint16_t no_tag, std::uint16_t first) {
        group_first_[no_tag] = first;
        return *this;
    }
    // Register one allowed enumerated value for `tag`. A tag with NO
    // registered value set is treated as a non-enumerated (free) field —
    // exactly FIX semantics: only fields carrying `<value>` children are
    // enum-constrained. (The real 2c table_view sources this from the
    // dictionary `<value enum=...>` set; recorded as the explicit 2c/002
    // dependency in specs/004-wire-codec/tasks.md Phase 6.)
    table_view& add_enum(std::uint16_t tag, std::string_view value) {
        enums_[tag].insert(std::string{value});
        return *this;
    }

    // ---- value-contract surface bound by wire::Validator/Parser
    [[nodiscard]] std::span<std::uint16_t const>
    required_fields(std::string_view msg_type) const noexcept {
        auto it = required_.find(std::string{msg_type});
        if (it == required_.end()) {
            return {};
        }
        return {it->second.data(), it->second.size()};
    }

    [[nodiscard]] bool field_valid_for(std::string_view msg_type,
                                        std::uint16_t tag) const noexcept {
        auto it = valid_.find(std::string{msg_type});
        return it != valid_.end() && it->second.contains(tag);
    }

    [[nodiscard]] std::uint16_t
    group_first_field(std::uint16_t no_tag) const noexcept {
        auto it = group_first_.find(no_tag);
        return it == group_first_.end() ? std::uint16_t{0} : it->second;
    }

    [[nodiscard]] field_type
    field_type_of(std::uint16_t tag) const noexcept {
        auto it = types_.find(tag);
        return it == types_.end() ? field_type::String : it->second;
    }

    // Runtime enum-membership check bound by wire::Validator's §6.5 enum
    // rule. Returns true if `tag` is not an enumerated field (no registered
    // value set) OR `value` matches one of its registered values byte-for-
    // byte. A registered-but-unmatched value is the only rejection
    // (→ wire_field_value_out_of_range, slot 40).
    [[nodiscard]] bool
    enum_valid(std::uint16_t tag,
               std::span<const std::byte> value) const noexcept {
        auto it = enums_.find(tag);
        if (it == enums_.end()) {
            return true;  // not an enumerated field — unconstrained
        }
        std::string v;
        v.reserve(value.size());
        for (auto b : value) {
            v.push_back(static_cast<char>(b));
        }
        return it->second.contains(v);
    }

private:
    std::unordered_map<std::string, std::vector<std::uint16_t>> required_;
    std::unordered_map<std::string, std::unordered_set<std::uint16_t>> valid_;
    std::unordered_map<std::uint16_t, std::uint16_t> group_first_;
    std::unordered_map<std::uint16_t, field_type> types_;
    std::unordered_map<std::uint16_t, std::unordered_set<std::string>> enums_;
};

}  // namespace fixpp::dict
