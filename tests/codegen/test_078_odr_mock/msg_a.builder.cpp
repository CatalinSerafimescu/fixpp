#include "msg_a.hpp"

// 078 R2a probe — external-linkage build_ def (mirrors data-model.md Entity
// 4: messages/<Msg>.builder.cpp). References groups.hpp data only — no
// validator symbol (SC-003 leg ii). Compiled only into test078_mock_builders.

namespace test078_mock {

void build_MsgA(MsgAArgs const& args) { (void)args; }

} // namespace test078_mock
