// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/loader_policy.hpp
//
// 083-group-delimiter-resolution FR-006a / C-6.3..C-6.6 — the ONE load-time
// policy knob shared by both dictionary loaders.
//
// Declared in its own header, and shared rather than duplicated, because
// C-6.4 requires the same option with the same semantics in `XmlLoader` and
// `OrchestraLoader`; two independently-declared enums would be free to drift.

#pragma once

#include <cstdint>

namespace fixpp::dict {

// What a loader does when a group's delimiter cannot be resolved from its own
// declaration — i.e. the group's document-order walk emitted no first member.
//
// C-6.3: tolerance is NEVER implicit, NEVER inferred, and is NEVER the
// fallback taken on a parse difficulty. It is only ever what the caller asked
// for, by naming it at the call site.
//
// C-6.5: this is consulted at LOAD time only. Neither the parse path nor the
// validate path branches on it — a loaded `Dictionary` carries no trace of
// which policy produced it, only the groups that survived.
enum class unresolved_group_policy : std::uint8_t {
    // FR-006, the DEFAULT: reject the load, with a diagnostic naming the
    // offending group. Mirrors the disposition the loaders already take for
    // every sibling violation (root-not-<fix>, missing <fields>, duplicate
    // field number, unknown type, a <group> with no matching <field>). A
    // silent zero-delimiter drop was the outlier, and it is what concealed
    // three FIX50SP2 groups.
    fail_closed = 0,

    // FR-006a, explicit opt-in: skip the group and keep loading, so a
    // third-party or partial dictionary remains usable. The skipped group is
    // left UNREGISTERED rather than half-registered — it never reaches the
    // consumer's enumeration at all (FR-023a).
    tolerant = 1,
};

}  // namespace fixpp::dict
