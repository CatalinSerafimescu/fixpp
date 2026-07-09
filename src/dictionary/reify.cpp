// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/reify.cpp
//
// 003-dictionary-codegen (T037) + 057-behavioral-reify-unblock. Out-of-line
// bodies for dict::reify + owning_message_handle ([2c §4.8] / data-model
// Entity 5/6). Bridge file (arch §2.4 v0.3): #includes the wire contract
// re-export (message_view_contract.hpp, post-004-T028 the REAL parser.hpp
// surface) so it compiles against wire::MessageView / Framer — NOT a cyclic
// dictionary→wire MODULE edge; check_layers.py BRIDGE_SOURCE_FILES exemption
// applies (message_view_contract.hpp only; NO direct <fixpp/wire/...> include).
//
// 057 layering (NFR-003-8 / contract C-1/C-5): dict::reify() delegates the
// actual per-MsgType dispatch to two bridge functions declared in the PRIVATE
// same-module header reify_dispatch_bridge.hpp and DEFINED in the build-tree-
// generated TU ${build}/_codegen/reify_dispatch_bridge.cpp (the sole includer
// of the build-tree _dispatch/*.hpp headers). This shipped TU therefore never
// #includes a build-tree header — NFR-003-8 is satisfied literally.
//
// owning_message_handle (057): a live byte-storage handle — {version, bytes_
// deep-copied frame, lazily re-framed view_cache_}. NOT type-erased; as<Msg>()
// stays T059-stubbed (out of scope). Construction is the single hand-written
// factory detail::owning_message_handle_from_frame (research D-2 / contract
// C-2), defined out-of-line here (the handle is a heap pimpl).
#include <cstdint>
#include <expected>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>  // REAL wire surface (Framer,
                                                 // pmr_carry_buffer, frame_view
                                                 // arrive transitively via the
                                                 // parser.hpp re-export) — the
                                                 // ONLY exempt bridge include.
#include <memory_resource>
#include <new>  // std::bad_alloc
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "reify_dispatch_bridge.hpp"  // private same-module bridge declarations

namespace fixpp::dict {

// ─── FIXT admin detection ────────────────────────────────────────────────────
// The 7 FIXT.1.1 admin MsgTypes per [FIXT §3] / [2c §6.3]. Hardcoded here (not
// from the dispatch table) so this shipped TU implements the version-axis
// resolution without including the build-tree generated dispatch headers.
namespace {
[[nodiscard]] constexpr bool is_fixt_admin(char mt) noexcept {
    // '0'=Heartbeat '1'=TestRequest '2'=ResendRequest '3'=Reject
    // '4'=SequenceReset '5'=Logout 'A'=Logon
    return mt == '0' || mt == '1' || mt == '2' || mt == '3' || mt == '4' || mt == '5' || mt == 'A';
}
}  // namespace

// ─── owning_message_handle implementation (057 live byte storage) ────────────
// Storage mirrors a concrete owning_<Msg> minus the typed accessors: the full
// validated frame span deep-copied into the caller mr, plus a lazily re-framed
// MessageView cache (same pattern as owning_<Msg>::view()). Move-only via the
// heap pimpl pointer (moving the pointer moves bytes_ + view_cache_ wholesale).
struct owning_message_handle::impl {
    resolved_message_version version{.k = resolved_message_version::kind::session_admin,
                                     .session = session_version::Unknown,
                                     .application = application_version::Unknown,
                                     ._reserved = 0};
    std::pmr::vector<std::byte> bytes_;
    // 066-dict-backed-inbound-parse T008 (mechanism (b), FR-007/C4): an OWNED
    // copy of the source view's dictionary membership (MessageView::
    // membership_copy(), parser.hpp), populated ONLY when the source is itself
    // dict-backed (MessageView::is_dict_backed()) — else nullopt, so view()'s
    // lazy re-frame below stays dict-free, mirroring a dict-free source
    // (data-model.md "Reify owning handle owned table_view" degenerate case).
    // Heap-owned (table_view's own containers use the default/global
    // allocator, independent of `bytes_`'s mr) and self-contained — safe to
    // outlive the source session/Dictionary (table_view.hpp:185-192/204/221).
    std::optional<table_view> owned_tv_;
    mutable std::optional<wire::MessageView<wire::access_mode::Index>> view_cache_;

