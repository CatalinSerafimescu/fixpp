# tests/dictionary/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(dictionary_smoke_test dictionary_smoke_test.cpp)
target_link_libraries(dictionary_smoke_test PRIVATE
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME dictionary_smoke COMMAND dictionary_smoke_test)
