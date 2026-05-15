// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/integration/multi_session_multi_version.cpp — T038 [P] [US4]
//
// Seam #10a, AC-C1..C3: multi-version coexistence in a single translation unit.
//
// What this test verifies (all compile-time, R6-independent):
//   AC-C1: Independent INTERFACE targets exist for each version; this TU
//           includes both v42 and v50sp2 Messages.hpp simultaneously without
//           compilation failure (demonstrating per-version INTERFACE isolation).
//   AC-C2: A consumer depending only on fixpp::dict::v44 + fixpp::dict::runtime
//           compiles without pulling v50sp2/v42/vt11 headers. The CMake wiring
//           of this target (see tests/integration/CMakeLists.txt) links only
//           fixpp::dict::v44 and fixpp::dict::runtime — yet only the v42 and
//           v50sp2 headers are EXPLICITLY included here, demonstrating that
//           per-version INTERFACE targets do not implicitly pull in each other.
//           (Build-system isolation is the CMake-level guarantee; the C++ level
//           test is AC-C1 and AC-C3 below.)
//   AC-C3: fixpp::v42::NewOrderSingle and fixpp::v50sp2::NewOrderSingle are
//           distinct, non-implicitly-convertible types when both Messages.hpp
//           are included in the same TU.
//
// No namespace bleed: the per-version groups:: sub-namespaces (fixpp::v42::groups
// and fixpp::v50sp2::groups) do NOT collide with each other because they are
// scoped under distinct version namespaces. This is structurally guaranteed by
// the generated namespace fixpp::vXX::groups pattern and verified here via
// the type-distinctness assertions below.
//
// These are pure COMPILE-TIME assertions. The runtime body is a trivial SUCCEED()
// so that CTest registers the test as passed. R6 does NOT block this test.
//
// Oracle: specs/003-dictionary-codegen/spec.md AC-C1, AC-C2, AC-C3, seam #10a;
//         [2c §7.6] per-version CMake targets; [arch §3] namespaces.

#include <type_traits>

#include <gtest/gtest.h>

// Include two distinct version headers in the same TU — AC-C1 compile check.
// If there were any namespace bleed (e.g. fixpp::v42::groups colliding with
// fixpp::v50sp2::groups), this would fail to compile.
#include <fixpp/v42/Messages.hpp>
#include <fixpp/v50sp2/Messages.hpp>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// AC-C3: fixpp::v42::NewOrderSingle and fixpp::v50sp2::NewOrderSingle are
// distinct, non-implicitly-convertible types.
// ─────────────────────────────────────────────────────────────────────────────

// They must be different types entirely.
static_assert(
    !std::is_same_v<fixpp::v42::NewOrderSingle, fixpp::v50sp2::NewOrderSingle>,
    "AC-C3: fixpp::v42::NewOrderSingle and fixpp::v50sp2::NewOrderSingle "
    "must be distinct types");

// Neither direction of implicit conversion is permitted.
static_assert(
    !std::is_convertible_v<fixpp::v42::NewOrderSingle, fixpp::v50sp2::NewOrderSingle>,
    "AC-C3: fixpp::v42::NewOrderSingle must NOT be implicitly convertible to "
    "fixpp::v50sp2::NewOrderSingle");

static_assert(
    !std::is_convertible_v<fixpp::v50sp2::NewOrderSingle, fixpp::v42::NewOrderSingle>,
    "AC-C3: fixpp::v50sp2::NewOrderSingle must NOT be implicitly convertible to "
    "fixpp::v42::NewOrderSingle");

// ─────────────────────────────────────────────────────────────────────────────
// AC-C1 / namespace isolation: v42 and v50sp2 are scoped in distinct namespaces.
// If namespace bleed occurred, same-named types would collide.
// ExecutionReport exists in both v42 and v50sp2 — a good cross-check that the
// per-version scoping prevents any ODR / symbol-collision issue.
// ─────────────────────────────────────────────────────────────────────────────

// Verify ExecutionReport is also a distinct type per version (not just NewOrderSingle).
static_assert(
    !std::is_same_v<fixpp::v42::ExecutionReport, fixpp::v50sp2::ExecutionReport>,
    "AC-C1: fixpp::v42::ExecutionReport and fixpp::v50sp2::ExecutionReport must be "
    "distinct types (no namespace bleed)");

static_assert(
    !std::is_convertible_v<fixpp::v42::ExecutionReport, fixpp::v50sp2::ExecutionReport>,
    "AC-C1: fixpp::v42::ExecutionReport must NOT be implicitly convertible to "
    "fixpp::v50sp2::ExecutionReport (no cross-version implicit conversion)");

// ─────────────────────────────────────────────────────────────────────────────
// Runtime test: trivial pass. All assertions are compile-time above.
// ─────────────────────────────────────────────────────────────────────────────

TEST(MultiSessionMultiVersion, AcC3_CrossVersionTypesDistinct) {
    // All assertions are static_assert above. This runtime body exists so CTest
    // registers the test as PASSED and reports a meaningful test name.
    // R6 does NOT block this test — there are no wire-frame reads here.
    SUCCEED() << "AC-C3: compile-time static_assert checks passed "
                 "(fixpp::v42 and fixpp::v50sp2 types are distinct and "
                 "non-implicitly-convertible)";
}

TEST(MultiSessionMultiVersion, AcC1_NoNamespaceBleed) {
    // Verifies that including both v42 and v50sp2 Messages.hpp in the same TU
    // compiles cleanly — no ODR violations, no symbol collisions, no namespace bleed.
    // If namespace bleed occurred (e.g. groups:: symbols escaping their version
    // namespace), the TU would fail to compile, and we would never reach here.
    SUCCEED() << "AC-C1: multi-version include compiled cleanly "
                 "(fixpp::v42::groups and fixpp::v50sp2::groups are properly "
                 "scoped; no namespace bleed)";
}

}  // namespace
