// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/detail/validate_compid_filesystem_safety.hpp
//
// fixpp::session::detail::validate_compid_filesystem_safety —
// shared CompID filesystem-safety validation predicate.
//
// Anchor: .specify/2e-msgstore.md v0.5 §D.4 (Gap 1 close).
// FR-008 / FR-030 / [2e §D.4] / [2e §9 seam #2 N-3].
//
// ENGINE-INTERNAL.  Do not use outside fixpp::session implementation TUs.
//
// The definition lives in src/session/file_store_factory.cpp (keeps
// platform-specific headers <unistd.h> / <windows.h> out of every TU
// that merely #includes this header).  Any TU that needs the function
// must also link fixpp_session.
//
// Called from two bind points:
//   1. FileStoreFactory::make()        (T026 / src/session/file_store_factory.cpp)
//   2. cfg_to_file_store_factory()     (T047 / src/session/quickfix_compat/cfg_loader.cpp)
//
// Single source of truth for the rule set — no rule drift between the two
// bind points per the US4 contract.
#pragma once

#include <fixpp/core/error.hpp>  // fixpp::core::error
#include <optional>
#include <string_view>

namespace fixpp::session::detail {

// validate_compid_filesystem_safety: checks a single CompID string for all
// filesystem-safety constraints per [2e §D.4].
//
// Parameters:
//   compid                        — the CompID string to validate.
//   directory_native              — native() string of the store directory
//                                   (used for NAME_MAX pathconf query on Linux,
//                                    MAX_PATH arithmetic on Windows).
//   other_compid              — the *other* CompID (sender when validating
//                               target, target when validating sender); its
//                               .size() contributes to the composed filename
//                               length check.
//
// Returns std::nullopt if the CompID is valid; otherwise returns
// core::error::store_factory_failed.
//
// IMPLEMENTATION DISCIPLINE (N-5 noexcept-safe primitives):
//   - Uses ONLY std::string_view::find_first_of / find / size()
//   - Does NOT call std::filesystem::path constructors at validation time
//   - noexcept — safe to call from any noexcept context
[[nodiscard]] std::optional<fixpp::core::error> validate_compid_filesystem_safety(
    std::string_view compid, std::string_view directory_native,
    std::string_view other_compid) noexcept;

}  // namespace fixpp::session::detail
