#pragma once

#include "groups.hpp"

// 078 R2a probe — mock validator-scoped shared group-plan trait (mirrors
// data-model.md Entity 1b: fixpp/<ns>/validators/traits.hpp). The out-of-line
// `inline` definition is what makes this specialization collapse to a single
// definition when it appears both in a force-inlined validator TU and in a
// separately-compiled linked validator TU that shares the same plan (R2a leg
// i). Included only by the validator surface, never by the builder surface.

namespace test078_mock {

template <typename T>
struct writer_traits;

template <>
struct writer_traits<GroupArgs> {
    static int required_count();
};

inline int writer_traits<GroupArgs>::required_count() { return 1; }

} // namespace test078_mock
