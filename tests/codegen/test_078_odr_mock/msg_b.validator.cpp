#include "msg_b.hpp"
#include "traits.hpp"

// 078 R2a probe — external-linkage validate_ def for MsgB, sharing MsgA's
// GroupArgs plan trait. Compiled only into test078_mock_validators. Linking
// this object alongside the probe TU's own inline msg_a.validator.inl (which
// also defines writer_traits<GroupArgs>::required_count()) is what leg (i)
// exercises: two definitions of the same shared trait must collapse to one
// via `inline`, not raise a duplicate-symbol link error.

namespace test078_mock {

bool validate_MsgB(MsgBArgs const& args) {
    return args.group.value >= writer_traits<GroupArgs>::required_count();
}

} // namespace test078_mock
