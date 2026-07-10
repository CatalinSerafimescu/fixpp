# tools/codegen/fixpp-codegen/CMakeLists.txt
# 003-dictionary-codegen — T003. C++23 build-only host executable
# ([const §III.5]; F1 Candidate A). Reuses the merged 002 fixpp::dictionary
# runtime as the single RUNTIME XML truth (no second QuickFIX-XML *dictionary*
# — research.md D-1): FieldRef/rule/group_no_tag/type all still come from
# fixpp::dictionary's XmlLoader via ir.cpp's build_ir(). NEVER linked into the
# user-facing library and NEVER installed; it runs at configure time via
# fixpp::dict::generate-vXX (T016).
#
# 067-codegen-writer-emitter T003/RC-B: fixpp-codegen ALSO links pugixml
# directly (PRIVATE, host-tool-only, never installed) for a SECOND,
# codegen-tool-local pugixml RE-PARSE of the same xml_path in ir.cpp
# (populating MessageIR.group_order — the declaration-order walk the
# tag-sorted/deduped runtime Dictionary cannot supply; research.md R9). This
# is NOT a second QuickFIX-XML *parser of runtime dictionary semantics* — it
# is a host-build-tool-only declaration-order re-walk of the same file, never
# reaching the shipped runtime surface. fixpp_dictionary's own pugixml link is
# PRIVATE and does NOT propagate transitively, so this tool needs its own.
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
  emit_builders.cpp
)

target_include_directories(fixpp-codegen PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependency: the 002 runtime dictionary (host-side build edge,
# [const §III.5] — not a module link edge of the user-facing library).
target_link_libraries(fixpp-codegen PRIVATE fixpp::dictionary)

# 067 T003/RC-B: pugixml build home for ir.cpp's codegen-tool-local re-parse
# (MessageIR.group_order). No version pin — inherit the project-resolved
# pugixml (fixpp_dictionary already depends on it project-wide, see
# src/dictionary/CMakeLists.txt:9). PRIVATE, build-only host-tool dep, never
# installed, zero ABI surface.
find_package(pugixml CONFIG REQUIRED)
target_link_libraries(fixpp-codegen PRIVATE pugixml::pugixml)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(fixpp-codegen PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Build-only: explicitly excluded from install() (the top-level install()
# ships include/ only; this target is never an install component).
