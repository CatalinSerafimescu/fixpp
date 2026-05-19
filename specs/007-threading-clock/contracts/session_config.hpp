// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. [2d §4.5] SessionConfig + enums.
#pragma once
#include <asio/any_io_executor.hpp>
#include <cstdint>
#include <chrono>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>

namespace fixpp::core { class Clock; }
namespace fixpp::dict { class Dictionary; class DialectOverlay; }
namespace fixpp::tls  { class cert_source; struct SecurityProfile; }
namespace fixpp::log  { class Sink; }
namespace fixpp::tap  { class TapConsumer; }

namespace fixpp::session {

class MessageStoreFactory;
enum class RejectPolicy : std::uint8_t;  // owned by 005; declared for the field

enum class threading_mode : std::uint8_t {
    per_session_strand = 0,   // default — make_strand wrap; callbacks serialised
    direct_executor    = 1,   // expert opt-out — needs already_serialized_executor (root cause #1)
};

enum class lock_policy : std::uint8_t {
    mutex = 0,                // default
    spin  = 1,                // opt-in; store-write path always mutex ([const §XI.5])
};

// CLOSED, exactly 2 values — drop_oldest is UNREPRESENTABLE on the
// app/session message path ([const §XV.15] / [2d §6.4]). The build header
// applies [[clang::enum_extensibility(closed)]] (correctly placed after the
// enum name, where supported); a static_assert at every switch enumerates
// exactly these two; a runtime out-of-range cast is rejected with
// error::invalid_session_config (seam 13). Attribute syntax/placement is an
// /implement detail and is intentionally NOT pinned by this oracle.
enum class backpressure_mode : std::uint8_t {
    block                  = 0,   // push back to producer (default)
    disconnect_and_recover = 1,   // terminate session; FIX ResendRequest on reconnect
};

// Value-typed; FROZEN at Session::open ([arch §5.6] — close-and-reopen only).
// executor/clock/dictionary axes: resolved = override.value_or(engine_anchor).
struct SessionConfig {
    std::optional<asio::any_io_executor> executor_override;
    threading_mode mode  = threading_mode::per_session_strand;
    lock_policy    locks = lock_policy::mutex;
    bool           already_serialized_executor = false;          // MUST be true when mode==direct_executor
    std::shared_ptr<fixpp::core::Clock> clock_override;          // null → EngineConfig::clock

    std::string sender_comp_id;     // identity owned by 005
    std::string target_comp_id;
    std::string begin_string;

    std::unique_ptr<MessageStoreFactory>           store_factory;   // unique ownership
    std::shared_ptr<fixpp::tls::cert_source>       cert_source;
    fixpp::tls::SecurityProfile                    security_profile; // no-implicit-default sentinel (N-P2-3)

    std::shared_ptr<const fixpp::dict::Dictionary>     dictionary;       // required
    std::shared_ptr<const fixpp::dict::DialectOverlay> dialect_overlay;  // optional

    std::optional<std::chrono::seconds>      heartbeat_interval;       // value owned by 005
    std::optional<std::chrono::milliseconds> test_request_threshold;   // value owned by 005
    std::optional<std::chrono::milliseconds> sending_time_threshold;   // value owned by 005
    RejectPolicy reject_policy{};                                      // owned by 005

    std::pmr::memory_resource* message_arena      = nullptr;  // null → engine default
    std::pmr::memory_resource* framer_carry_arena = nullptr;  // owned by 2b; recorded here
    std::pmr::memory_resource* session_arena      = nullptr;

    fixpp::otel::trace_context        initial_trace_context{}; // value-typed (C-P2-4)
    std::shared_ptr<fixpp::log::Sink> log_sink_override;       // null → engine default
    fixpp::tap::TapConsumer           tap_consumer;            // default = no tap

    backpressure_mode app_backpressure = backpressure_mode::block;
};

}  // namespace fixpp::session
