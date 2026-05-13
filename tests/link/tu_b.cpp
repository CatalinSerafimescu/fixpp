// tests/link/tu_b.cpp — mismatched alias
// Compiled with -DFIXPP_DECIMAL_T=::fixpp::core::test::mismatch_type
// This forces a reference to decimal_alias_sentinel<mismatch_type>::tag which
// is never defined, causing an unresolved symbol at link time.
namespace fixpp::core::test { struct mismatch_type {}; }
#include "fixpp/core/decimal_alias.hpp"
int tu_b_dummy() { return fixpp::detail::fixpp_decimal_alias_lock ? 1 : 0; }
int main() { return tu_a_dummy() + tu_b_dummy(); }
extern int tu_a_dummy();
