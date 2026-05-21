# tests/perf/CMakeLists.txt
# 008-message-store seam 14 — mallocnesia alloc-guard.
#
# Seam→file binding is one-to-one and glob-free (plan.md Test-seam map).
# CTest name prefix `perf_` selects the perf set:
#   ctest --test-dir build/<preset> -R '^perf_'
# Registrations are added red-first per TDD ([const §VII.3]) as each seam lands.
#
# The mallocnesia interceptor is at tools/mallocnesia/libmallocnesia.so
# (reference_mallocnesia_path). The harness LD_PRELOADs it at test runtime.
# fixpp_session transitively brings fixpp_core + asio.
# Uses fixpp_add_store_test from cmake/fixpp_store_test.cmake (included at root).
# NOTE: LD_PRELOAD wiring for mallocnesia is deferred to D-17 /gate-a re-touch;
#       this helper does NOT set LD_PRELOAD (see cmake/fixpp_store_test.cmake).
find_package(GTest CONFIG REQUIRED)

# ── 008-message-store seam registrations ────────────────────────────────────
# T050 — seam 14: MemoryStore alloc-guard (SC-007 / FR-027 / [const §VIII.5])
# Run with:
#   LD_PRELOAD=tools/mallocnesia/libmallocnesia.so \
#       build/linux-clang-debug/tests/perf/perf_store_alloc_guard \
#       --gtest_filter='*Mallocnesia*'
fixpp_add_store_test(perf_store_alloc_guard SOURCES test_store_alloc_guard.cpp)
# D-17 /gate-a re-touch: wire mallocnesia LD_PRELOAD so SC-007 actually fires.
# Uses APPEND so the TSAN_OPTIONS already set by fixpp_add_store_test is preserved.
# MALLOCNESIA_MAX_ALLOCS is intentionally unset → default g_max = 0 strict gate.
set_property(TEST perf_store_alloc_guard APPEND PROPERTY
    ENVIRONMENT "LD_PRELOAD=${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
