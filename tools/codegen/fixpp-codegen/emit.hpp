// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/emit.hpp
//
// 003-dictionary-codegen — emitter entry points. One free function per
// generated artifact ([2c §4.7]/[2c §4.2]/[2c §4.8]). Each returns the full
// header/markdown text (deterministic, LF-only) for `ir`; an empty string
// means "nothing to emit for this version". Bodies are filled by the US1+
// tasks (T023-T027 emit_fields/messages/validator/normative_refs; T032
// emit_reify; T036 emit_dispatch) — at the Foundational checkpoint they are
// scaffolding stubs so the tool builds and links.
#pragma once
#include <string>
#include <vector>

#include "ir.hpp"

namespace fixpp::codegen {

[[nodiscard]] std::string emit_fields(VersionIR const& ir);          // <vXX>/Fields.hpp
[[nodiscard]] std::string emit_messages(VersionIR const& ir);        // <vXX>/Messages.hpp
[[nodiscard]] std::string emit_validator(VersionIR const& ir);       // <vXX>/Validator.hpp
[[nodiscard]] std::string emit_reify(VersionIR const& ir);           // <vXX>/Reify.hpp
[[nodiscard]] std::string emit_normative_refs(VersionIR const& ir);  // <vXX>/NormativeReferences.md

// 067-codegen-writer-emitter: the write emitter — build_<Msg>/<Msg>Args/
// validate_<Msg> over wire::body_builder, for every OFFICIAL message. Returns
// "" for non-v44 versions (writer-emitter is v44-scoped for v1.0); `write_file`'s
// empty-skip then writes no Builders.hpp for those versions.
[[nodiscard]] std::string emit_builders(VersionIR const& ir);  // <vXX>/Builders.hpp

// Shared dispatch headers ([2c §4.8]/[2c §6.3]) — emitted once over ALL
// codegen versions, not per-version. _dispatch/reify_dispatch_{fixt,application}.hpp
[[nodiscard]] std::string emit_dispatch_fixt(std::vector<VersionIR> const& all);
[[nodiscard]] std::string emit_dispatch_application(std::vector<VersionIR> const& all);

}  // namespace fixpp::codegen
