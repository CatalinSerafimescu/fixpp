#pragma once

#include "msg_a.hpp"
#include "traits.hpp"

// 078 R2a probe — inline validate_ body (mirrors data-model.md Entity 3:
// messages/<Msg>.validator.inl): same body as msg_a.validator.cpp, differing
// only in linkage (inline). Force-inline mode source — leg (i) includes this
// directly instead of linking the extern validate_MsgA declared in msg_a.hpp.

namespace test078_mock {

inline bool validate_MsgA(MsgAArgs const& args) {
    return args.group.value >= writer_traits<GroupArgs>::required_count();
}

} // namespace test078_mock
