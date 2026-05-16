// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/reify.cpp
//
// 003-dictionary-codegen — T037. Out-of-line bodies for dict::reify +
// owning_message_handle ([2c §4.8] / data-model Entity 5/6). Bridge file
// (arch §2.4 v0.3, RC#3): #includes the vendored frozen wire contract stub
// so it compiles against wire::MessageView — NOT a cyclic dictionary→wire
// module link edge; check_layers.py BRIDGE_SOURCE_FILES exemption applies.
//
// Layering design (NFR-003-8 / contracts/reify_dispatch.hpp / spec §9 seam
// #15a/#15b):
//   * dict::reify() is declared in the shipped include/fixpp/dict/reify.hpp.
//   * Its dispatch logic calls dispatch::dispatch_fixt() and
//     dispatch::dispatch_application() which are defined in the BUILD-TREE
//     generated _dispatch/ headers (included by the dispatch-consuming TU,
//     NOT by the shipped header — that would create a shipped→build-tree
//     layering violation breaking check_layers.py).
//   * For R6 scope: dict::reify() resolves the version axis (MsgType peek
//     via get<35>(), resolve_application_version) and returns appropriate
//     errors. The dispatch to owning_message_handle (the full typed return)
//     is R6-blocked pending the real wire body (2b) and the dispatch TU
//     wiring (T037 complete wiring is in the test-consumer TU via the
//     generated _dispatch/ headers — see tests/dictionary/reify_dispatch_test.cpp
//     and tests/integration/fixt_cross_vocabulary.cpp).
//
//   * IMPORTANT: the dispatch-consuming TU (test or session-FSM) includes
//     both _dispatch/ headers AND calls dispatch::dispatch_fixt /
//     dispatch::dispatch_application directly. dict::reify() is the
//     high-level façade that can be wired to those helpers by a consumer
//     that includes the dispatch headers. The inline helpers in the dispatch
//     headers provide the switch bodies; dict::reify() coordinates them.
//     Since src/dictionary/reify.cpp cannot #include the build-tree generated
//     headers, dict::reify() in this TU implements the resolution algorithm
//     only and returns R6-scoped stubs for the owning_message_handle result.
//
// R6 scope: every get<>() on the frozen stub returns dict_xml_parse_failed
//   (frozen-stub internal impl detail). dict::reify() normalizes that to the
//   reify-domain error dict_reify_wire_body_not_ready (slot 29) before returning:
//   dict::reify():
//     - get<35>() → dict_xml_parse_failed (stub internal) → normalized to
//       dict_reify_wire_body_not_ready on the reachable exit (gate-b/r2 RC#2 F1).
//       dict_xml_parse_failed is reserved for genuine 002 XML-loader parse
//       failures; it must not surface from the reify public API.
//     - Full behavioural dispatch (FIXT-admin match → vt11 owner;
//       application match → vXX owner) is R6-blocked until 2b replaces the
//       wire stub body with real frame bytes.
//   Tests that exercise the resolution algorithm (resolve_application_version,
//   FIXT-admin char detection, error propagation) use the dispatch helpers
//   and resolve_application_version DIRECTLY (not via dict::reify()) since
//   dict::reify() is MsgType-read-blocked (AC-D1..D7 — see T034/T035 test
//   files for the R6-scoped test strategy and 2b-unblock notes).
//
// owning_message_handle: a minimal R6 stub. The full impl (SBO polymorphic
// variant, as<Msg>() type-checked cast, move semantics with cache reset) is
// deferred; the compile-time shape (move-only, method signatures) is what
// T034 tests (AC-R6 is shape-only per reify_test.cpp precedent).
#include <cstdint>
#include <expected>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <memory_resource>
#include <string_view>

