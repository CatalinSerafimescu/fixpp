// tests/link/tu_a.cpp — default alias (pod_decimal)
// Compiled without FIXPP_DECIMAL_T override.
#include "fixpp/core/decimal_alias.hpp"
int tu_a_dummy() { return fixpp::detail::fixpp_decimal_alias_lock ? 1 : 0; }
