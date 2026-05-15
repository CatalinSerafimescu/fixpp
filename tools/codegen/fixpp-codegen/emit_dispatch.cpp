// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/emit_dispatch.cpp
//
// 003-dictionary-codegen — T036. Shared _dispatch/ headers ([2c §4.8]/[2c
// §6.3]): reify_dispatch_fixt.hpp (7 FIXT admin MsgTypes) +
// reify_dispatch_application.hpp (~470 (version,MsgType) cases, fail-loud
// default — data-model Entity 8 / I-11 / R3).
// Foundational-checkpoint SCAFFOLDING STUB: returns "" so the tool builds;
// US3 task T036 fills the bodies.
#include "emit.hpp"

namespace fixpp::codegen {

std::string emit_dispatch_fixt(std::vector<VersionIR> const& all) {
    (void)all;
    return {};  // TODO(T036): 7 FIXT admin MsgTypes switch
}

std::string emit_dispatch_application(std::vector<VersionIR> const& all) {
    (void)all;
    return {};  // TODO(T036): ~470 (application_version, MsgType) cases
}

}  // namespace fixpp::codegen
