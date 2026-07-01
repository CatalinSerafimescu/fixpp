// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/reify_dispatch_bridge.hpp
//
// 057-behavioral-reify-unblock — PRIVATE same-module declaring header (contract
// C-1). NOT a shipped/public include/ header: it only DECLARES the two dispatch
// bridge free functions that shipped reify.cpp calls at its placeholder sites.
// The two functions are DEFINED exactly once, in the build-tree-generated TU
// ${CMAKE_BINARY_DIR}/_codegen/reify_dispatch_bridge.cpp (configure_file'd from
// cmake/templates/reify_dispatch_bridge.cpp.in) — the SOLE translation unit that
// #includes the build-tree _dispatch/*.hpp headers, and, being outside src/**,
// the sole TU outside the check_layers.py scan by design (NFR-003-8).
//
// LAYER-HYGIENE (contract C-1 / research D-7): this header LIVES under src/**,
// so check_layers.py DOES scan it (module=dictionary, ALLOWED={core}), and the
// scan is direct-include-only. It MUST therefore #include ONLY
// <fixpp/dict/reify.hpp> (a dict->dict self-include, allowed) + the two std
// headers below. Every wire/dict type its two declarations name
// (owning_message_handle, wire::MessageView<Index>, version_profile,
// application_version, core::expected_t) arrives TRANSITIVELY through reify.hpp
// (which lives in include/, a dir the scanner does not scan). A DIRECT
// <fixpp/wire/...> or build-tree _dispatch/vXX include here would be a
// dictionary -> wire violation -> check_layers.py RED. DO NOT add one.
#pragma once
#include <fixpp/dict/reify.hpp>
#include <memory_resource>
#include <string_view>

namespace fixpp::dict {

// Delegates (in the generated-aware bridge TU only) to dispatch::dispatch_fixt.
[[nodiscard]] core::expected_t<owning_message_handle> reify_dispatch_fixt(
    wire::MessageView<wire::access_mode::Index> const& view, char msg_type, version_profile profile,
    std::pmr::memory_resource* mr) noexcept;

// Delegates (in the generated-aware bridge TU only) to dispatch::dispatch_application.
[[nodiscard]] core::expected_t<owning_message_handle> reify_dispatch_application(
    wire::MessageView<wire::access_mode::Index> const& view, std::string_view msg_type,
    application_version appver, version_profile profile, std::pmr::memory_resource* mr) noexcept;

}  // namespace fixpp::dict
