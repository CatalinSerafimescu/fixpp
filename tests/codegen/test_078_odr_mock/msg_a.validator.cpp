#include "msg_a.hpp"
#include "traits.hpp"

// 078 R2a probe — external-linkage validate_ def (mirrors data-model.md
// Entity 4: messages/<Msg>.validator.cpp). Uses the shared group-plan trait.
// Compiled only into test078_mock_validators.

namespace test078_mock {

bool validate_MsgA(MsgAArgs const& args) {
    return args.group.value >= writer_traits<GroupArgs>::required_count();
}

} // namespace test078_mock
