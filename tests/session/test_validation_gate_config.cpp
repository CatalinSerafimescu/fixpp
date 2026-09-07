// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_validation_gate_config.cpp
//
// 041-validation-gate-wiring T003/T004 — fail-closed config gate in
// Engine::register_session (FR-011 / contract C-5 / data-model E-1).
//
// C-5: register_session with validate_inbound_messages=true AND dictionary=nullptr
//      must return core::error::invalid_session_config (slot 53).
//      Distinct from session_invalid_argument (119) returned on a duplicate id.
//
// T003 RED: register_session still returns {} (no check) → assertion fails.
// T004 GREEN: the check is wired → assertion passes.
//
// Pattern mirrors engine_lifecycle_test.cpp DuplicateIdentityRejected: fresh
// Engine per test case, minimal config, stop() the engine after each test.
//
// Anchors: spec.md FR-011; contracts/validation-gate.md C-5;
//          data-model E-1; [2d §4.5]/[2d §6.1] invalid_session_config (slot 53).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session_config.hpp>

#include "support/pump_until_ready.hpp"

namespace {

using fixpp::core::error;
using fixpp::session::Engine;
using fixpp::session::session_role;
using fixpp::session::SessionConfig;

// Minimal SessionConfig for register_session that resolves to a valid SessionId.
// No dictionary set (nullptr) — used with validate_inbound_messages=true for C-5.
static SessionConfig make_cfg(asio::any_io_executor exec, bool validate_inbound, bool with_dict) {
    SessionConfig c;
    c.sender_comp_id = "SENDER";
    c.target_comp_id = "TARGET";
    c.begin_string = "FIX.4.4";
    c.role = session_role::initiator;
    c.executor_override = exec;
    c.validate_inbound_messages = validate_inbound;
    if (with_dict) {
        // A non-null dictionary pointer (minimal — only nullptr-ness matters for C-5).
        // Use make_minimal_dictionary from the test support header.
        // We forward-declare via the existing test support header.
        // Included below at point of use.
    }
    // dictionary is nullptr by default (std::shared_ptr default-constructs to null).
    return c;
}

// ── T003 (RED → T004 GREEN) ───────────────────────────────────────────────────

// C-5: validate_inbound_messages=true + dictionary=nullptr →
//   register_session must return invalid_session_config (slot 53).
TEST(ValidationGateConfig, ValidateTrue_NullDict_FailsClosed) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    Engine engine{ioc.get_executor(), std::move(eng)};

    SessionConfig cfg;
    cfg.sender_comp_id = "SENDER";
    cfg.target_comp_id = "TARGET";
    cfg.begin_string = "FIX.4.4";
    cfg.role = session_role::initiator;
    cfg.executor_override = ioc.get_executor();
    cfg.validate_inbound_messages = true;
    // cfg.dictionary is nullptr (default) — the failing condition.

    auto result = engine.register_session(std::move(cfg));

    // C-5: must be rejected with invalid_session_config.
    EXPECT_FALSE(result.has_value())
        << "C-5/FR-011: register_session with validate_inbound_messages=true and "
           "dictionary=nullptr must return an error";
    EXPECT_EQ(result.error(), error::invalid_session_config)
        << "C-5/FR-011: error must be invalid_session_config (slot 53), not: "
        << static_cast<int>(result.error());

    // Clean up: stop() the never-started engine.
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut, "ValidationGateConfig::ValidateTrue_NullDict_FailsClosed")) {
        return;
    }
    stop_fut.get();
}

// Negative control: validate_inbound_messages=false + dictionary=nullptr →
//   register_session must succeed (the gate is flag-conditioned).
TEST(ValidationGateConfig, ValidateFalse_NullDict_Succeeds) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    Engine engine{ioc.get_executor(), std::move(eng)};

    SessionConfig cfg;
    cfg.sender_comp_id = "SENDER2";
    cfg.target_comp_id = "TARGET2";
    cfg.begin_string = "FIX.4.4";
    cfg.role = session_role::initiator;
    cfg.executor_override = ioc.get_executor();
    cfg.validate_inbound_messages = false;  // default — gate must not fire
    // cfg.dictionary is nullptr.

    auto result = engine.register_session(std::move(cfg));

    EXPECT_TRUE(result.has_value())
        << "C-5: with validate_inbound_messages=false the null-dictionary must NOT "
           "cause register_session to fail (gate is flag-conditioned)";

    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut, "ValidationGateConfig::ValidateFalse_NullDict_Succeeds")) {
        return;
    }
    stop_fut.get();
}

}  // namespace
