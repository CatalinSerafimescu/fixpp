// Build-flag probe for #481: the "#481" block in tests/core/CMakeLists.txt
// registers the ctest entries that build it. The function does not read its
// parameter on purpose: -Wextra's unused-parameter check is what each entry
// looks for in the build output.
// NOLINTNEXTLINE(misc-unused-parameters,clang-diagnostic-unused-parameter)
int fixpp_481_probe(int fixpp_481_probe_param) { return 0; }
