// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/logger_fwd.hpp
//
// Forward declaration of fixpp::log::Logger and the alias
// fixpp::core::Logger = fixpp::log::Logger.
//
// This header provides the type name resolution needed by EngineConfig::logger
// (engine_config.hpp:127) and any other header that holds a
// std::shared_ptr<fixpp::core::Logger> field WITHOUT needing the full Logger
// definition (which is pimpl'd and lives in include/fixpp/log/logger.hpp).
//
// [const §XV.9] COMPLIANCE: the OTel SDK, std::mutex, and std::shared_mutex
// are all kept OFF this include-edge. Logger::Impl is pimpl'd so the internal
// drain thread, MPSC ring, and sink vector are NOT visible here. Including this
// header in an awaitable-corpus header (e.g. session.hpp) does NOT drag a
// mutex into the co_await closure.
//
// Usage: include this header wherever a std::shared_ptr<fixpp::core::Logger>
// (or fixpp::log::Logger) is held by pointer/reference only. Include
// <fixpp/log/logger.hpp> in .cpp files or headers that need the full interface.
//
// Anchor: [2d §4.5] (engine-anchor/session-override pattern),
//         contracts/adjacent-amendments.md (T010 type-completion),
//         [const §XV.9] (awaitable include-edge).
#pragma once

namespace fixpp::log {
// Forward declaration only — full definition in include/fixpp/log/logger.hpp.
class Logger;
}  // namespace fixpp::log

namespace fixpp::core {
// Alias fixpp::core::Logger → fixpp::log::Logger.
// EngineConfig::logger is std::shared_ptr<fixpp::core::Logger>; this alias
// makes the two names interchangeable without a type-redefining include.
using Logger = fixpp::log::Logger;
}  // namespace fixpp::core
