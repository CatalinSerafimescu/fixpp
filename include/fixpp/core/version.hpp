#pragma once
// include/fixpp/core/version.hpp
// Version constants for the fixpp library.
// [arch §9.2] — dual SemVer tracks: library and C ABI.

namespace fixpp::core {

/// Library version string — incremented per SemVer on C++ surface breaks.
constexpr inline auto FIXPP_VERSION = "0.0.1";

}  // namespace fixpp::core
