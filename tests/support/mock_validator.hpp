#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/mock_validator.hpp
// Seam #1 — a value-substituted virtual fixpp::wire::Validator for US1/US4
// tests. Canned, deterministic answers so parser/cutover tests can exercise
// the validator seam without the full dictionary_driven_validator
// (src/wire/validator.cpp, US4/T046). Compiles once <fixpp/wire/validator.hpp>
// (and its parser.hpp include) land in US1/US4.

#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/validator.hpp>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace fixpp::wire::test {

// Programmable test Validator: every hook defaults to "accept / unknown" and
// can be overridden per test via the public std::function members.
class mock_validator final : public Validator {
public:
    std::function<core::expected_t<void>(MessageView<access_mode::Index> const&)> on_validate =
        [](auto const&) { return core::expected_t<void>{}; };
    std::function<core::expected_t<void>(std::uint16_t, std::span<const std::byte>)>
        on_validate_field =
            [](std::uint16_t, std::span<const std::byte>) { return core::expected_t<void>{}; };

    [[nodiscard]] core::expected_t<void> validate(
        MessageView<access_mode::Index> const& msg,
        std::pmr::memory_resource*) const noexcept override {
        return on_validate(msg);
    }
    [[nodiscard]] core::expected_t<void> validate_field(
        std::uint16_t tag, std::span<const std::byte> value) const noexcept override {
        return on_validate_field(tag, value);
    }
    [[nodiscard]] std::span<std::uint16_t const> required_fields(
        std::string_view) const noexcept override {
        return {};
    }
    [[nodiscard]] bool field_valid_for(std::string_view, std::uint16_t) const noexcept override {
        return true;
    }
    [[nodiscard]] std::uint16_t group_first_field(std::uint16_t) const noexcept override {
        return 0;
    }
};

}  // namespace fixpp::wire::test