    explicit impl(std::pmr::memory_resource* mr) : bytes_(mr) {}
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

wire::MessageView<wire::access_mode::Index> const& owning_message_handle::view() const noexcept {
    static wire::MessageView<wire::access_mode::Index> const kEmpty{};
    if (pimpl_ == nullptr) {
        return kEmpty;
    }
    if (!pimpl_->view_cache_) {
        // Lazily re-frame over the owned bytes_ via a one-shot Framer (same
        // pattern as owning_<Msg>::view()). A zero-cap local carry suffices —
        // a complete frame never appends to it (004 T059 design).
        wire::pmr_carry_buffer carry{0, pimpl_->bytes_.get_allocator().resource()};
        wire::Framer framer{};
        wire::frame_view out_arr[1]{};
        auto framed =
            framer.feed(std::span<const std::byte>{pimpl_->bytes_.data(), pimpl_->bytes_.size()},
                        carry, std::span<wire::frame_view>{out_arr, 1});
        if (framed && !framed->empty()) {
            // 066-dict-backed-inbound-parse T008: re-frame dict-backed when
            // this handle carries an owned membership copy (owned_tv_), so
            // group reads are membership-bounded identically to the source
            // (contracts/inbound-parse.md C4). Reuses the SAME
            // Parser<Index>{table_view} template instantiation the shipped
            // Session inbound path (T006) and the C-ABI clone path (T007)
            // already exercise — no duplicated classify_fn/group_member_fn.
            std::optional<wire::MessageView<wire::access_mode::Index>> reframed;
            if (pimpl_->owned_tv_) {
                wire::Parser<wire::access_mode::Index> parser{*pimpl_->owned_tv_};
                auto parsed =
                    parser.parse((*framed)[0], pimpl_->bytes_.get_allocator().resource());
                if (parsed) {
                    reframed.emplace(std::move(*parsed));
                }
            }
            if (reframed) {
                pimpl_->view_cache_.emplace(std::move(*reframed));
            } else {
                // Dict-free source, OR (practically unreachable — the same
                // bytes the source already parsed successfully) the
                // dict-backed re-parse failed: fall back to the dict-free
                // 2-arg ctor (pre-066 behavior).
                pimpl_->view_cache_.emplace((*framed)[0], pimpl_->bytes_.get_allocator().resource());
            }
        } else {
            pimpl_->view_cache_.emplace();
        }
    }
    return *pimpl_->view_cache_;
}

std::string_view owning_message_handle::msg_type() const noexcept {
    auto mt = view().template get<35>();
    if (!mt) {
        return {};
    }
    return mt->as_string();
}

core::expected_t<wire::field_view> owning_message_handle::field_value(
    std::uint16_t tag) const noexcept {
    return view().get(tag);
}

// ─── detail::owning_message_handle_from_frame (construction seam, C-2) ────────
// The single hand-written factory that mints a live handle: deep-copy the
// validated frame span into mr, set the resolved version, leave view_cache_
// empty (built lazily on first view()). std::bad_alloc → dict_reify_oom. The
// resulting handle is independent of the source parse buffer (FR-005).
namespace detail {
core::expected_t<owning_message_handle> owning_message_handle_from_frame(
    resolved_message_version rmv, wire::MessageView<wire::access_mode::Index> const& view,
    std::pmr::memory_resource* mr) noexcept {
    try {
        owning_message_handle handle{new owning_message_handle::impl{mr}};
        handle.pimpl_->version = rmv;
        auto const sb = view.bytes();                        // full validated frame span
        handle.pimpl_->bytes_.assign(sb.begin(), sb.end());  // single deep copy into mr
        // 066-dict-backed-inbound-parse T008 (mechanism (b), FR-007/C4):
        // propagate the source view's dictionary membership into the handle's
        // own owned table_view, ONLY when the source itself is dict-backed —
        // else stay dict-free (data-model.md degenerate case; see view()'s
        // re-frame above).
        if (view.is_dict_backed()) {
            handle.pimpl_->owned_tv_ = view.membership_copy();
        }
        return handle;                                       // move (custom noexcept move ctor)
    } catch (std::bad_alloc const&) {
        return std::unexpected{core::error::dict_reify_oom};
    }
}
}  // namespace detail

// ─── dict::reify ─────────────────────────────────────────────────────────────
// Runtime-dispatch entry point (AC-D1..D7 / [2c §4.8]).
//
// Resolution algorithm:
//  1. peek MsgType(35) via view.get<35>(). Absent tag 35 → dict_reify_unknown_
//     msg_type (a validated frame always carries tag 35; this is the defensive
//     near-unreachable path — FR-009 deliberate remap from the retired
//     dict_reify_wire_body_not_ready).
//  2. single-char FIXT-admin hit → reify_dispatch_fixt (→ {session_admin,
//     profile.session, Unknown}).
//  3. miss → read ApplVerID(1128) (field-absent → empty sv → profile default) →
//     resolve_application_version → reify_dispatch_application (→ {application,
//     profile.session, resolved}). Multi-char MsgTypes forward the full sv;
//     the emitter's two-level dispatch resolves single- and 2-char arms.
[[nodiscard]] core::expected_t<owning_message_handle> reify(
    wire::MessageView<wire::access_mode::Index> const& view, version_profile profile,
    std::pmr::memory_resource* mr) noexcept {
    // Step 1: peek MsgType(35).
    auto mt_fv = view.template get<35>();
    if (!mt_fv) {
        // Absent tag 35 — cannot dispatch (defensive; a validated frame always
        // carries MsgType). FR-009 deliberate remap: dict_reify_unknown_msg_type
        // (was the now-retired dict_reify_wire_body_not_ready).
        return std::unexpected{core::error::dict_reify_unknown_msg_type};
    }
    std::string_view const mt_sv = mt_fv->as_string();

    // Step 2: FIXT-admin detection (single-char admin MsgType).
    if (mt_sv.size() == 1 && is_fixt_admin(mt_sv.front())) {
        return reify_dispatch_fixt(view, mt_sv.front(), profile, mr);
    }

    // Step 3: application dispatch. Read ApplVerID(1128); field-absent → empty
    // sv → profile default.
    std::string_view appl_ver_sv;
    auto appl_fv = view.template get<1128>();
    if (appl_fv) {
        appl_ver_sv = appl_fv->as_string();
    }

    auto app_ver = resolve_application_version(profile, appl_ver_sv);
    if (!app_ver) {
        // AC-D6: dict_unresolved_application_version.
        // AC-D7: dict_unknown_appl_ver_id.
        return std::unexpected{app_ver.error()};
    }

    return reify_dispatch_application(view, mt_sv, *app_ver, profile, mr);
}

}  // namespace fixpp::dict
