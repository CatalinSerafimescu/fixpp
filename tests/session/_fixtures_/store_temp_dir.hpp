// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/_fixtures_/store_temp_dir.hpp
//
// MIGRATION SHIM — the helper itself now lives at tests/support/temp_dir.hpp in
// namespace `fixpp::test_support`, because tests/log needed it too and a
// tests/log file including a fixture out of tests/session is a layering
// inversion (#404).
//
// This header exists so the existing `fixpp::store_test::unique_store_dir` /
// `remove_store_dir` call sites across tests/session keep compiling: a mechanical
// rename of them does not belong inside a Windows-correctness change.
//
// ⚠️ REMOVAL CONDITION — this is a migration seam, NOT a compatibility layer,
// and it has a defined end:
//
//     DELETE THIS FILE once no file includes it — i.e. once the tests/session
//     call sites use `#include "support/temp_dir.hpp"` and
//     `fixpp::test_support::unique_temp_dir` / `remove_temp_dir` directly.
//     Re-derive who is left with:
//         grep -rl '_fixtures_/store_temp_dir.hpp' tests/
//
// Nothing new should include this header. `fixpp::store_test` itself stays — it
// is shared with store_factories.hpp and test_double_fsm.hpp and is NOT being
// retired; only these two names are moving out of it.
#pragma once

#include "support/temp_dir.hpp"

namespace fixpp::store_test {

// The old spellings, kept only for the call sites named in the removal condition
// above. `using` declarations cannot serve here: the names differ
// (unique_store_dir != unique_temp_dir), so these forward explicitly.
inline std::filesystem::path unique_store_dir(std::string_view tag) {
    return fixpp::test_support::unique_temp_dir(tag);
}
inline void remove_store_dir(const std::filesystem::path& p) {
    fixpp::test_support::remove_temp_dir(p);
}

}  // namespace fixpp::store_test
