# tests/session/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(session_smoke_test session_smoke_test.cpp)
target_link_libraries(session_smoke_test PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME session_smoke COMMAND session_smoke_test)
