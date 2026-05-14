// SPDX-License-Identifier: AGPL-3.0-or-later
// extract from .specify/2c-codegen.md v1.3 §4.3 (signed off 2026-05-10)
// This file is a /plan Phase 1 contract extract — the canonical declaration
// lives at include/fixpp/dict/version_profile.hpp once 002-dictionary-xml-loader
// reaches /implement. Verbatim subset from §4.3; the `version_profile` struct
// and `resolve_application_version` free function are out of scope for this
// PR (they ship with the wire/session integration features).
//
// What this PR materializes: the `session_version` and `application_version`
// enums — needed so `XmlLoader::load` can record the loaded XML's version
// against the v1.0 supported nine per `[2c §1.3]` and reject the rest with
// `dict::unknown_version_error` (AC-L4).

#pragma once

#include <cstdint>

namespace fixpp::dict {

// The *session* version on the wire, per `[FIXT §5]`. For unified
// pre-FIXT.1.1 sessions, both session and application-default coincide.
//
// Per /clarify Q1 → B (spec.md §1) this PR ships XML data and per-version
// headline tests for: `v42`, `v44`, `v50sp2`, `vt11` (the four codegen-target
// versions). The loader code path structurally accepts all nine values per
// AC-L4; F1 (spec.md §10) tracks the five runtime-XML-only versions.
enum class session_version : std::uint8_t {
    Unknown = 0,
    v40     = 1,    // runtime-XML only (no codegen namespace) — F1
    v41     = 2,    // runtime-XML only — F1
    v42     = 3,    // codegen + runtime-XML
    v43     = 4,    // runtime-XML only — F1
    v44     = 5,    // codegen + runtime-XML
    v50     = 6,    // runtime-XML only — F1
    v50sp1  = 7,    // runtime-XML only — F1
    v50sp2  = 8,    // codegen + runtime-XML
    vt11    = 9,    // codegen (FIXT.1.1 session-layer); split-vocabulary
                    // parent
};

// The *default application* version a FIXT.1.1 session resolves to when
// `ApplVerID(1128)` is absent on a message and `DefaultApplVerID(1137)` was
// set at Logon time (`[FIXT §5.1]`). For unified pre-FIXT.1.1 sessions, this
// equals the session version. The session FSM (Phase 4, separate feature)
// walks the resolution algorithm at message time; this PR records the
// *value space* for use by the loader's version-string parse.
enum class application_version : std::uint8_t {
    Unknown = 0,
    v40     = 1,
    v41     = 2,
    v42     = 3,
    v43     = 4,
    v44     = 5,
    v50     = 6,
    v50sp1  = 7,
    v50sp2  = 8,
};

// `version_profile` struct + `resolve_application_version` free function
// (both declared in `[2c §4.3]`) are deferred to the wire/session
// integration feature where the FIXT.1.1 cross-vocabulary dispatch is
// implemented. The runtime XML loader does not need them: `XmlLoader::load`
// derives the `session_version` from the XML's `<fix major minor [servicepack]>`
// header at parse time and stores it inside the returned `Dictionary`
// (accessed via `Dictionary::which_session_version()` per
// contracts/dictionary.hpp).

}  // namespace fixpp::dict
