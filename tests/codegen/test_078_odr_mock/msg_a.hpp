#pragma once

#include "groups.hpp"

// 078 R2a probe — mock slim per-message decl header (mirrors data-model.md
// Entity 2: messages/<Msg>.hpp). build_/validate_ are declared extern; the
// probe TU that wants force-inline mode includes msg_a.validator.inl instead
// of relying on this declaration.

namespace test078_mock {

struct MsgAArgs {
    GroupArgs group;
};

void build_MsgA(MsgAArgs const& args);
bool validate_MsgA(MsgAArgs const& args);

} // namespace test078_mock
