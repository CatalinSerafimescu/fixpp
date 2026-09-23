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
// owning_message_handle (057): a live byte-storage handle (members: `impl`
// below). NOT type-erased; as<Msg>() stays T059-stubbed (out of scope).
// Construction is the single hand-written factory
// detail::owning_message_handle_from_frame (research D-2 / contract C-2),
// defined out-of-line here (the handle is a pimpl whose impl is allocated from
// `mr`, fixpp#495 D-1c).
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
#include <memory>
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
// validated frame span deep-copied into the caller mr, plus an EAGERLY
// materialised MessageView cache (fixpp#458 / 090-capi-refusals D-4 — moved
// out of view()'s former lazy re-frame; see
// detail::owning_message_handle_from_frame below). Move-only through the raw
// pimpl pointer: moving the pointer transfers the impl without relocating it
// (the impl lives in the factory's `mr`, fixpp#495 D-1c).
struct owning_message_handle::impl {
    resolved_message_version version{.k = resolved_message_version::kind::session_admin,
                                     .session = session_version::Unknown,
                                     .application = application_version::Unknown,
                                     ._reserved = 0};
    std::pmr::vector<std::byte> bytes_;
    // ⚠️ Superseded in part by `.specify/495-493-486-dict-reify-copy.md` §2.4
    // (fixpp#495): was an OWNED deep copy (066 T008, `membership_copy()`); now the
    // source's membership table held by reference count, seated from
    // `MessageView::shared_membership()` ONLY when the source is dict-backed — else
    // null, so the eager materialisation below stays dict-free (data-model.md
    // "Reify owning handle owned table_view" degenerate case). An owned-route
    // source SHARES its owner's table (no copy); a borrowed-route source gets a
    // self-contained copy. Either way the pointee is self-contained and may outlive
    // the source session and Dictionary (table_view.hpp's "may legally outlive the
    // Dictionary" note).
    // The owner object of view_cache_'s owned-route parse: the OWNER-OBJECT RULE
    // at Parser's owned-route constructor (parser.hpp; note §3.1) applies. This
    // site's facts: seated once by the factory (re-check with
    // `grep -n "owned_tv_ *=" src/dictionary/reify.cpp`), inside an impl that never
    // relocates; declared BEFORE view_cache_ so reverse-order destruction destroys
    // the view first.
    std::shared_ptr<const table_view> owned_tv_;
    // fixpp#458 D-4: populated once, eagerly, by the factory (rationale there),
    // so no longer `mutable`.
    std::optional<wire::MessageView<wire::access_mode::Index>> view_cache_;

    explicit impl(std::pmr::memory_resource* mr) : bytes_(mr) {}

    // fixpp#495 D-1c (`.specify/495-493-486-dict-reify-copy.md` §2.5): the impl
    // lives in the caller's `mr`. Allocation and release go through these two, and
    // release recovers the resource from `bytes_` BEFORE the impl is destroyed.
    [[nodiscard]] static impl* create(std::pmr::memory_resource* mr) {
        return std::pmr::polymorphic_allocator<>{mr}.new_object<impl>(mr);
    }
    static void destroy(impl* p) noexcept {
        if (p == nullptr) {
            return;
        }
        std::pmr::memory_resource* const mr = p->bytes_.get_allocator().resource();
        std::pmr::polymorphic_allocator<>{mr}.delete_object(p);
    }
};

owning_message_handle::owning_message_handle(owning_message_handle&& other) noexcept
    : pimpl_(other.pimpl_) {
    other.pimpl_ = nullptr;
}

