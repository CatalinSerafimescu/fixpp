// 078 R2a: ODR / builder⟂validator (SC-003) probe — proves legs (i) and (iii)
// of the split layout EMPIRICALLY via a hand-written mock of the intended
// split (research.md R2a; the real emitter split does not exist yet — see
// tests/codegen/test_078_odr_mock/). Leg (ii) is a separate builder-only
// binary + nm check, wired below in CMakeLists.txt.
#include <gtest/gtest.h>

#include "test_078_odr_mock/msg_a.validator.inl" // force-inline validate_MsgA (leg i)
#include "test_078_odr_mock/msg_b.hpp"

namespace test078_mock {

// Leg (i): validate_MsgA is force-inlined (included above) while
// validate_MsgB — sharing the same GroupArgs plan — resolves from the
// LINKED test078_mock_validators archive. If the shared
// writer_traits<GroupArgs>::required_count() specialization were not
// `inline`, this binary would fail to LINK (duplicate-symbol) rather than
// fail an assertion here: the test IS the link succeeding.
TEST(Test078OdrSc003Probe, ForceInlinedValidatorSharesPlanWithLinkedValidator) {
    EXPECT_TRUE(validate_MsgA(MsgAArgs{{1}}));  // inline definition, this TU
    EXPECT_TRUE(validate_MsgB(MsgBArgs{{1}}));  // pulled from test078_mock_validators
}

// Leg (iii): the binary links BOTH test078_mock_builders and
// test078_mock_validators — physically disjoint <Msg>.builder.cpp /
// <Msg>.validator.cpp objects (R1) — and calls build_/validate_ from each
// side with no duplicate-symbol at link: the test IS the link succeeding.
TEST(Test078OdrSc003Probe, BothLibsLinkWithNoDuplicateSymbol) {
    build_MsgA(MsgAArgs{{1}});
    build_MsgB(MsgBArgs{{1}});
    EXPECT_TRUE(validate_MsgA(MsgAArgs{{1}}));
    EXPECT_TRUE(validate_MsgB(MsgBArgs{{1}}));
}

} // namespace test078_mock
