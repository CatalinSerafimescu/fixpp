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
