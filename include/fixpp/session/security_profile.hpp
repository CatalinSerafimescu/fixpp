// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/security_profile.hpp
//
// fixpp::session::SecurityProfile — MINIMAL stub that ships the no-implicit-default
// sentinel ([const §XII.5] / [2d §4.5] / N-P2-3 / [arch §6 line 243]). SessionConfig
// holds a value-typed member `fixpp::session::SecurityProfile security_profile;`;
// the default-constructed sentinel (`kind::unset`) is REJECTED by Session::open()
// with error::invalid_session_config (slot 53 / FR-018).
//
// Lives in `fixpp::session` per `architecture.md:243` (NOT `fixpp::tls`, which was
// the post-merge layer-violation hotfix on 2026-05-20). The `tls/` module is for
// the concrete TLS implementation (2g) — the KIND discriminant is a session-owned
// closed-enum sentinel, not a TLS-impl artifact, so it lives with the other
// session-owned forward-extension stubs (D-15: `MessageStoreFactory`,
// `ControlPlaneFactory`). Pattern: same "minimal real skeleton, downstream
// extends" as D-15 / D-1 (trace_context). 2g owns the full TLS surface (mTLS-CA,
// mTLS-pinned, one-way-CA concrete implementations, cert_source wiring,
// cipher-suite policy); this feature ships only the KIND discriminant so
// `[const §XII.5]`'s closed-enumeration requirement is honoured now and 2g
// extends without a breaking field-shape change.
//
// [2g extends]: concrete `cert_source` binding, cipher-suite policy, and
// handshake validation logic. The `kind` enum values map to the constitutional
// closed set from `[const §XII.5]` (mtls_ca / mtls_pinned / one_way_ca); the
// `unset` sentinel is the "no valid profile supplied" discriminant rejected at
// open time.
// NOTE (043, 2026-06-17): `insecure_plain_tcp` (k=4) was appended via the
// constitution v0.3 §XII.5 amendment (design-doc amendment D-9 / 043 spec).
// It is gated behind a `[[deprecated]]` friction attribute — see the enumerator.
#pragma once

#include <cstdint>

namespace fixpp::session {

// Closed enumeration of profile kinds (`[const §XII.5]` v0.3 closed set +
// `unset` sentinel). DO NOT add values here without a design-doc amendment
// — the constitutional closed set is: mtls_ca / mtls_pinned / one_way_ca /
// insecure_plain_tcp (v0.3 amendment, 2026-06-17).
// `unset` is the default-constructed sentinel that Session::open() REJECTS.
struct SecurityProfile {
    enum class kind : std::uint8_t {
        unset = 0,        // sentinel — rejected at Session::open() (FR-018)
        mtls_ca = 1,      // mutual TLS with CA-verified peer certificate
        mtls_pinned = 2,  // mutual TLS with certificate pinning
        one_way_ca = 3,   // one-way TLS (server authenticates to client)
        // Loud opt-in: plaintext TCP (no TLS/encryption/peer-auth). MUST be
        // used only over a separately-secured link (colo cross-connect / VPN).
        // The [[deprecated]] attribute fires a -Wdeprecated-declarations
        // diagnostic at every unsuppressed selection site — the construction-
        // site friction prescribed by [const §XII.5] v0.3 / 043 spec SC-005 /
        // D-9. fixpp-internal code that legitimately selects this value wraps
        // the selection in `#pragma clang diagnostic push/ignored/pop` per the
        // one_way_ca precedent at session.cpp. An operator selecting this value
        // without a pragma suppression WILL see the diagnostic. [043 D-9/T019]
        insecure_plain_tcp                                                  //
        [[deprecated("insecure_plain_tcp disables transport security (no "  //
                     "TLS/encryption/peer-auth); use only over a "          //
                     "separately-secured link (colo/VPN) — prefer "         //
                     "mtls_pinned/mtls_ca")]] = 4,
    };

    kind k = kind::unset;  // no-implicit-default (`[const §XII.5]` / N-P2-3)
};

// fixpp-INTERNAL predicate: is this the insecure_plain_tcp profile?
// The [[deprecated]] enumerator fires -Wdeprecated-declarations on every named
// reference, including fixpp's own internal *inspection* sites. Centralizing the
// read here confines the suppression to ONE place rather than scattering
// `#pragma clang diagnostic push/ignored/pop` triplets across session.cpp /
// engine.cpp — each scattered ignore silences ALL deprecation diagnostics in its
// region (a masking hazard). This is a read-only predicate: the SC-005 / D-9
// operator friction targets the *selection/assignment* site (`k = insecure_plain_tcp`)
// in operator config, which this does not touch — an operator selecting the
// enumerator still sees the diagnostic. [043 D-9; /simplify 2026-06-17]
[[nodiscard]] constexpr bool is_insecure_plain_tcp(SecurityProfile::kind k) noexcept {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return k == SecurityProfile::kind::insecure_plain_tcp;
#pragma clang diagnostic pop
}

}  // namespace fixpp::session
