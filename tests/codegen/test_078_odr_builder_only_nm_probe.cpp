// 078 R2a leg (ii): builder-only binary — links ONLY test078_mock_builders,
// calls one build_. CI wiring (CMakeLists.txt) runs `nm --defined-only` on
// this binary and asserts zero validate_/writer_traits symbols (SC-003).
#include "test_078_odr_mock/msg_a.hpp"

int main() {
    test078_mock::build_MsgA(test078_mock::MsgAArgs{{1}});
    return 0;
}
