// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/config_byte_floor.hpp
//
// 090-capi-refusals (fixpp#452) — D-5b: the ONE definition of the configured
// FIX field value byte floor, callable both from C-ABI config validation
// (src/capi/config.cpp, before any Session exists) and from Session::open()
// (src/session/session.cpp). Replaces Session::open's former function-local
// `is_invalid_cred_byte` lambda, which had no linkage and so could not be
// reused from src/capi/config.cpp (FR-013).
//
// Home: `session`, on OWNERSHIP, not on layering — [arch §2.3]'s whitelist
// already permits capi -> session, and src/capi/config.cpp already includes
// other fixpp/session/ headers. The rule is a policy about what may appear in
// a configured FIX field value, whose authority is SessionConfig.
// (contracts/session-config-byte-floor.md §8.4)
//
// POLICY FLOOR, not FIX grammar: fixpp's scanner splits a field at the FIRST
// '=' only (`++i;  // skip '='` before taking the value start), so `372=A=B`
// parses as one field whose value is `A=B` — '=' CAN legally appear in a FIX
// field value as fixpp parses it. This predicate is a conservative
// compatibility/security floor inherited verbatim from the shipped credential
// guard (session.cpp, "Floor: reject any byte < 0x20 ..."), not a statement
// of the grammar. The charset is UNCHANGED from that guard — widening it (all
// C0 bytes, ASCII-printable-only) is a separate decision with a separate
// blast radius. (contracts/session-config-byte-floor.md §2)
#ifndef FIXPP_SESSION_CONFIG_BYTE_FLOOR_HPP
#define FIXPP_SESSION_CONFIG_BYTE_FLOOR_HPP

#include <string_view>

namespace fixpp::session {

// Reports whether `value` contains a byte this engine refuses in a
// configured FIX field value: any byte < 0x20 (SOH \x01 included) or '='
// (0x3D).
[[nodiscard]] constexpr bool contains_forbidden_config_byte(std::string_view value) noexcept {
    // A raw loop, not std::ranges::any_of: <algorithm> would break this leaf
    // header's contract of depending on nothing but <string_view> (FR-013).
    // NOLINTNEXTLINE(readability-use-anyofallof)
    for (unsigned char c : value) {
        if (c < 0x20U || c == static_cast<unsigned char>('=')) {
            return true;
        }
    }
    return false;
}

}  // namespace fixpp::session

#endif  // FIXPP_SESSION_CONFIG_BYTE_FLOOR_HPP
