// src/capi/capi.cpp
// Phase 3 skeleton: implements the single C ABI symbol wired for SWIG.
// Full C ABI surface per api-contract.md §7 lands incrementally in Phase 4.

#include "fix/c_api.h"
#include "fixpp/core/version.hpp"

extern "C" {

const char* fixpp_version_string(void) {
    return fixpp::core::FIXPP_VERSION;
}

}  // extern "C"
