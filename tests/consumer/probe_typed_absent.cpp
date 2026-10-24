// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_typed_absent.cpp — #613 C-3 (`.specify/613-builders-export.md` §4.1).
//
// The ABSENT branch's ❌ cell: where the typed tier is not in effect (by default every
// Debug tree), no slim typed header may resolve through fixpp::fixpp. 086's ❌-cell form
// (rule 2 in tests/consumer/CMakeLists.txt's 086 block): `__has_include` plus a
// unique-token `#error`, asserted by COMPILING.
//
// Per version it asserts absence for, a POSITIVE CONTROL first: the version's read-side
// Messages.hpp must resolve. Without it, a version tree that was not staged at all would
// make that version's absences true for the wrong reason.
//
// COMPILE-ONLY (OBJECT library, no main).

#ifndef FIXPP_613_HAS_VLATEST
#error "FIXPP_613_HAS_VLATEST must be defined (0 or 1) by tests/consumer/CMakeLists.txt"
#endif

#if !__has_include(<fixpp/v42/Messages.hpp>)
#error "FIXPP_613_POSITIVE_CONTROL_MISSING: fixpp/v42/Messages.hpp"
#endif
#if !__has_include(<fixpp/v44/Messages.hpp>)
#error "FIXPP_613_POSITIVE_CONTROL_MISSING: fixpp/v44/Messages.hpp"
#endif
#if !__has_include(<fixpp/v50sp2/Messages.hpp>)
#error "FIXPP_613_POSITIVE_CONTROL_MISSING: fixpp/v50sp2/Messages.hpp"
#endif
#if FIXPP_613_HAS_VLATEST && !__has_include(<fixpp/vlatest/Messages.hpp>)
#error "FIXPP_613_POSITIVE_CONTROL_MISSING: fixpp/vlatest/Messages.hpp"
#endif

#if __has_include(<fixpp/v42/all.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v42/all.hpp"
#endif
#if __has_include(<fixpp/v42/groups.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v42/groups.hpp"
#endif
#if __has_include(<fixpp/v42/validators/traits.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v42/validators/traits.hpp"
#endif
#if __has_include(<fixpp/v42/messages/NewOrderSingle.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v42/messages/NewOrderSingle.hpp"
#endif

#if __has_include(<fixpp/v44/all.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v44/all.hpp"
#endif
#if __has_include(<fixpp/v44/groups.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v44/groups.hpp"
#endif
#if __has_include(<fixpp/v44/validators/traits.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v44/validators/traits.hpp"
#endif
#if __has_include(<fixpp/v44/messages/NewOrderSingle.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v44/messages/NewOrderSingle.hpp"
#endif

#if __has_include(<fixpp/v50sp2/all.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v50sp2/all.hpp"
#endif
#if __has_include(<fixpp/v50sp2/groups.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v50sp2/groups.hpp"
#endif
#if __has_include(<fixpp/v50sp2/validators/traits.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v50sp2/validators/traits.hpp"
#endif
#if __has_include(<fixpp/v50sp2/messages/ListStatus.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/v50sp2/messages/ListStatus.hpp"
#endif

#if FIXPP_613_HAS_VLATEST
#if __has_include(<fixpp/vlatest/all.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/vlatest/all.hpp"
#endif
#if __has_include(<fixpp/vlatest/groups.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/vlatest/groups.hpp"
#endif
#if __has_include(<fixpp/vlatest/validators/traits.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/vlatest/validators/traits.hpp"
#endif
#if __has_include(<fixpp/vlatest/messages/ListStatus.hpp>)
#error "FIXPP_613_TYPED_HEADER_REACHABLE: fixpp/vlatest/messages/ListStatus.hpp"
#endif
#endif

int fixpp_613_typed_absent_probe();
int fixpp_613_typed_absent_probe() { return 0; }
