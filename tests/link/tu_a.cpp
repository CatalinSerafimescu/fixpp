// tests/link/tu_a.cpp — default alias (pod_decimal).
// Compiled without FIXPP_DECIMAL_T override. Real-consumer pattern: includes
// the header, does not directly reference decimal_alias_sentinel<T>::tag.
// The production guard in decimal_alias.hpp is what must trigger the link
// reference; that's the property under test.
#include "fixpp/core/decimal_alias.hpp"
int tu_a_dummy() { return 0; }
