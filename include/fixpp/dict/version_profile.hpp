// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/version_profile.hpp
//
// Session and application FIX version enums. Canonical declaration; mirrors
// the `[2c §4.3]` subset recorded at
// `specs/002-dictionary-xml-loader/contracts/version_profile.hpp`. The full
// `version_profile` struct + `resolve_application_version` free function
// (both declared in `[2c §4.3]`) are deferred to the wire/session
// integration feature; the runtime XML loader does not need them.
//
// What this PR materialises: the `session_version` and `application_version`
// enums — needed so `XmlLoader::load` can record the loaded XML's version
// against the v1.0 supported nine per `[2c §1.3]` and reject the rest with
// `dict::unknown_version_error` (AC-L4).

#pragma once

#include <cstdint>
#include <fixpp/core/error.hpp>  // fixpp::core::expected_t, fixpp::core::error
#include <string_view>
#include <type_traits>

namespace fixpp::dict {

// The *session* version on the wire, per `[FIXT §5]`. For unified
// pre-FIXT.1.1 sessions, both session and application-default coincide.
//
// Per /clarify Q1 → B (spec.md §1) this PR ships XML data and per-version
// headline tests for: `v42`, `v44`, `v50sp2`, `vt11`. The loader code path
// structurally accepts all nine values per AC-L4; F1 (spec.md §10) tracks
// the five runtime-XML-only versions.
enum class session_version : std::uint8_t {
    Unknown = 0,
    v40 = 1,       // runtime-XML only (no codegen namespace) — F1
    v41 = 2,       // runtime-XML only — F1
    v42 = 3,       // codegen + runtime-XML
    v43 = 4,       // runtime-XML only — F1
    v44 = 5,       // codegen + runtime-XML
    v50 = 6,       // runtime-XML only — F1
    v50sp1 = 7,    // runtime-XML only — F1
    v50sp2 = 8,    // codegen + runtime-XML
    vt11 = 9,      // codegen (FIXT.1.1 session-layer)
    vlatest = 10,  // 074: FIX Latest (Orchestra EP303) — runtime-dictionary read tier
                   // only; wire application version maps to v50sp2 (no distinct ApplVerID).
};

// The *default application* version a FIXT.1.1 session resolves to when
// `ApplVerID(1128)` is absent on a message and `DefaultApplVerID(1137)` was
// set at Logon time (`[FIXT §5.1]`). For unified pre-FIXT.1.1 sessions, this
// equals the session version.
enum class application_version : std::uint8_t {
    Unknown = 0,
    v40 = 1,
    v41 = 2,
    v42 = 3,
    v43 = 4,
    v44 = 5,
    v50 = 6,
    v50sp1 = 7,
    v50sp2 = 8,
};

// ─── 003-OWNED ADDITIONS (RC#1, re-/plan 2026-05-15) ────────────────────────
// Appended below the unchanged 002 enums, non-renumbering. 002 deferred the
// `version_profile` struct + `resolve_application_version` free function
// (specs/002-dictionary-xml-loader/contracts/version_profile.hpp:59-66); 003
// owns them. Shapes are verbatim [2c §4.3:408-481]; oracle:
// specs/003-dictionary-codegen/contracts/version_profile.hpp; data-model
// Entity 10; spec §4.8 AC-VP1..AC-VP6.

// 4-byte profile carried by every Dictionary; the resolution input
// `dict::reify` consumes (it holds no Dictionary). Verbatim [2c §4.3:408-422].
struct version_profile {
    session_version session;           // 1 byte
    application_version default_appl;  // 1 byte
    bool has_per_message_override;     // 1 byte; true iff FIXT.1.1
                                       // AND ApplVerID(1128) allowed
                                       // per message ([FIXT §5.3]).
    std::uint8_t _reserved;            // pad to 4; zero on emit,
                                       // ignore on read in v1.0.
};
static_assert(sizeof(version_profile) == 4);
static_assert(std::is_trivially_copyable_v<version_profile>);

// Resolved per-message version axis ([2c §4.3:440-455]). kind-first dispatch:
// session_admin → vt11 (application == Unknown); application → resolved value.
struct resolved_message_version {
    enum class kind : std::uint8_t { session_admin, application };
    kind k;                           // 1 byte
    session_version session;          // 1 byte; vt11 for session_admin
    application_version application;  // 1 byte; Unknown when k==session_admin
    std::uint8_t _reserved;           // pad to 4; zero on emit, ignore on read
};
static_assert(sizeof(resolved_message_version) == 4);
static_assert(alignof(resolved_message_version) == 1);
static_assert(std::is_trivially_copyable_v<resolved_message_version>);

// Free-function form of the [FIXT §5] priority resolution ([2c §4.3:457-481]).
// Profile-only (no Dictionary needed) so `dict::reify` — which receives only
// MessageView + version_profile + mr — can run it. Dictionary's member
// `resolve_application_version` is a thin wrapper:
//   dict::resolve_application_version(this->which(), appl_ver_id_value).
//
// Algorithm ([2c §4.3:465-474]):
//   1. appl_ver_id_value non-empty → parse via the WIRE→C++ mapping table
//      below ([2c §4.3:488-501]). On success → resolved application_version;
//      on parse failure → core::error::dict_unknown_appl_ver_id.
//   2. appl_ver_id_value empty → use profile.default_appl. If that is
//      application_version::Unknown → core::error::
//      dict_unresolved_application_version (RC#1 / AC-D6; the v1.0
//      misdiagnosis sentinel-fall-through is CLOSED — it no longer returns
//      dict_reify_unknown_msg_type).
[[nodiscard]] core::expected_t<application_version> resolve_application_version(
    version_profile profile, std::string_view appl_ver_id_value) noexcept;

// ─── Wire ApplVerID(1128) → C++ application_version mapping ([2c §4.3:486]) ──
// [FIXT §5.1] (DefaultApplVerID 1137) / [FIXT §5.3] (per-message ApplVerID
// 1128). The FIX-defined WIRE enum values do NOT coincide with this header's
// C++ application_version internal indices (Unknown=0,v40=1,…,v50sp2=8). The
// parse MUST use this table; reusing the C++ index would mis-map FIX 5.0 and
// the SP variants (N2-P3-1). ACs AC-VP3 + AC-VP4 pin this table + the
// "do not reuse the C++ index" negative property.
//
//   wire "1128"   FIX version    C++ application_version
//   "0"           FIX 2.7        (pre-4.0; reject → dict_unknown_appl_ver_id)
//   "1"           FIX 3.0        (unused;  reject → dict_unknown_appl_ver_id)
//   "2"           FIX 4.0        application_version::v40
//   "3"           FIX 4.1        application_version::v41
//   "4"           FIX 4.2        application_version::v42
//   "5"           FIX 4.3        application_version::v43
//   "6"           FIX 4.4        application_version::v44
//   "7"           FIX 5.0        application_version::v50
//   "8"           FIX 5.0 SP1    application_version::v50sp1
//   "9"           FIX 5.0 SP2    application_version::v50sp2
//   ""  (empty)   →  use profile.default_appl ([FIXT §5.1])
//
// Cross-feature dependency note (avoids an RC#1-class phantom-ownership
// repeat): the "field absent" error returned by
// wire::MessageView::get<1128>() is a 2b/wire-owned code — NOT a 003-owned
// core::error slot. dict::reify maps "get<1128>() reported field-absent" →
// empty appl_ver_id_value (→ step 2). 003 does NOT define a
// `dict_field_not_present` slot; the 6 new slots (23..28) are exactly the
// RC#1 + D-10 set in data-model.md "Error mapping" (spec Assumption A6).

// ─── 033 ADDITION — inverse render helper (data-model.md E3 / research R3) ──
// application_version → wire ApplVerID(1137) string, the inverse of
// resolve_application_version (wire→C++). MUST use this table; never reuse the
// C++ enum index (which diverges: v40→"2" not "1", v50→"7" not "6", etc.).
// Returns `std::unexpected` for application_version::Unknown or any out-of-range
// value — no garbage string reaches the wire.
// Inline/constexpr — no out-of-line compilation unit required; the view points
// into static literal storage (no lifetime hazard).
[[nodiscard]] inline core::expected_t<std::string_view> render_appl_ver_id(
    application_version v) noexcept {
    switch (v) {
        case application_version::v40:
            return std::string_view{"2"};
        case application_version::v41:
            return std::string_view{"3"};
        case application_version::v42:
            return std::string_view{"4"};
        case application_version::v43:
            return std::string_view{"5"};
        case application_version::v44:
            return std::string_view{"6"};
        case application_version::v50:
            return std::string_view{"7"};
        case application_version::v50sp1:
            return std::string_view{"8"};
        case application_version::v50sp2:
            return std::string_view{"9"};
        case application_version::Unknown:
            break;
    }
    return std::unexpected{core::error::dict_unknown_appl_ver_id};
}

}  // namespace fixpp::dict