namespace fixpp::dict {

// ─── FIXT admin detection ────────────────────────────────────────────────────
// The 7 FIXT.1.1 admin MsgTypes per [FIXT §3] / [2c §6.3] / contracts/
// reify_dispatch.hpp. Hardcoded here (not from the dispatch table) so
// src/dictionary/reify.cpp can implement the resolution algorithm without
// including the build-tree generated dispatch headers (AC-D2 / seam #15a).
namespace {
[[nodiscard]] constexpr bool is_fixt_admin(char mt) noexcept {
    // '0'=Heartbeat '1'=TestRequest '2'=ResendRequest '3'=Reject
    // '4'=SequenceReset '5'=Logout 'A'=Logon
    return mt == '0' || mt == '1' || mt == '2' || mt == '3' || mt == '4' || mt == '5' || mt == 'A';
}
}  // namespace

// ─── owning_message_handle implementation (R6 stub) ──────────────────────────
// R6: the frozen wire stub carries no frame state; owning_message_handle
// cannot hold a real typed owner without the dispatch TU including the
// build-tree generated _dispatch/ and Reify.hpp headers. The struct impl
// is a minimal stub that satisfies the ABI surface the shipped header declares.
// The full polymorphic type-erased implementation (SBO, as<Msg>() checked
// cast) lands when 2b swaps in the real wire body and T040 wires the dispatch
// CMake target. Behavioural assertions (handle.version() / handle.msg_type()
// / handle.as<NOS>()) are R6-blocked per T034 test-comment rationale.
struct owning_message_handle::impl {
    resolved_message_version version{.k = resolved_message_version::kind::session_admin,
                                     .session = session_version::Unknown,
                                     .application = application_version::Unknown,
                                     ._reserved = 0};
    // R6: no typed owner stored — dispatch TU wiring deferred (T037 / 2b).
};

owning_message_handle::owning_message_handle(owning_message_handle&& other) noexcept
    : pimpl_(other.pimpl_) {
    other.pimpl_ = nullptr;
}

owning_message_handle& owning_message_handle::operator=(owning_message_handle&& other) noexcept {
    if (this != &other) {
        delete pimpl_;
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    return *this;
}

owning_message_handle::~owning_message_handle() { delete pimpl_; }

resolved_message_version owning_message_handle::version() const noexcept {
    if (pimpl_ == nullptr) {
        return {.k = resolved_message_version::kind::session_admin,
                .session = session_version::Unknown,
                .application = application_version::Unknown,
                ._reserved = 0};
    }
    return pimpl_->version;
}

std::string_view owning_message_handle::msg_type() const noexcept {
    // R6: no frame bytes → empty. Full impl with 2b.
    return {};
}

wire::MessageView<wire::access_mode::Index> const& owning_message_handle::view() const noexcept {
    // R6: no real frame; return a static default-constructed stub MV.
    static wire::MessageView<wire::access_mode::Index> const kEmpty{};
    return kEmpty;
}

core::expected_t<wire::field_view> owning_message_handle::field_value(
    std::uint16_t tag) const noexcept {
    return view().get(tag);
}

// ─── dict::reify ─────────────────────────────────────────────────────────────
// Runtime-dispatch entry point (AC-D1..D7 / [2c §4.8] / seam #15a/#15b/#15c).
//
// Resolution algorithm ([2c §4.3:465-474] + [2c §6.3]):
//  1. peek MsgType(35) via view.template get<35>().
//  2. FIXT-admin hit (single-char '0'-'5'/'A') → resolve {session_admin,
//     profile.session, Unknown}; dispatch via dispatch_fixt.
//  3. miss → read ApplVerID(1128) via view.template get<1128>(); the 2b/wire
//     field-absent error maps to empty sv (cross-feature note: NOT a 003 slot
//     — spec A6 / version_profile.hpp). Call resolve_application_version.
//     Dispatch via dispatch_application.
//
// R6-blocked: get<35>() returns dict_xml_parse_failed (frozen stub). The
// algorithm body is present and correct for 2b; for R6 the MsgType read
// always fails and we propagate the stub error. Tests exercise
// resolve_application_version + is_fixt_admin + the dispatch switch shapes
// directly (not via dict::reify()) — see reify_dispatch_test.cpp for the
// R6-scoped test strategy and the 2b-unblock notes per AC.
//
// Dispatch integration: the dispatch-consuming TU (tests, session FSM) includes
// _dispatch/reify_dispatch_fixt.hpp + _dispatch/reify_dispatch_application.hpp
// which define dispatch::dispatch_fixt() and dispatch::dispatch_application()
// as INLINE functions. dict::reify() could call them IF this .cpp included the
// generated headers — but it cannot (shipped src, not build-tree). Instead,
// the dispatch-consuming TU can call dispatch:: helpers directly. For the
// full wiring (where dict::reify ALSO dispatches), the CMake fixpp::dict::dispatch
// target is the intended consumer that links both this .cpp and the dispatch TU.
// In R6 scope the reify stub returns an error to keep tests clean.
[[nodiscard]] core::expected_t<owning_message_handle> reify(
    wire::MessageView<wire::access_mode::Index> const& view, version_profile profile,
    std::pmr::memory_resource* mr) noexcept {
    (void)mr;

    // Step 1: peek MsgType(35) (AC-D2 / [2c §4.8]).
    // R6: the frozen stub returns dict_xml_parse_failed for every get<>().
    // In R6 scope we cannot determine the MsgType from the frozen stub, so
    // the dispatch path falls through to the error below.
    // 2b-unblock: when the real wire body lands, get<35>() returns a
    // field_view whose as_string() yields the one- or two-char MsgType.
    // The resolution logic below is architecturally correct; only the stub
    // body prevents it from exercising the actual dispatch.
    auto mt_fv = view.template get<35>();
    if (!mt_fv) {
        // R6: field-absent from frozen stub → cannot dispatch.
        // gate-b/r2 RC#2 F1: normalize to dict_reify_wire_body_not_ready — the
        // correct reify-domain error for the R6 "wire body not yet available"
        // condition. dict_xml_parse_failed is reserved strictly for genuine 002
        // XML-loader parse failures and must not surface from the reify API.
        // All three non-success exits from dict::reify() now consistently use
        // dict_reify_wire_body_not_ready (slot 29), matching the two unreachable
        // branches at the FIXT-admin and application-dispatch stubs below.
        // 2b-unblock: real frame bytes → this path is not taken.
        return std::unexpected{core::error::dict_reify_wire_body_not_ready};
    }
    std::string_view const mt_sv = mt_fv->as_string();

    // Step 2: FIXT-admin detection (AC-D2 / seam #15a).
    if (mt_sv.size() == 1 && is_fixt_admin(mt_sv.front())) {
        // FIXT admin hit → resolve {session_admin, profile.session, Unknown}.
        // gate-b/r1 RC#2: this stub must call dispatch::dispatch_fixt(view,
        // mt_sv.front(), profile, mr) which is defined in the build-tree-generated
        // _dispatch/reify_dispatch_fixt.hpp. This .cpp cannot #include that header
        // (shipped source must not depend on build-tree headers — arch §2.4 v0.3 /
        // NFR-003-8). The full wiring lands when 2b introduces the
        // fixpp::dict::dispatch CMake target (a generated-aware TU that includes
        // the _dispatch/ headers and to which dict::reify delegates via a
        // non-generated bridge header — 2b-unblock).
        // R6: this branch is unreachable (get<35>() always errors above).
        // Return the correct R6 placeholder — NOT dict_xml_parse_failed (wrong
        // layer/module — RC#2 finding F3 correction).
        (void)profile;
        return std::unexpected{core::error::dict_reify_wire_body_not_ready};
    }

    // Step 3: application dispatch (AC-D3 / seam #15b).
    // Read ApplVerID(1128); wire field-absent → empty sv → profile default.
    std::string_view appl_ver_sv;
    auto appl_fv = view.template get<1128>();
    if (appl_fv) {
        appl_ver_sv = appl_fv->as_string();
    }
    // else: field absent (wire-owned error, NOT a 003 slot) → use default.

    auto app_ver = resolve_application_version(profile, appl_ver_sv);
    if (!app_ver) {
        // AC-D6: dict_unresolved_application_version propagates.
        // AC-D7: dict_unknown_appl_ver_id propagates.
        return std::unexpected{app_ver.error()};
    }

    // gate-b/r1 RC#2: this stub must call dispatch::dispatch_application(view,
    // mt_sv, *app_ver, profile, mr). Same constraint as above — build-tree headers
    // not includable here. 2b-unblock: dispatch bridge TU + CMake target wiring.
    // R6: this branch is also unreachable (get<35>() always errors above).
    // Return the correct R6 placeholder — NOT dict_xml_parse_failed (RC#2 fix).
    (void)app_ver;
    return std::unexpected{core::error::dict_reify_wire_body_not_ready};
}

}  // namespace fixpp::dict
