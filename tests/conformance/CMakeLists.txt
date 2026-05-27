# tests/conformance/CMakeLists.txt
# 008-message-store seam 17 (raw-frame round-trip via tests/conformance/corpus/).
# Future homes: 005-session-establishment-fsm FIX-TC TC-001..TC-017 transitions
# (per [const §VII.5]; research D-5 partition — FSM-half is 005's discharge).
#
# Seam→file binding is one-to-one and glob-free (plan.md Test-seam map).
# CTest name prefix `store_` selects the store-side set:
#   ctest --test-dir build/<preset> -R '^store_'
# Registrations are added red-first per TDD ([const §VII.3]) as each seam lands.
# fixpp_session transitively brings fixpp_core + asio.
# Uses fixpp_add_store_test from cmake/fixpp_store_test.cmake (included at root).
find_package(GTest CONFIG REQUIRED)

# ── 008-message-store seam registrations ────────────────────────────────────
# T019 — seam 17: raw-frame round-trip conformance corpus replay (SC-010 / research D-13)
fixpp_add_store_test(store_corpus_replay SOURCES
  test_store_corpus_replay.cpp
  "${CMAKE_SOURCE_DIR}/tests/session/_fixtures_/test_double_fsm.cpp"
)

# ── 011-tls-policy seam registrations ────────────────────────────────────────
# T020 — seam #3: FIXS-RC1 §5 recorded rotation cross-handshake atomicity.
# T020a (mock_transport.hpp) is header-only; consumed directly by T020 sources.
add_executable(tls_fixs_rotation
  test_fixs_rotation.cpp
)
target_include_directories(tls_fixs_rotation PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"  # for mock_transport.hpp
)
target_link_libraries(tls_fixs_rotation PRIVATE
  fixpp::tls
  GTest::gtest
  GTest::gtest_main
)
set_target_properties(tls_fixs_rotation PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME tls_fixs_rotation COMMAND tls_fixs_rotation)
set_tests_properties(tls_fixs_rotation PROPERTIES LABELS "tls;011;conformance")

# ── Phase 3a tests (wired by T029): ──────────────────────────────────────────

# T020 — [2h §9 seam #1] — transport interop conformance corpus TC-001..TC-017.
# All TC-001..TC-017 cells are DISABLED_ pending the QuickFIX test peer binary
# (Phase 8 / post-012 interop run per [[project_release_interop_quickfix_fix8]]).
# The error-code scaffolding cell (TransportInteropScaffolding) runs Phase 3a.
add_executable(transport_interop_conformance
  test_transport_interop.cpp)
target_link_libraries(transport_interop_conformance PRIVATE
  fixpp_transport  fixpp_tls  fixpp_wire
  GTest::gtest  GTest::gtest_main)
add_test(NAME transport_interop_conformance
         COMMAND transport_interop_conformance)
set_tests_properties(transport_interop_conformance PROPERTIES
  LABELS "transport;012;us1;seam1;conformance")
