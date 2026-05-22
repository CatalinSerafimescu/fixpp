// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/support/frame_field_extract.hpp
//
// Extract the value of a FIX tag from a committed outbound wire frame.
// Returns std::nullopt if the tag is not present in the frame. Test-only;
// not optimized for production use. Authored for 009-session-fsm-finalize
// (FR-013 outbound tag-8/52 assertions + US3 Reject / US4 TestReqID tests).
//
// FIX wire grammar: each field is <tag>=<value><SOH> where SOH = 0x01. The
// returned string_view points into the supplied frame; lifetime is the
// caller's responsibility.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace fixpp::session::test_support {

[[nodiscard]] std::optional<std::string_view> extract_field(std::span<const std::byte> frame,
                                                            std::uint16_t tag) noexcept;

}  // namespace fixpp::session::test_support
