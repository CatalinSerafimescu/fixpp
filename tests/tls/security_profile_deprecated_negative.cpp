// security_profile_deprecated_negative.cpp
// Negative-compile witness: referencing SecurityProfile::one_way_ca
// with -Wdeprecated-declarations -Werror must emit a warning/error.
#include <fixpp/tls/security_profile.hpp>
int main() {
    auto x = fixpp::tls::SecurityProfile::one_way_ca;
    (void)x;
    return 0;
}
