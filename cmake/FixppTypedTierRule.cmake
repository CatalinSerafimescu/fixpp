# cmake/FixppTypedTierRule.cmake — #613 (`.specify/613-builders-export.md` D-0, §4).
#
# The TESTS' copy of the typed-tier rule, shared by the two `cmake -P` witnesses that
# check it: tests/consumer/run_consumer_witness.cmake and
# tests/packaging/run_package_contents_witness.cmake.
#
# ⚠️ The root CMakeLists.txt must NEVER include this file. §4's guards are worth
# something only because the witnesses re-derive the default from a copy of the rule the
# root's own resolution does not share; a root that included this module would make a
# defaulting bug move both sides at once.
#
# Usable from `cmake -P` (no project(), no policy version set): no IN_LIST, and every
# value is compared quoted.

# Guard 0 — both inputs in domain. The raw setting, uppercased, is AUTO or one of D-0's
# boolean spellings; the effective value is exactly ON or OFF. Empty is out of domain on
# both (`STREQUAL ""`, not `NOT DEFINED`: `-DX=` defines X as empty). Each hint is the
# caller's sentence for what an EMPTY value of that input means.
function(fixpp_typed_tier_check_domain setting effective setting_hint effective_hint)
  string(TOUPPER "${setting}" _uc)
  if("${setting}" STREQUAL "" OR NOT _uc MATCHES "^(AUTO|ON|YES|TRUE|Y|1|OFF|NO|FALSE|N|0)$")
    message(FATAL_ERROR
      "#613 guard 0: FIXPP_TYPED_TIER_SETTING='${setting}' is not AUTO or a CMake "
      "boolean spelling. ${setting_hint}")
  endif()
  if(NOT "${effective}" STREQUAL "ON" AND NOT "${effective}" STREQUAL "OFF")
    message(FATAL_ERROR
      "#613 guard 0: FIXPP_TYPED_TIER_EFFECTIVE='${effective}' is not exactly ON or OFF. "
      "${effective_hint}")
  endif()
endfunction()

# Guard 1's re-derivation, written from the rule, not read from the root's code. With
# the setting AUTO: a case-insensitive Release build type is ON, every other build type
# OFF, and `${out_from}` is `default_label` (the caller's words for where the build type
# came from). An explicit setting is normalised (ON|YES|TRUE|Y|1 ⇒ ON, else OFF) and
# `${out_from}` names it. Call after fixpp_typed_tier_check_domain().
function(fixpp_typed_tier_rederive setting build_type default_label out out_from)
  string(TOUPPER "${setting}" _uc)
  string(TOUPPER "${build_type}" _bt)
  if(_uc STREQUAL "AUTO")
    if(_bt STREQUAL "RELEASE")
      set(_v ON)
    else()
      set(_v OFF)
    endif()
    set(_from "${default_label}")
  elseif(_uc MATCHES "^(ON|YES|TRUE|Y|1)$")
    set(_v ON)
    set(_from "the explicit setting '${setting}'")
  else()
    set(_v OFF)
    set(_from "the explicit setting '${setting}'")
  endif()
  set(${out} "${_v}" PARENT_SCOPE)
  set(${out_from} "${_from}" PARENT_SCOPE)
endfunction()

# A version directory's expected immediate children, sorted. Written from 078
# contracts/include-layout.md's file set, not from the install rule: the five read-side
# files, plus, where the typed tier is PRESENT, the 078 split layout.
function(fixpp_typed_tier_expected_children present out)
  set(_c Fields.hpp Messages.hpp NormativeReferences.md Reify.hpp Validator.hpp)
  if(present)
    list(APPEND _c all.hpp groups.hpp groups messages validators)
  endif()
  list(SORT _c)
  set(${out} "${_c}" PARENT_SCOPE)
endfunction()
