// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/table_view.hpp
//
// `fixpp::dict::table_view` — production value type that `wire::Validator`
// and `wire::dictionary_driven_validator` bind BY VALUE to drive dictionary-
// backed inbound validation.
//
// This type is the Phase-1 realisation of the RC-A blocker identified in
// `specs/041-validation-gate-wiring/research.md §R-1`.
//
// CONTRACT (C-1, 041-validation-gate-wiring/contracts/validation-gate.md):
// The 6-method surface the validator calls:
//
//   bool         field_valid_for(string_view msg_type, uint16_t tag)    const noexcept
//   span<uint16_t const> required_fields(string_view msg_type)          const noexcept
//   uint16_t     group_first_field(uint16_t no_tag)                     const noexcept
//   span<uint16_t const> group_member_tags(uint16_t no_tag)             const noexcept
//   field_type   field_type_of(uint16_t tag)                            const noexcept
//   bool         enum_valid(uint16_t tag, span<const byte> value)       const noexcept
//       → Phase-1: always true (FR-005; enum tables deferred to 2c work)
//
// STORAGE (E-2, data-model.md):
// Owns its tables using std::vector / std::unordered_map. Constructed ONCE at
// session/validator setup time by `Dictionary::as_table_view()` ([const §XV.1]
// — config-time, not per-message). Immutable after construction.
//
// INCLUDE-GRAPH CONSTRAINT ([const §XV.9]):
// This header is included (transitively) by `validator.hpp`, which lands on
// the `on_inbound_frame` co_await path. MUST NOT pull std::mutex,
// std::shared_mutex, or heavy asio into the closure. The dependency direction
// is:  dictionary.hpp → table_view.hpp  (never the reverse).
//
// Minimal includes only — deliberate:

#pragma once

