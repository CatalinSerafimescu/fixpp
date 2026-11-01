#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/dictionary_test_access.hpp — TEST-ONLY read access to a loaded
// Dictionary's metadata handle (098-dialect-overlay, research R-10 §2 Correction).
//
// `Dictionary` declares `friend struct dictionary_test_access;` unconditionally.
// Define this struct only in this header; a differing definition of this struct
// elsewhere in the program is an ODR violation (ill-formed, no diagnostic required).
// Add new accessors here, never as members of `Dictionary` gated behind a test macro.
// Read accessors only. Never installed; production targets must not include this
// header. Needs no test macro. Its includer needs `src/dictionary` on the include
// path, for the private `dictionary_internal.hpp`.

#include <fixpp/dict/dictionary.hpp>

#include "dictionary_internal.hpp"

namespace fixpp::dict {

struct dictionary_test_access {
    // The handle a loader built, or nullptr for a moved-from Dictionary.
    [[nodiscard]] static detail::dict_metadata_handle const* handle(Dictionary const& d) noexcept {
        return d.handle_.get();
    }
};

}  // namespace fixpp::dict
