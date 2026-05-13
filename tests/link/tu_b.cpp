// tests/link/tu_b.cpp — mismatched alias
// Compiled with -DFIXPP_DECIMAL_T=::fixpp::core::test::mismatch_type
// This forces a reference to decimal_alias_sentinel<mismatch_type>::tag which
// is never defined, causing an unresolved symbol at link time.
namespace fixpp::core::test { struct mismatch_type {}; }
#include "fixpp/core/decimal_alias.hpp"
extern int tu_a_dummy();
// Direct load of decimal_alias_sentinel<mismatch_type>::tag — the library
// never defines this specialization, so the relocation cannot be resolved
// and the link must fail with an "undefined reference to ...
// decimal_alias_sentinel<...mismatch_type>::tag" message (AC-B3).
int tu_b_dummy() {
    return static_cast<int>(fixpp::detail::decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag);
}
int main() { return tu_a_dummy() + tu_b_dummy(); }
