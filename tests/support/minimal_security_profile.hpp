// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/minimal_security_profile.hpp
//
// Test-only helper: returns a minimal fixpp::session::SecurityProfile that
// satisfies Session::open()'s no-implicit-default sentinel rejection
// (FR-018 / N-P2-3 / [const §XII.5] / gate-b/r1 RC#1 / [arch §6 line 243]).
//
// Usage: set cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
// before calling Session::open() in any test that expects open() to succeed.
//
// The returned profile uses kind::mtls_ca — a non-sentinel value from the
// constitutional closed set ([const §XII.5]: mtls_ca / mtls_pinned / one_way_ca).
// Tests that assert open() REJECTS the default sentinel (kind::unset) must NOT
// use this helper; they should leave security_profile default-constructed.
//
// Do NOT use for TLS-semantic tests — only for threading/executor/clock/
// cancellation tests that need a valid security_profile precondition.
#pragma once

#include <fixpp/session/security_profile.hpp>

namespace fixpp::test_support {

[[nodiscard]] inline fixpp::session::SecurityProfile make_minimal_security_profile() {
    return fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
}

}  // namespace fixpp::test_support
