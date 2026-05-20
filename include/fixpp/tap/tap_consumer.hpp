// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/tap/tap_consumer.hpp
//
// fixpp::tap::TapConsumer — MINIMAL stub that ships a value-typed, default-
// constructable "no tap" sentinel. SessionConfig holds a value-typed member
// `fixpp::tap::TapConsumer tap_consumer;`; the default-constructed value means
// "no tap consumer installed" (a null pointer alternative is NOT the contract
// shape — [arch §5.6] / contracts/session_config.hpp:76).
//
// Pattern: same "minimal real skeleton, downstream extends" as D-15
// (MessageStoreFactory / ControlPlaneFactory) and the SecurityProfile stub in
// this same RC#1 fix. The contract oracle pins the field as value-typed; the
// concrete tap surface (message-tap hooks, filter configuration) is owned by
// 2l (tap feature).
//
// [2l extends]: concrete tap hooks (onInbound, onOutbound), filter policy, and
// tap-channel binding. The default-constructed TapConsumer is the legal
// "no tap" state; Session::open() does NOT reject it (no constitutional
// no-implicit-default constraint applies to TapConsumer — unlike SecurityProfile
// which has [const §XII.5]).
#pragma once

namespace fixpp::tap {

// Minimal value-typed stub — default = no tap installed.
// [2l extends] the full tap-hook surface.
struct TapConsumer {
    // No fields in the minimal stub. Default-constructed = no tap.
    // 2l adds: bool is_active() const noexcept; + hook surface.
};

}  // namespace fixpp::tap
