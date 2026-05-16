// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 1 contract — 003-OWNED surface, literal extract of [2c §4.3]
// (.specify/2c-codegen.md v1.4 §4.3, lines 371-501).
//
// RC#1 RESOLUTION (re-/plan 2026-05-15). Gate A round 1 (Codex P1-1 / Opus
// root cause #1) established that 002 ships ONLY the `session_version` and
// `application_version` enums in include/fixpp/dict/version_profile.hpp and
// EXPLICITLY DEFERRED the `version_profile` struct + the
// `dict::resolve_application_version` free function
// (specs/002-dictionary-xml-loader/contracts/version_profile.hpp:59-66,
// verified on disk: include/fixpp/dict/version_profile.hpp carries the two
// enums only). 2c §4.3 designs both but assigns no shipping owner; the free
// function exists "so callers that don't hold a Dictionary (notably
// dict::reify) can run the resolution". Therefore 003 OWNS them.
//
// FILES-IN-SCOPE consequence: this is NOT a new file. 003 ADDITIVELY EDITS
// the 002-shipped include/fixpp/dict/version_profile.hpp — appending the
// `version_profile` + `resolved_message_version` structs and the
// `resolve_application_version` free-function declaration BELOW the existing
// (002-owned, unchanged) `session_version` / `application_version` enums.
// Same additive-edit discipline as the core/error.hpp enum extension
// (research.md D-10). The two 002 enums are reproduced here ONLY for context;
// 003 does not redeclare or renumber them.
#pragma once
#include <fixpp/core/error.hpp>   // fixpp::core::expected_t, fixpp::core::error
#include <cstdint>
#include <string_view>

namespace fixpp::dict {

// ─── 002-SHIPPED, UNCHANGED (context only; 003 does NOT redeclare these) ────
// enum class session_version     : std::uint8_t { Unknown=0, v40=1, v41=2,
//     v42=3, v43=4, v44=5, v50=6, v50sp1=7, v50sp2=8, vt11=9 };
// enum class application_version : std::uint8_t { Unknown=0, v40=1, v41=2,
//     v42=3, v43=4, v44=5, v50=6, v50sp1=7, v50sp2=8 };
// (live at include/fixpp/dict/version_profile.hpp, shipped by 002 / PR #66.)

// ─── 003-OWNED ADDITIONS (appended to the 002 file at re-/plan) ─────────────

// 4-byte profile carried by every Dictionary; the resolution input
// dict::reify consumes (it holds no Dictionary). Verbatim [2c §4.3:408-422].
struct version_profile {
    session_version     session;                  // 1 byte
    application_version default_appl;             // 1 byte
    bool                has_per_message_override; // 1 byte; true iff FIXT.1.1
                                                  // AND ApplVerID(1128) allowed
                                                  // per message ([FIXT §5.3]).
    std::uint8_t        _reserved;                // pad to 4; zero on emit,
                                                  // ignore on read in v1.0.
};
static_assert(sizeof(version_profile) == 4);
static_assert(std::is_trivially_copyable_v<version_profile>);

// Resolved per-message version axis ([2c §4.3:440-455]). kind-first dispatch:
// session_admin → vt11 (application == Unknown); application → resolved value.
struct resolved_message_version {
    enum class kind : std::uint8_t { session_admin, application };
    kind                k;             // 1 byte
    session_version     session;       // 1 byte; vt11 for session_admin
    application_version application;    // 1 byte; Unknown when k==session_admin
    std::uint8_t        _reserved;     // pad to 4; zero on emit, ignore on read
};
static_assert(sizeof(resolved_message_version) == 4);
static_assert(alignof(resolved_message_version) == 1);
static_assert(std::is_trivially_copyable_v<resolved_message_version>);

// Free-function form of the [FIXT §5] priority resolution ([2c §4.3:457-481]).
// Profile-only (no Dictionary needed) so dict::reify — which receives only
// MessageView + version_profile + mr — can run it. Dictionary's member
// resolve_application_version is a thin wrapper:
//   dict::resolve_application_version(this->which(), appl_ver_id_value).
//
// Algorithm ([2c §4.3:465-474]):
//   1. appl_ver_id_value non-empty → parse via the WIRE→C++ mapping table
//      below ([2c §4.3:488-501]). On success → resolved application_version;
//      on parse failure → core::error::dict_unknown_appl_ver_id.
//   2. appl_ver_id_value empty → use profile.default_appl. If that is
//      application_version::Unknown → core::error::
//      dict_unresolved_application_version (per RC#1 / AC-D6; the v1.0
//      misdiagnosis sentinel-fall-through path is CLOSED — it no longer
//      returns dict_reify_unknown_msg_type).
[[nodiscard]] expected_t<application_version>
resolve_application_version(version_profile  profile,
                            std::string_view appl_ver_id_value) noexcept;

// ─── Wire ApplVerID(1128) → C++ application_version mapping ([2c §4.3:486]) ──
// [FIXT §5.1] (DefaultApplVerID 1137) / [FIXT §5.3] (per-message ApplVerID
// 1128). The FIX-defined WIRE enum values do NOT coincide with this header's
// C++ application_version internal indices (Unknown=0,v40=1,…,v50sp2=8). The
// parse MUST use this table; reusing the C++ index would mis-map FIX 5.0 and
// the SP variants (N2-P3-1). New ACs AC-VP3 + AC-VP4 pin this table + the
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
// `dict_field_not_present` enum slot; the 6 new slots are exactly the RC#1 +
// D-10 set listed in data-model.md "Error mapping".

}  // namespace fixpp::dict
