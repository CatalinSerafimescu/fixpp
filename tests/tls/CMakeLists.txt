# tests/tls/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(tls_smoke_test tls_smoke_test.cpp)
target_link_libraries(tls_smoke_test PRIVATE
  fixpp_tls
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME tls_smoke COMMAND tls_smoke_test)