owning_message_handle& owning_message_handle::operator=(owning_message_handle&& other) noexcept {
    if (this != &other) {
        impl::destroy(pimpl_);
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    return *this;
}

owning_message_handle::~owning_message_handle() { impl::destroy(pimpl_); }

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
    // fixpp#458 D-4: a pure read of the cache the factory populated eagerly
    // (rationale at detail::owning_message_handle_from_frame). The factory seats
    // view_cache_ on every path that returns a handle, so an unseated cache is
    // unreachable; checking it keeps that state defined rather than UB now that
    // the seating lives in another function.
    if (pimpl_ == nullptr || !pimpl_->view_cache_) {
        return kEmpty;
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
// validated frame span into mr, set the resolved version, then EAGERLY
// materialise view_cache_ (fixpp#458 / 090-capi-refusals D-4 — moved out of
// view()'s former lazy build-on-first-call). std::bad_alloc during
// construction (the impl, the deep copy, or a borrowed-route membership copy)
// → dict_reify_oom, preserved. A FAILED DICT-BACKED RE-PARSE is the one new refusal this
// factory adds: it returns the wire error the failed parse produced (EC-8),
// with no handle constructed. A framing failure, a framed-but-empty span,
// and a dict-free OffsetTable degradation are all RETAINED, exactly as
// view()'s former lazy re-frame behaved (data-model.md §2.2 B/C). The
// resulting handle is independent of the source parse buffer (FR-005).
namespace detail {
core::expected_t<owning_message_handle> owning_message_handle_from_frame(
    resolved_message_version rmv, wire::MessageView<wire::access_mode::Index> const& view,
    std::pmr::memory_resource* mr) noexcept {
    try {
        // fixpp#495 D-1c: the impl comes from `mr` (the first `mr` call).
        owning_message_handle handle{owning_message_handle::impl::create(mr)};
        handle.pimpl_->version = rmv;
        auto const sb = view.bytes();                        // full validated frame span
        handle.pimpl_->bytes_.assign(sb.begin(), sb.end());  // single deep copy into mr
        // 066-dict-backed-inbound-parse T008 (FR-007/C4), fixpp#495 §2.4:
        // propagate the source view's dictionary membership into the handle,
        // ONLY when the source itself is dict-backed — else stay dict-free
        // (data-model.md degenerate case; see the eager materialisation below).
        // An owned-route source shares its table; a borrowed one is copied.
        if (view.is_dict_backed()) {
            handle.pimpl_->owned_tv_ =
                wire::detail::message_view_membership_access::shared_membership(view);
        }

        // fixpp#458 D-4: EAGER materialisation, moved here verbatim from
        // view()'s former lazy re-frame. A zero-cap local carry suffices — a
        // complete frame never appends to it (004 T059 design).
        wire::pmr_carry_buffer carry{0, mr};
        wire::Framer framer{};
        wire::frame_view out_arr[1]{};
        auto framed = framer.feed(
            std::span<const std::byte>{handle.pimpl_->bytes_.data(), handle.pimpl_->bytes_.size()},
            carry, std::span<wire::frame_view>{out_arr, 1});
        if (framed && !framed->empty()) {
            // 066-dict-backed-inbound-parse T008: re-frame dict-backed when
            // this handle carries a membership table (owned_tv_), so group
            // reads are membership-bounded identically to the source
            // (contracts/inbound-parse.md C4). fixpp#495: on the OWNED route,
            // over the impl's own owner object, so this handle's view() — and
            // any reify or clone of it — shares the table too.
            if (handle.pimpl_->owned_tv_) {
                wire::Parser<wire::access_mode::Index> parser{wire::detail::owned_route_key{},
                                                              handle.pimpl_->owned_tv_};
                // fixpp#493 (`.specify/495-493-486-dict-reify-copy.md` §4): re-parse
                // under the SOURCE's caps, so a raised or lowered cap survives the copy.
                auto parsed = parser.parse((*framed)[0], mr, view.offsets().config());
                if (!parsed) {
                    // fixpp#458 (090-capi-refusals) D-4: a dict-backed source
                    // whose re-parse of the copied frame fails now REFUSES
                    // instead of silently falling through to a dict-free
                    // handle (the fail-open defect this comment used to
                    // document — see contracts/msg-clone.md §9 / EC-8). The
                    // error is the wire error the failed re-parse produced,
                    // verbatim — not dict_reify_oom, not a generic sentinel.
                    // No handle escapes: `handle` unwinds via RAII on this
                    // return; `view` (the source) is untouched.
                    return std::unexpected{parsed.error()};
                }
                handle.pimpl_->view_cache_.emplace(std::move(*parsed));
            } else {
                // Dict-free source (data-model.md §2.2 B, RETAINED): the
                // dict-free constructor never refuses -- OffsetTable::build
                // catches std::bad_alloc internally and degrades in place,
                // publicly reported via view().offsets().build_status().
                // fixpp#493: it takes the source's caps too, through the
                // four-argument form with `dict_hooks::none()`, which also seeds
                // the root group context as a Parser{}-parsed source has it.
                handle.pimpl_->view_cache_.emplace((*framed)[0], mr, view.offsets().config(),
                                                   wire::dict_hooks::none());
            }
        } else {
            // Span frames to nothing, including a zero-byte span
            // (data-model.md §2.2 C, RETAINED): still succeeds, with a
            // default-constructed empty view -- Framer::feed on a zero-byte
            // span returns success with an empty span.
            handle.pimpl_->view_cache_.emplace();
        }

        return handle;  // move (custom noexcept move ctor)
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
