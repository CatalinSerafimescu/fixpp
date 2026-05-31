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
// The returned profile uses kind::one_way_ca — server-auth-only (no client cert).
// This means the CompID authorization gate (FR-019/FR-024) is NOT triggered:
// one_way_ca has no client certificate so there is no peer_identity to authorize.
// Tests that test mTLS CompID binding must use kind::mtls_ca or kind::mtls_pinned
// explicitly, set a compid_authorization_policy, and inject the peer identity via
// inject_live_identity (support/identity_injecting_transport.hpp) after open().
// Tests that assert open() REJECTS the default sentinel (kind::unset) must NOT
// use this helper; they should leave security_profile default-constructed.
//
// RC#A (gate-b/r1): changed from mtls_ca → one_way_ca. The old mtls_ca value
// would trigger the fail-closed CompID gate for any session with no live peer
// identity attached. All non-CompID-auth tests keep the valid-profile
// precondition satisfied without hitting the authorization path.
//
// Do NOT use for TLS-semantic tests — only for threading/executor/clock/
// cancellation tests that need a valid security_profile precondition.
#pragma once

#include <fixpp/session/security_profile.hpp>

namespace fixpp::test_support {

[[nodiscard]] inline fixpp::session::SecurityProfile make_minimal_security_profile() {
    return fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::one_way_ca};
}

}  // namespace fixpp::test_support
