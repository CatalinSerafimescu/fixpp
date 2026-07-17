#pragma once

#include "groups.hpp"

// 078 R2a probe — mock slim per-message decl header for the SECOND message,
// sharing MsgA's GroupArgs plan (mirrors data-model.md Entity 2).

namespace test078_mock {

struct MsgBArgs {
    GroupArgs group;
};

void build_MsgB(MsgBArgs const& args);
bool validate_MsgB(MsgBArgs const& args);

} // namespace test078_mock
