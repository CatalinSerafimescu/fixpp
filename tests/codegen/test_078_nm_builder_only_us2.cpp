// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_nm_builder_only_us2.cpp
//
// 078-precompiled-builder-libs T024 [US2] [TESTS-FIRST]: `nm` builder-only
// witness (SC-003; FR-005; builder<->validator cross-entity invariant 1).
// This TU `#include`s ONLY the slim per-message declaration header for one
// v44 message, calls `build_NewOrderSingle`, and links ONLY
// `fixpp::builders::v44` -- NOT `fixpp::validators::v44` -- proving a
// send-only consumer never pays for (or even pulls in) validator code.
//
// Foundational (T002-T019) already landed the real emitter split + the
// disjoint builder/validator libs, so this witness is BORN-GREEN (the
// property it asserts already holds) --
// feedback_fail_placeholder_red_test: no fake red-first cycle staged here.
// This is the REAL-lib counterpart to the T002 R2a probe's leg (ii), which
// proved the same property against a hand-written mock
// (test_078_odr_mock/); here it is proved against the actual generated
// fixpp::builders::v44 / fixpp::validators::v44 targets.
//
// Two discrimination legs, wired in tests/codegen/CMakeLists.txt:
//   (1) `nm -C --defined-only` (demangled) on the LINKED BINARY shows zero
//       `fixpp::v44::validate_`/`writer_traits` symbols -- not merely that
//       the archive itself is clean (already true of libfixpp_builders_v44.a
//       in isolation), but that nothing pulls a validator symbol in via the
//       final link. Uses the shared test_078_nm_assert.cmake with its
//       demangled default -- NOT the T002 probe's DEMANGLE=OFF invocation:
//       a bare (mangled, undemangled) "validate_" substring match
//       false-positives here --
//       fixpp::wire::body_builder carries a genuinely-named
//       validate_group_grammar() member (unrelated wire-grammar validation,
//       pulled in transitively by this binary's fixpp_wire dependency via
//       fixpp::builders::v44). The T002 mock binary never links fixpp_wire,
//       so this collision never surfaced there -- discovered empirically
//       when this witness was first run against the REAL lib.
//   (2) The target does not `target_link_libraries` against
//       `fixpp::validators::v44` at all -- verified structurally by the
//       CMake wiring below (no link dependency), and empirically by the
//       fact that this binary builds and links successfully even though
//       `validate_NewOrderSingle` is declared (but never called) in the
//       included slim header -- an unresolved reference to a symbol that IS
//       used would fail to link; here it is simply never referenced by
//       generated code, so the linker never needs the validator archive.
//
// Anchors: specs/078-precompiled-builder-libs/spec.md US2 (lines 64-67),
// SC-003 (line 166), FR-005; quickstart.md Scenario 2;
// contracts/include-layout.md invariant 1 (builder<->validator disjointness);
// tests/codegen/test_078_odr_sc003_probe.cpp (mock precedent, leg ii).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/v44/messages/NewOrderSingle.hpp>  // slim decl header -- NOT all.hpp
#include <span>

TEST(NmBuilderOnlyUS2, BuildNewOrderSingleViaBuilderOnlyLink) {
    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = "US2-1";
    args.symbol = "US2SYM";
    args.side = '1';

    std::array<std::byte, 256> out{};
    auto built = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_NewOrderSingle failed";
    EXPECT_GT(built->size(), 0u) << "build_NewOrderSingle produced an empty frame";
}
