# tests/fuzz/CMakeLists.txt — libFuzzer harness for decimal parse (seam #7)
# Built only when FIXPP_BUILD_FUZZ=ON.
# Clang-only: uses -fsanitize=fuzzer,address which GCC does not support.

add_executable(fuzz_decimal_parse fuzz_decimal_parse.cpp)
target_link_libraries(fuzz_decimal_parse PRIVATE fixpp_core)
target_compile_options(fuzz_decimal_parse PRIVATE -fsanitize=fuzzer,address)
target_link_options(fuzz_decimal_parse PRIVATE -fsanitize=fuzzer,address)
set_target_properties(fuzz_decimal_parse PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
