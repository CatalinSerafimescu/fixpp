// tests/link/tu_a.cpp — default alias (pod_decimal)
// Compiled without FIXPP_DECIMAL_T override.
#include "fixpp/core/decimal_alias.hpp"
// Direct reference to decimal_alias_sentinel<T>::tag — the inline pointer
// fixpp_decimal_alias_lock alone is unreliable because compilers elide the
// address-of-static-to-bool comparison and drop the weak inline. Forcing a
// load of `tag` itself emits a relocation that the linker must resolve.
int tu_a_dummy() {
    return static_cast<int>(fixpp::detail::decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag);
}
