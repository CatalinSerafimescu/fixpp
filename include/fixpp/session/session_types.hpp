// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/session_types.hpp
//
// 070-fix44-closeout — small shared value types used by BOTH session_config.hpp
// and admin_messages.hpp. `logon_advertise_options` (in admin_messages.hpp) holds
// a `std::span<const supported_msg_type>`, which requires the COMPLETE type: a
// forward declaration compiles on libstdc++/libc++ but MSVC's `std::span` rejects
// `span<incomplete>` with C2036. Keeping these types in a dependency-light header
// (only <string>) lets admin_messages.hpp obtain the complete definition WITHOUT
// pulling the heavy session_config.hpp (asio / transport) transitively.
#ifndef FIXPP_SESSION_SESSION_TYPES_HPP
#define FIXPP_SESSION_SESSION_TYPES_HPP

#include <string>

namespace fixpp::session {

// data-model E1 — local test/production posture (S-029). Compared against the
// inbound peer's TestMessageIndicator(464) on the Logon handshake.
enum class session_posture { production, test };

// data-model E3 — NoMsgTypes(384) member direction. Renders send→'S', receive→'R'
// on the wire (FIX44 CHAR domain, FIX44.xml:4997-5000). NOTE: a C++ `enum class`
// can still hold an off-enum value via `static_cast`, so the wire rendering in
// build_logon uses an exhaustive switch that FAILS CLOSED (returns an error) on
// any other value (gate-b/r1, PR #189) — it is NOT unrepresentable by construction.
enum class msg_direction { send, receive };

// data-model E3 — one NoMsgTypes(384) advertise entry: RefMsgType(372) +
// MsgDirection(385). SessionConfig::supported_msg_types is an ordered
// std::vector<supported_msg_type>.
struct supported_msg_type {
    msg_direction direction;  // MsgDirection(385)
    std::string msg_type;     // RefMsgType(372)
};

}  // namespace fixpp::session

#endif  // FIXPP_SESSION_SESSION_TYPES_HPP
