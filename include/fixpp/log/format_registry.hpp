// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/log/format_registry.hpp
//
// Drain-side format-string registry (T024 companion header).
// Declares the lookup + format functions used by the drain thread in Logger::Impl.
//
// This header is NOT included on the producer path (logger.hpp does not
// include it). It is private to src/log/logger.cpp and src/log/format_registry.cpp.
//
// [2k §4.3] / contracts/log-core.md R3.
#pragma once

#include <cstdint>
#include <fixpp/log/record.hpp>
#include <string>
#include <string_view>

namespace fixpp::log::detail {

// Look up a format string by CRC32 format_id.
// Returns "<unknown format_id>" if not found.
// Called on the drain thread only (not on the producer hot path).
std::string_view format_registry_lookup(std::uint32_t format_id) noexcept;

// Format a Record into a string using the registry lookup + ArgValue marshalling.
// Called by the drain thread for each consumed record.
std::string format_record(Record const& rec);

}  // namespace fixpp::log::detail
