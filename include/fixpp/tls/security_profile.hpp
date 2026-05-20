// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/tls/security_profile.hpp
//
// fixpp::tls::SecurityProfile — MINIMAL stub that ships the no-implicit-default
// sentinel ([const §XII.5] / [2d §4.5] / N-P2-3). SessionConfig holds a
// value-typed member `fixpp::tls::SecurityProfile security_profile;`; the
// default-constructed sentinel (`kind::unset`) is REJECTED by Session::open()
// with error::invalid_session_config (slot 53 / FR-018).
//
// Pattern: same "minimal real skeleton, downstream extends" as D-15
// (MessageStoreFactory / ControlPlaneFactory) and D-1 (trace_context). 2g owns
// the full TLS surface (mTLS-CA, mTLS-pinned, one-way-CA concrete
// implementations, cert_source wiring, cipher-suite policy); this feature ships
// only the KIND discriminant so [const §XII.5]'s closed-enumeration requirement
// is honoured now and 2g extends without a breaking field-shape change.
//
// [2g extends]: concrete `cert_source` binding, cipher-suite policy, and
// handshake validation logic. The `kind` enum values map to the constitutional
// closed set from [const §XII.5] (mtls_ca / mtls_pinned / one_way_ca); the
// `unset` sentinel is the "no valid profile supplied" discriminant rejected at
// open time. A future "no-TLS / plaintext" escape value is 2g's call and will
// be appended as an additional enumerator if [const §XII.5]'s closed set is
// reopened (design-doc amendment required per [const §XVI.3]).
#pragma once

#include <cstdint>

namespace fixpp::tls {

// Closed enumeration of TLS profile kinds ([const §XII.5] closed set +
// `unset` sentinel). DO NOT add values here without a design-doc amendment
// — the constitutional closed set is: mtls_ca / mtls_pinned / one_way_ca.
// `unset` is the default-constructed sentinel that Session::open() REJECTS.
struct SecurityProfile {
    enum class kind : std::uint8_t {
        unset      = 0,   // sentinel — rejected at Session::open() (FR-018)
        mtls_ca    = 1,   // mutual TLS with CA-verified peer certificate
        mtls_pinned = 2,  // mutual TLS with certificate pinning
        one_way_ca = 3,   // one-way TLS (server authenticates to client)
    };

    kind k = kind::unset;  // no-implicit-default ([const §XII.5] / N-P2-3)
};

}  // namespace fixpp::tls
