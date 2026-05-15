// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/emit_normative_refs.cpp
//
// 003-dictionary-codegen — T026 — <vXX>/NormativeReferences.md per-message citations (AC-V5; [const §VI.5]).
// Foundational-checkpoint SCAFFOLDING STUB: returns "" (no artifact emitted)
// so the host tool builds and links. The full deterministic emitter body is
// implemented by the named US1+ task; until then main.cpp writes no file for
// this artifact (write_file() skips empty content).
#include "emit.hpp"

namespace fixpp::codegen {

std::string emit_normative_refs(VersionIR const& ir) {
    (void)ir;
    return {};  // TODO(T026 — <vXX>/NormativeReferences.md per-message citations (AC-V5; [const §VI.5])): emit per the contract oracle + data-model entity
}

}  // namespace fixpp::codegen
