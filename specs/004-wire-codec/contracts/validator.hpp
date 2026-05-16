// SPDX-License-Identifier: AGPL-3.0-or-later
// CONTRACT SHAPE ORACLE — extract of [2b §4.6]. Authority: 2b-wire.md v0.2.
// EXACTLY 5 pure-virtual ([const §XIV.2] cap satisfied directly). Default impl
// dictionary_driven_validator holds dict::table_view BY VALUE — no virtual
// wire->dict runtime edge. validate() is UNCONDITIONAL over every
// dictionary-known field present (not per-accessor).
#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <memory_resource>
#include <fixpp/core/error.hpp>
#include "parser.hpp"

namespace fixpp::dict { class table_view; }

namespace fixpp::wire {

class Validator {
public:
    virtual ~Validator() noexcept = default;

    [[nodiscard]] virtual core::expected_t<void>
    validate(MessageView<access_mode::Index> const& msg,
             std::pmr::memory_resource* scratch_mr) const noexcept = 0;

    [[nodiscard]] virtual core::expected_t<void>
    validate_field(std::uint16_t tag,
                   std::span<const std::byte> value) const noexcept = 0;

    [[nodiscard]] virtual std::span<std::uint16_t const>
    required_fields(std::string_view msg_type) const noexcept = 0;

    [[nodiscard]] virtual bool
    field_valid_for(std::string_view msg_type,
                    std::uint16_t tag) const noexcept = 0;

    [[nodiscard]] virtual std::uint16_t
    group_first_field(std::uint16_t no_tag) const noexcept = 0;
};
static_assert(/* exactly 5 pure-virtual */ true, "[const §XIV.2] cap = 5");

class dictionary_driven_validator final : public Validator {
public:
    explicit dictionary_driven_validator(fixpp::dict::table_view dict) noexcept;
    // 5 overrides; per-version (v42/v44/v50sp2/vt11); scratch <= ~600 B.
};

}  // namespace fixpp::wire
