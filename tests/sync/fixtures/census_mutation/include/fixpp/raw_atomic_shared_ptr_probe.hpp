// SPDX-License-Identifier: AGPL-3.0-or-later
// Mutation fixture for SC-004: a deliberate raw std::atomic<std::shared_ptr>
// re-introduction. tools/check_no_raw_atomic_shared_ptr.sh MUST reject this
// (the census-regrowth ctest runs the guard against this fixture root and
// asserts a non-zero exit). NOT part of the library build.
#pragma once
#include <atomic>
#include <memory>

namespace fixpp::census_probe {
struct Regrowth {
  std::atomic<std::shared_ptr<int>> reintroduced_raw_slot_;
};
}  // namespace fixpp::census_probe
