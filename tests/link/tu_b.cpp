// tests/link/tu_b.cpp — mismatched alias.
// Compiled with -DFIXPP_DECIMAL_T=::fixpp::core::test::mismatch_type.
// Real-consumer pattern: just includes the header and uses fixpp::decimal_t.
// The production guard must emit a relocation against
// decimal_alias_sentinel<mismatch_type>::tag with no consumer-side help; the
// library never defines that specialization, so the link must fail with an
// "undefined reference to ... decimal_alias_sentinel<...mismatch_type>::tag"
// message (AC-B3).
namespace fixpp::core::test { struct mismatch_type {}; }
#include "fixpp/core/decimal_alias.hpp"
extern int tu_a_dummy();
int tu_b_dummy() { return 0; }
int main() { return tu_a_dummy() + tu_b_dummy(); }