#include <cstddef>
#include <cstdint>
#include <fixpp/dict/field_type.hpp>  // field_type (7-value enum)
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fixpp::dict {

// Transparent hash for std::unordered_map<std::string, ...> enabling
// heterogeneous find(std::string_view) without constructing a std::string
// temporary. Both overloads hash via std::hash<std::string_view> so
// stored-string-key hashes remain consistent with string_view lookups.
// Combined with std::equal_to<> (transparent equality), this makes
// .find(string_view) fully allocation-free on the lookup path.
//
// [const §VIII.5 / §XV.1]: the two string-keyed maps (valid_, required_)
// are on the validate-ON hot path (called once per field per inbound message).
// Without this, a MsgType longer than SSO (~15 chars on libstdc++, ~22 on
// libc++) heap-allocates per message AND std::terminate()s on bad_alloc
// inside the noexcept methods.
struct string_hash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
    [[nodiscard]] std::size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

// Production value type exposing exactly the 6-method surface bound by
// `wire::dictionary_driven_validator`. Owns its backing storage; spans
// returned by the methods remain valid for the lifetime of this object.
//
// move-only (large owned tables; copying is intentionally deleted).
class table_view {
public:
    table_view() = default;
    ~table_view() = default;

    // Copy and move — both allowed. Copies duplicate the owned tables (used
    // when a single table_view configuration seeds multiple validator instances,
    // and by the mock-compatibility static_assert in validator_domain_test.cpp).
    // Move is noexcept; copy may throw on allocation failure.
    table_view(table_view const&) = default;
    table_view& operator=(table_view const&) = default;
    table_view(table_view&&) noexcept = default;
    table_view& operator=(table_view&&) noexcept = default;

    // ── 6-method validator surface (C-1) ────────────────────────────────────

    // True iff `tag` is declared for `msg_type` in the source Dictionary.
    [[nodiscard]] bool field_valid_for(std::string_view msg_type,
                                       std::uint16_t tag) const noexcept {
        auto it = valid_.find(msg_type);
        return it != valid_.end() && it->second.count(tag) > 0;
    }

    // Tags that are required for `msg_type`. Empty span if unknown.
    // The span aliases storage owned by this table_view (stable for lifetime).
    [[nodiscard]] std::span<std::uint16_t const> required_fields(
        std::string_view msg_type) const noexcept {
        auto it = required_.find(msg_type);
        if (it == required_.end()) {
            return {};
        }
        return {it->second.data(), it->second.size()};
    }

    // First-delimiter tag for group `no_tag`. Returns 0 if not a group.
    [[nodiscard]] std::uint16_t group_first_field(std::uint16_t no_tag) const noexcept {
        auto it = group_first_.find(no_tag);
        return it == group_first_.end() ? std::uint16_t{0} : it->second;
    }

    // All member tags for group `no_tag`. Empty span if not a group.
    // The span aliases storage owned by this table_view (stable for lifetime).
    [[nodiscard]] std::span<std::uint16_t const> group_member_tags(
        std::uint16_t no_tag) const noexcept {
        auto it = group_members_.find(no_tag);
        if (it == group_members_.end()) {
            return {};
        }
        return {it->second.data(), it->second.size()};
    }

    // 7-value structural type category for `tag`. Defaults to String for
    // unknown tags (safe: the String arm imposes no structural constraint).
    [[nodiscard]] field_type field_type_of(std::uint16_t tag) const noexcept {
        auto it = types_.find(tag);
        return it == types_.end() ? field_type::String : it->second;
    }

    // Phase-1: always true. Enum-value checking is deferred to the 2c work
    // (FR-005). A field whose value is a wrong enum constant but a correct
    // structural type is accepted.
    [[nodiscard]] bool enum_valid(std::uint16_t /*tag*/,
                                  std::span<const std::byte> /*value*/) const noexcept {
        return true;
    }

    // ── Build-time population surface ────────────────────────────────────────
    // Used by Dictionary::as_table_view() (non-chain void) and by test code
    // via the chain-style API below. Not part of the validator-facing contract.
    //
    // The chain-style methods mirror the test mock's builder surface so that
    // existing tests/wire/validator_*_test.cpp TUs can drop the mock include
    // and use this production type directly (RC-A closure, T009).

    void add_valid_tag(std::string_view msg_type, std::uint16_t tag) {
        valid_[std::string{msg_type}].insert(tag);
    }

    void add_required_tag(std::string_view msg_type, std::uint16_t tag) {
        required_[std::string{msg_type}].push_back(tag);
        valid_[std::string{msg_type}].insert(tag);
    }

    void set_field_type(std::uint16_t tag, field_type ft) { types_[tag] = ft; }

    // add_group_member returns *this for chaining (mirrors mock surface).
    table_view& add_group_member(std::uint16_t no_tag, std::uint16_t member_tag) {
        auto& members = group_members_[no_tag];
        for (auto const t : members) {
            if (t == member_tag) return *this;  // dedup
        }
        members.push_back(member_tag);
        return *this;
    }

    // Chain-style helpers — all return *this for fluent use.

    table_view& add_valid(std::string_view msg_type, std::uint16_t tag) {
        add_valid_tag(msg_type, tag);
        return *this;
    }

    table_view& add_required(std::string_view msg_type, std::uint16_t tag) {
        add_required_tag(msg_type, tag);
        return *this;
    }

    table_view& set_type(std::uint16_t tag, field_type ft) {
        set_field_type(tag, ft);
        return *this;
    }

    // set_group_first: sets the first-delimiter tag AND adds it as a member
    // (mirrors the mock's set_group_first behaviour exactly).
    table_view& set_group_first(std::uint16_t no_tag, std::uint16_t first) {
        group_first_[no_tag] = first;
        add_group_member(no_tag, first);
        return *this;
    }

    // Phase-1 enum stub: always returns true from enum_valid(); no storage
    // (FR-005 — enum tables deferred to 2c work).
    table_view& add_enum(std::uint16_t /*tag*/, std::string_view /*value*/) { return *this; }

private:
    // Valid-tag set per msg_type (used by field_valid_for).
    // transparent hash+equality: find(string_view) is allocation-free
    // [const §VIII.5 / §XV.1 — on the validate-ON hot path].
    std::unordered_map<std::string, std::unordered_set<std::uint16_t>,
                       string_hash, std::equal_to<>> valid_;

    // Required-tag list per msg_type (insertion order preserved; spans stable).
    // transparent hash+equality: find(string_view) is allocation-free
    // [const §VIII.5 / §XV.1 — on the validate-ON hot path].
    std::unordered_map<std::string, std::vector<std::uint16_t>,
                       string_hash, std::equal_to<>> required_;

    // Group first-delimiter (no_tag → first member tag).
    std::unordered_map<std::uint16_t, std::uint16_t> group_first_;

    // Group member-tag lists (no_tag → member tags; spans stable).
    std::unordered_map<std::uint16_t, std::vector<std::uint16_t>> group_members_;

    // Global tag → field_type map (built once from Dictionary).
    std::unordered_map<std::uint16_t, field_type> types_;
};

}  // namespace fixpp::dict
