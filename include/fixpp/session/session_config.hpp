// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/session_config.hpp
//
// fixpp::session::SessionConfig — value-typed, FROZEN at Session::open
// ([arch §5.6] — close-and-reopen only; I-13). The executor / clock /
// dictionary axes follow ONE uniform pattern:
//     resolved = override.value_or(engine_anchor)
// Namespace-scoped threading_mode / lock_policy enums + the CLOSED, 2-value
// nested SessionConfig::backpressure_mode (drop_oldest UNREPRESENTABLE on the
// app/session message path — [const §XV.15] / [2d §6.4] / I-14). The frozen
// field implementers switch on is SessionConfig::app_backpressure.
// [2d §4.5]. Realizes specs/007-threading-clock/contracts/session_config.hpp.
//
// NO close_timeout field (D-9): the close-timeout VALUE lives in the
// session-module Phase-4 spec (005), not 2d's frozen config shape; 2d wires
// only the timeout mechanism ([2d §4.7]:864 / [2d §6.7]:1207).
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>

#include <asio/any_io_executor.hpp>

#include <fixpp/core/trace_context.hpp>
#include <fixpp/session/message_store_factory.hpp>  // unique_ptr member ⇒ complete type

namespace fixpp::core { class Clock; }
namespace fixpp::dict { class Dictionary; class DialectOverlay; }
namespace fixpp::tls  { class cert_source; struct SecurityProfile; }
namespace fixpp::log  { class Sink; }
namespace fixpp::tap  { class TapConsumer; }

namespace fixpp::session {

enum class RejectPolicy : std::uint8_t;  // owned by 005; declared for the field

enum class threading_mode : std::uint8_t {
    per_session_strand = 0,   // default — make_strand wrap; callbacks serialised
    direct_executor    = 1,   // expert opt-out — needs already_serialized_executor
};

enum class lock_policy : std::uint8_t {
    mutex = 0,                // default
    spin  = 1,                // opt-in; store-write path always mutex ([const §XI.5])
};

// Portable "closed enum" attribute (no-op where unsupported). Placed after
// the enum name per the Clang spelling; a static_assert at every switch site
// (T048) enumerates exactly the 2 values and a runtime out-of-range cast is
// rejected with error::invalid_session_config (seam 13).
#if defined(__clang__) && defined(__has_attribute)
#  if __has_attribute(enum_extensibility)
#    define FIXPP_ENUM_CLOSED __attribute__((enum_extensibility(closed)))
#  endif
#endif
#ifndef FIXPP_ENUM_CLOSED
#  define FIXPP_ENUM_CLOSED
#endif

// Compile-time exhaustiveness guard for switch sites over backpressure_mode.
// Place FIXPP_ASSERT_BACKPRESSURE_SWITCH_EXHAUSTIVE(T) immediately before any
// switch(backpressure_mode), enumeration BLOCK_VAL and DISCONNECT_VAL below.
// The static_assert fires if the underlying integer range of T ever grows
// beyond the 2 legal values (block=0, disconnect_and_recover=1). Dropping
// drop_oldest from the enum is intentional per [const §XV.15] / [2d §6.4] /
// I-14; this macro is the compile-time enforcement companion.
//
// Usage pattern (at every switch site):
//   FIXPP_ASSERT_BACKPRESSURE_SWITCH_EXHAUSTIVE(SessionConfig::backpressure_mode);
//   switch (cfg.app_backpressure) {
//       case SessionConfig::backpressure_mode::block:              ...
//       case SessionConfig::backpressure_mode::disconnect_and_recover: ...
//   }
#define FIXPP_ASSERT_BACKPRESSURE_SWITCH_EXHAUSTIVE(T)                         \
    static_assert(                                                              \
        static_cast<std::uint8_t>(T::block) == 0 &&                            \
        static_cast<std::uint8_t>(T::disconnect_and_recover) == 1,             \
        "backpressure_mode must be closed: exactly {block=0, "                  \
        "disconnect_and_recover=1}. drop_oldest is BANNED on the app/session " \
        "path ([const §XV.15] / [2d §6.4] / I-14). Extend this list if "      \
        "the enum changes and update ALL switch sites.")

// Value-typed; FROZEN at Session::open ([arch §5.6] — close-and-reopen only).
struct SessionConfig {
    enum class FIXPP_ENUM_CLOSED backpressure_mode : std::uint8_t {
        block                  = 0,   // push back to producer (default)
        disconnect_and_recover = 1,   // terminate session; ResendRequest on reconnect
    };

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
    fixpp::tls::SecurityProfile*                   security_profile = nullptr;  // N-P2-3 sentinel — enforcement deferred to 2g (D-21; src/session/session.cpp WIRING POINT FOR 2g)

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
    fixpp::tap::TapConsumer*          tap_consumer = nullptr;   // default = no tap

    backpressure_mode app_backpressure = backpressure_mode::block;
};

}  // namespace fixpp::session
