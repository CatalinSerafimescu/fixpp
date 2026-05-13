// src/capi/decimal_assert.cpp
// Seam #4 compile-time anchor — six static_asserts verifying fixpp_decimal_t layout.
// These fail to compile until include/fix/c_api/decimal.h has the correct shape.
// NOT a runtime test; included in the fixpp_capi library target.

#include <cstddef>
#include <type_traits>

#include "fix/c_api/decimal.h"

static_assert(sizeof(fixpp_decimal_t) == 16, "fixpp_decimal_t must be 16 bytes [const §X.3]");

static_assert(alignof(fixpp_decimal_t) == 8, "fixpp_decimal_t must align to 8 bytes");

static_assert(offsetof(fixpp_decimal_t, mantissa) == 0, "mantissa must be at offset 0");

static_assert(offsetof(fixpp_decimal_t, exponent) == 8, "exponent must be at offset 8");

static_assert(offsetof(fixpp_decimal_t, _reserved) == 9, "_reserved must be at offset 9");

static_assert(std::is_standard_layout_v<fixpp_decimal_t>,
              "fixpp_decimal_t must satisfy is_standard_layout [const §X.3]");
