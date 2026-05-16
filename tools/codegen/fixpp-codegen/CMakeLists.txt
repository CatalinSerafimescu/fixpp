# tools/codegen/fixpp-codegen/CMakeLists.txt
# 003-dictionary-codegen — T003. C++23 build-only host executable
# ([const §III.5]; F1 Candidate A). Reuses the merged 002 fixpp::dictionary
# runtime as the single XML truth (no second QuickFIX-XML parser —
# research.md D-1). NEVER linked into the user-facing library and NEVER
# installed; it runs at configure time via fixpp::dict::generate-vXX (T016).
#
# Exceptions are ENABLED here (XmlLoader::load throws on bad XML — [arch §5.3]);
# cmake/Helpers.cmake's fixpp_apply_common_flags (-fno-exceptions) is
# deliberately NOT applied to this host tool.

add_executable(fixpp-codegen
  main.cpp
  ir.cpp
  emit_fields.cpp
  emit_messages.cpp
  emit_validator.cpp
  emit_reify.cpp
  emit_normative_refs.cpp
  emit_dispatch.cpp
)

target_include_directories(fixpp-codegen PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependency: the 002 runtime dictionary (host-side build edge,
# [const §III.5] — not a module link edge of the user-facing library).
target_link_libraries(fixpp-codegen PRIVATE fixpp::dictionary)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(fixpp-codegen PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Build-only: explicitly excluded from install() (the top-level install()
# ships include/ only; this target is never an install component).
