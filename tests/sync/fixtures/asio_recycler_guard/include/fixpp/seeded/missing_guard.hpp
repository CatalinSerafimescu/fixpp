// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/fixtures/asio_recycler_guard/include/fixpp/seeded/missing_guard.hpp
//
// fixpp#544 (B35): the seeded positive of the ctest asio_recycler_guard_inclusion_seeded.
// It includes an asio header and not the guard header, so the inclusion check must report
// it. Scanned as text only; nothing compiles it.
#pragma once

#include <asio/awaitable.hpp>
