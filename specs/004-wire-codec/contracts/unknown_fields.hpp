// SPDX-License-Identifier: AGPL-3.0-or-later
// CONTRACT SHAPE ORACLE — extract of [2b §4.8]. Authority: 2b-wire.md v0.2.
// Yields ONLY dictionary-MISSING tags. Dictionary-known-but-invalid-for-MsgType
// tags are NOT here — they raise wire_unexpected_tag (validator rule 5).
// No vector materialization; round-trip preserves original byte order.
#pragma once
#include "view.hpp"

namespace fixpp::wire {

class unknown_fields_view : public View {
public:
    class iterator;   // forward iterator yielding (tag, value) pairs
    [[nodiscard]] iterator begin() const noexcept [[clang::lifetimebound]];
    [[nodiscard]] iterator end()   const noexcept [[clang::lifetimebound]];
    [[nodiscard]] bool     empty() const noexcept;
};

}  // namespace fixpp::wire
