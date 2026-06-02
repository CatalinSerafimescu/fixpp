find_package(GTest CONFIG REQUIRED)

# ── Slice 3a: layout smoke test ───────────────────────────────────────────
add_executable(log_smoke_test log_smoke_test.cpp)
target_link_libraries(log_smoke_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_smoke COMMAND log_smoke_test)

# ── Slice 3b-i: T015 (TS-2) — overflow drop_newest ───────────────────────
add_executable(log_overflow_test test_overflow_drop_newest.cpp)
target_link_libraries(log_overflow_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_overflow COMMAND log_overflow_test)

# ── Slice 3b-i: T019 (TS-8) — level + category filter ────────────────────
add_executable(log_filter_test test_level_and_category_filter.cpp)
target_link_libraries(log_filter_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_filter COMMAND log_filter_test)
