// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/emit_builders.cpp
//
// 067-codegen-writer-emitter — the write emitter: build_<Msg>(out,args) /
// <Msg>Args / validate_<Msg> over wire::body_builder, for every OFFICIAL
// message (data-model.md §1-§3). T005: empty-output scaffolding only — the
// per-message group planner (T016) and the builder/validate bodies (T017,
// T025) land in Phase 3/US3. `write_file`'s empty-skip (main.cpp) means no
// Builders.hpp is written while this returns "", so Phase-3 tests stay RED
// until implemented (TDD-friendly), not stale-file green.
#include "emit.hpp"

namespace fixpp::codegen {

std::string emit_builders(VersionIR const& /*ir*/) {
    return {};
}

}  // namespace fixpp::codegen
