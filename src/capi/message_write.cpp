// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/capi/message_write.cpp — CA-009/CA-010-write: outbound construct/populate/commit + clone.
//
// Implements the 10 CA-009 symbols (T009/US2):
//   fixpp_msg_create_outbound, fixpp_msg_destroy, fixpp_msg_clone,
//   fixpp_msg_set_string, fixpp_msg_set_bytes, fixpp_msg_set_int,
//   fixpp_msg_set_double, fixpp_msg_set_decimal, fixpp_msg_remove_tag,
//   fixpp_msg_commit.
//
// Design anchors:
//   D-5  (dictionary access via fixpp_session::dict_)
//   D-9  (lazy weak_ptr<SessionLiveness> tombstone)
//   E-3  (OutboundAccumulator — shell-owned, in a per-message monotonic arena;
//          E-3/INV-5 reconciled — see note below)
//   E-9  (liveness token / tombstone discipline)
//   INV-3 (framing tag rejection at set-time)
//   Codex #4 (commit->send->destroy safety via fut.get() + deep-copy)
//   SC-003 / FR-006 (zero-global-heap set_*/commit via the per-message arena)
//   [2i §5.2] (steady-state thunk: abort on exception escape)
//
// E-3/INV-5 RECONCILED (ratified): Session::session_arena() does not exist for the
// C-ABI path (the plan's session.hpp:189 was an unreliable claim) and the engine's
// default_session_resource is new_delete_resource().  The outbound fixpp_msg shell
// therefore owns a PER-MESSAGE std::pmr::monotonic_buffer_resource
// (fixpp_msg::arena_resource_, seeded >= frame-cap at create_outbound,
// construction-time); the accumulator + committed buffer carve from it → the
// steady-state set_*/commit path is ZERO-GLOBAL-HEAP (SC-003 dual gate). Shell-owned
// ⇒ session close cannot free the accumulator (no session-arena UAF was ever
// possible), so the E-9 token is the FR-009a semantic validity gate, not UAF
// prevention. Bundle updated: data-model E-1/E-3/E-9 + contracts/message-write.md.
//
// The clone (fixpp_msg_clone) produces an INBOUND-flavoured handle with its own
// deep-copied frame buffer (unique_ptr<byte[]>) + a per-message monotonic arena (for
// the MessageView OffsetTable + any group cursors), a MessageView over the copy, and
// no liveness token. Reads (incl. get_group) are THREAD_SAFE and leak-free.

#include "fix/c_api/message.h"
#include "fix/c_api/decimal.h"
#include "fix/c_api/export.h"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <new>
#include <string_view>

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/session/session.hpp>  // session_arena()
#include <fixpp/wire/framer.hpp>      // frame_view / frame_view_access
#include <fixpp/wire/parser.hpp>      // MessageView

#include "capi_internal.hpp"

// ── frame_view_access — production clone seam ────────────────────────────────
//
// The friend struct of fixpp::wire::frame_view that allows production code to
// mint a populated frame_view from a known (frame, body_off, body_len) triple.
// Defined in namespace fixpp::wire to satisfy the `friend struct frame_view_access`
// declaration in framer.hpp. The test-only analogue lives in
// tests/support/frame_view_factory.hpp.
namespace fixpp::wire {
struct frame_view_access {
    static frame_view make(std::byte const* frame, std::size_t frame_len, std::size_t body_off,
                           std::size_t body_len, detail::generation_token gen = {}) noexcept {
        return frame_view{frame, frame_len, body_off, body_len, gen};
    }
};
}  // namespace fixpp::wire

// ── GroupInstance out-of-line definitions (T003 structural glue) ─────────────
//
// GroupInstance::fields is std::pmr::vector<AccumulatorEntry>; AccumulatorEntry
// must be COMPLETE at the point these bodies are compiled.  Including capi_internal.hpp
// above brings in the complete AccumulatorEntry definition.

GroupInstance::GroupInstance(std::pmr::memory_resource* mr) : fields(mr) {}

GroupInstance::~GroupInstance() = default;

GroupInstance::GroupInstance(GroupInstance&&) noexcept = default;

GroupInstance& GroupInstance::operator=(GroupInstance&&) noexcept = default;  // LCOV_EXCL_LINE — move-assign fires only on vector reallocation of GroupInstance; not exercised in current test corpus

// ── Constants ─────────────────────────────────────────────────────────────────

// Framing tags that are forbidden in set_* (INV-3):
static constexpr uint16_t kFramingTags[] = {8, 9, 34, 49, 52, 56, 10};

static bool is_framing_tag(uint16_t tag) noexcept {
    for (auto t : kFramingTags) {
        if (t == tag) return true;
    }
    return false;
}

// Frame-cap for commit serialization (~3800 B, per session.cpp:4021).
static constexpr std::size_t kFrameCap = 3800;

// ── Handle-validation helpers ─────────────────────────────────────────────────

// Check-before-deref for outbound set_* / commit / remove_tag.
//
// E-9 ordering (D-9): check tag_ FIRST (safe: shell survives arena teardown),
// then check flavour (inbound → INVALID), then check token (lazy tombstone).
// Returns OK if the handle is live and outbound.
static fixpp_error_t check_outbound_msg(const fixpp_msg_t* msg) noexcept {
    if (msg == nullptr) return FIXPP_ERR_NULL_HANDLE;
    const auto* h = reinterpret_cast<const fixpp_msg*>(msg);
    if (h->tag_ == FIXPP_HANDLE_TAG_DEAD) return FIXPP_ERR_INVALID_HANDLE;
    // Inbound flavour (including inbound dispatch handles) → set_* is invalid.
    if (h->flavour != FixppMsgFlavour::outbound) return FIXPP_ERR_INVALID_HANDLE;
    // Lazy tombstone: session close/engine destroy resets liveness_, so token expires.
    if (h->token.expired()) return FIXPP_ERR_INVALID_HANDLE;
    return FIXPP_ERR_OK;
}

// ── Dictionary type-check helpers ─────────────────────────────────────────────

// Setter flavour — drives TYPE_MISMATCH in check_dict (FR-006).
//   String  → always OK (set_string serialises bytes; no dict-type restriction)
//   Int     → only integer-category types accepted
//   Float   → only float-category types accepted
enum class SetterFlavour : uint8_t { String, Int, Float };

// Integer-category dict types: Int, Length, SeqNum, NumInGroup, DayOfMonth.
static bool is_int_category(fixpp::dict::field_data_type t) noexcept {
    using T = fixpp::dict::field_data_type;
    return t == T::Int || t == T::Length || t == T::SeqNum || t == T::NumInGroup ||
           t == T::DayOfMonth;
}

// Float-category dict types: Price, Qty, Amt, PriceOffset, Percentage, Float.
static bool is_float_category(fixpp::dict::field_data_type t) noexcept {
    using T = fixpp::dict::field_data_type;
    return t == T::Price || t == T::Qty || t == T::Amt || t == T::PriceOffset ||
           t == T::Percentage || t == T::Float;
}

// Validate tag against the dictionary for a given msg_type and setter flavour.
// Returns:
//   FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN  — framing tag (INV-3)
//   FIXPP_ERR_DICT_CONFIG                — tag absent from the MsgType grammar
//   FIXPP_ERR_TYPE_MISMATCH              — dict field type ≠ setter flavour (FR-006)
//   FIXPP_ERR_OK                         — all checks pass
// set_string passes SetterFlavour::String (no type check).
// set_bytes skips check_dict entirely (type-agnostic escape hatch).
static fixpp_error_t check_dict(const fixpp_msg* h, uint16_t tag,
                                SetterFlavour flavour = SetterFlavour::String) noexcept {
    if (is_framing_tag(tag)) return FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN;

    // If no dictionary was cached, skip validation (dict_==nullptr on handles
    // created without a dict; the dict is always present for session-created handles).
    if (!h->dict_) return FIXPP_ERR_OK;

    // Look up the tag in the msg_type's grammar.
    const auto& dict = *h->dict_;
    const auto& acc = *h->accumulator;
    auto fr = dict.field_ref(acc.msg_type, tag);
    if (fr.rule == fixpp::dict::field_presence::NotDeclared) {
        return FIXPP_ERR_DICT_CONFIG;
    }

    // FR-006: type-category check for non-String setters.
    if (flavour == SetterFlavour::Int && !is_int_category(fr.type)) {
        return FIXPP_ERR_TYPE_MISMATCH;
    }
    if (flavour == SetterFlavour::Float && !is_float_category(fr.type)) {
        return FIXPP_ERR_TYPE_MISMATCH;
    }
    return FIXPP_ERR_OK;
}

// ── AccumulatorEntry helpers ──────────────────────────────────────────────────

// Upsert: find or create an AccumulatorEntry for `tag`.
// Returns pointer into the vector (pointer stable under move; not under push_back).
static AccumulatorEntry& upsert_entry(std::pmr::vector<AccumulatorEntry>& entries,
                                      std::pmr::memory_resource* mr, uint16_t tag) {
    for (auto& e : entries) {
        if (e.tag == tag) return e;
    }
    entries.emplace_back(mr);
    entries.back().tag = tag;
    return entries.back();
}

// ── Serialisation helpers ─────────────────────────────────────────────────────

// Append "tag=value\x01" to `out`, advancing `pos`. Returns false if no space.
static bool append_field(std::byte* buf, std::size_t cap, std::size_t& pos, uint16_t tag,
                         const std::byte* value, std::size_t vlen) noexcept {
    // Compute the tag digits.
    char tagbuf[8];
    int taglen =
        static_cast<int>(std::to_chars(tagbuf, tagbuf + sizeof(tagbuf), tag).ptr - tagbuf);
    // Need: taglen + 1 (=) + vlen + 1 (\x01)
    std::size_t needed = static_cast<std::size_t>(taglen) + 1 + vlen + 1;
    if (pos + needed > cap) return false;
    std::memcpy(buf + pos, tagbuf, static_cast<std::size_t>(taglen));
    pos += static_cast<std::size_t>(taglen);
    buf[pos++] = static_cast<std::byte>('=');
    std::memcpy(buf + pos, value, vlen);
    pos += vlen;
    buf[pos++] = static_cast<std::byte>('\x01');
    return true;
}

// ── CA-009 implementations ────────────────────────────────────────────────────

extern "C" {

// ── fixpp_msg_create_outbound ─────────────────────────────────────────────────
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_create_outbound(fixpp_session_t* session,
                                                    const char* msg_type, size_t msg_type_len,
                                                    fixpp_msg_t** msg_out) {
    if (msg_out != nullptr) *msg_out = nullptr;
    if (session == nullptr || msg_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    if (msg_type == nullptr) return FIXPP_ERR_NULL_HANDLE;

    // Re-use the fixpp_session concrete type to validate and extract fields.
    const auto* sess = reinterpret_cast<const fixpp_session*>(session);

    // Validate session liveness.
    if (!sess->valid.load(std::memory_order_acquire) || sess->engine == nullptr ||
        sess->engine->tag_ == FIXPP_HANDLE_TAG_DEAD || sess->engine->state_ == nullptr ||
        !sess->engine->state_->engine_.has_value()) {
        return FIXPP_ERR_INVALID_HANDLE;
    }

    // Verify the msg_type exists in the session dictionary (D-5 / DICT_CONFIG).
    std::string_view mt{msg_type, msg_type_len};
    if (sess->dict_) {
        bool found = false;
        for (const auto& entry : sess->dict_->messages()) {
            if (entry.msg_type == mt) {
                found = true;
                break;
            }
        }
        if (!found) return FIXPP_ERR_DICT_CONFIG;
    }

    // Construction-time thunk: allocate the outbound handle + a per-message arena
    // + the accumulator. The arena seed below is a CONSTRUCTION-time allocation
    // (FR-020 allows it); the steady-state set_*/commit path then allocates ONLY
    // from that arena → zero global heap (SC-003 dual gate). E-3 reconciliation:
    // Session::session_arena() does not exist for the C-ABI path; the per-message
    // monotonic arena (shell-owned) replaces it and is safer (no session-arena UAF).
    //
    // Exceptions during construction → translate to FIXPP_ERR_CAPI_CONFIG_INVALID.
    try {
        // Heap-allocate the fixpp_msg shell.
        auto* h = new fixpp_msg{};  // NOLINT(cppcoreguidelines-owning-memory)
        h->tag_ = FIXPP_HANDLE_TAG_MSG;
        h->flavour = FixppMsgFlavour::outbound;
        h->view = nullptr;

        // Copy the session's weak liveness token (E-9: set BEFORE any arena use).
        h->token = reinterpret_cast<const fixpp_session*>(session)->liveness_;

        // Copy the dictionary reference into the msg shell (D-5) so setters can
        // validate without re-leasing the session.
        h->dict_ = reinterpret_cast<const fixpp_session*>(session)->dict_;

        // Seed the per-message arena (construction-time). 16 KiB comfortably holds
        // a frame-cap (~3800 B) message's accumulator (field bytes + container
        // overhead); typical messages don't spill (overwrite-churn of the same tag
        // can, → graceful new_delete upstream). Upstream = new_delete (NOT null →
        // no terminate on pathological overwrite-heavy builds).
        constexpr std::size_t kPerMsgArenaSeed = 16384;
        h->arena_buf_ = std::make_unique<std::byte[]>(kPerMsgArenaSeed);
        h->arena_resource_ = std::make_unique<std::pmr::monotonic_buffer_resource>(
            h->arena_buf_.get(), kPerMsgArenaSeed, std::pmr::new_delete_resource());

        // Construct the OutboundAccumulator over the per-message arena (owned by
        // fixpp_msg); all set_*/commit allocations carve from it (zero-global-heap).
        auto* acc = new OutboundAccumulator{h->arena_resource_.get()};
        acc->msg_type.assign(mt.data(), mt.size());
        h->accumulator = acc;

        *msg_out = reinterpret_cast<fixpp_msg_t*>(h);
        return FIXPP_ERR_OK;
    } catch (...) {  // LCOV_EXCL_LINE — OOM/new-failure during arena creation; untestable in unit tests
        return FIXPP_ERR_CAPI_CONFIG_INVALID;  // LCOV_EXCL_LINE
    }  // LCOV_EXCL_LINE
}

// ── fixpp_msg_destroy ─────────────────────────────────────────────────────────
//
// NULL-safe: returns OK for NULL.  Single-destroy only: double-destroy of the
// same non-null pointer is UB (standard C free() contract).  Consumer must null
// their pointer after destroy to avoid UB on a second call.
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_destroy(fixpp_msg_t* msg) {
    if (msg == nullptr) return FIXPP_ERR_OK;
    auto* h = reinterpret_cast<fixpp_msg*>(msg);

    // Expire the session liveness token (no-op for inbound/clone handles that
    // have an expired token already).
    h->token.reset();

    // Release the dict shared_ptr (decrements refcount; may free the dict if last ref).
    h->dict_.reset();

    // Release clone-owned buffers (unique_ptrs auto-destruct if non-null).
    h->owned_view_.reset();
    h->owned_frame_.reset();

    // The OutboundAccumulator is heap-allocated over the per-message arena; delete
    // it FIRST (its pmr members deallocate to arena_resource_, still alive here).
    // For inbound/clone handles, accumulator is nullptr — safe to delete nullptr.
    delete h->accumulator;
    h->accumulator = nullptr;

    // Free the per-message arena AFTER the accumulator (resource before its backing
    // buffer).  nullptr for inbound/clone handles (reset() is a no-op).
    h->arena_resource_.reset();
    h->arena_buf_.reset();

    // Free the shell.  Per the narrowed contract (B-051-2), single-destroy only;
    // double-destroy of the same pointer is UB — consumer nulls their pointer.
    // Unlike engine handles (O(few), retain viable), msg handles are per-send
    // (unbounded), so retaining shells would leak ~150 B/msg indefinitely.
    delete h;  // NOLINT(cppcoreguidelines-owning-memory)
    return FIXPP_ERR_OK;
}

// ── fixpp_msg_clone ───────────────────────────────────────────────────────────
//
// Creates an inbound-flavoured clone of `src` with its own heap-allocated frame
// buffer and a dict-free MessageView<Index> built over it.
// The clone is session-independent (no liveness token) and reads THREAD_SAFE.
//
// For inbound handles: copies the raw wire frame bytes from src->view->bytes().
// For outbound handles: not supported yet (returns INVALID_HANDLE; the contract
//   note says "version_mismatch if version not in loaded dicts" — for an outbound
//   accumulator that hasn't been committed there are no raw wire bytes).
//   We only support cloning inbound messages in CA-009 scope (T011 tests this path).
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_clone(const fixpp_msg_t* src,
                                               fixpp_msg_t** clone_out) {
    if (clone_out != nullptr) *clone_out = nullptr;
    if (src == nullptr || clone_out == nullptr) return FIXPP_ERR_NULL_HANDLE;

    const auto* h = reinterpret_cast<const fixpp_msg*>(src);
    if (h->tag_ == FIXPP_HANDLE_TAG_DEAD) return FIXPP_ERR_INVALID_HANDLE;

    // For outbound accumulator (no view): we can only clone inbound/view-based handles.
    if (h->view == nullptr) {
        // Outbound accumulator clone is not implemented in CA-009 scope.
        return FIXPP_ERR_INVALID_HANDLE;
    }

    // Construction-time thunk: catch→translate.
    try {
        // Get the source's raw wire bytes.
        auto src_bytes = h->view->bytes();  // span<const byte> aliasing the source frame
        std::size_t frame_len = src_bytes.size();

        // Allocate a new owned frame buffer (deep copy).
        auto owned_frame = std::make_unique<std::byte[]>(frame_len);
        std::memcpy(owned_frame.get(), src_bytes.data(), frame_len);

        // Locate the "9=" and "10=" boundaries to compute body_off / body_len
        // for the frame_view we build over the cloned bytes. Uses the
        // fixpp::wire::frame_view_access helper defined above in this TU.
        auto mk_fv = [](const std::byte* buf, std::size_t len)
            -> std::optional<fixpp::wire::frame_view> {
            constexpr char SOH = '\x01';
            std::string_view s{reinterpret_cast<const char*>(buf), len};
            std::size_t p9 = s.starts_with("9=") ? 0 : s.find("\x01" "9=");
            if (p9 == std::string_view::npos) return std::nullopt;  // LCOV_EXCL_LINE — valid inbound view always has 9=
            if (s[p9] == SOH) ++p9;
            std::size_t soh9 = s.find(SOH, p9);
            if (soh9 == std::string_view::npos) return std::nullopt;  // LCOV_EXCL_LINE — valid inbound view has SOH after 9=NNN
            std::size_t body_off = soh9 + 1;
            std::size_t p10 = s.find("\x01" "10=", body_off);
            if (p10 == std::string_view::npos) return std::nullopt;  // LCOV_EXCL_LINE — valid inbound view always has 10=
            std::size_t body_len = (p10 + 1) - body_off;
            return fixpp::wire::frame_view_access::make(buf, len, body_off, body_len);
        };

        auto maybe_fv = mk_fv(owned_frame.get(), frame_len);

        // Allocate the clone shell first so we can seed its per-clone arena
        // BEFORE building the MessageView.  The arena (arena_buf_ / arena_resource_)
        // backs the clone's OffsetTable PMR vectors AND any group cursor shells
        // allocated via fixpp_msg_get_group on the clone.  Seeded to frame_len +
        // 4096 bytes: OffsetTable entries are proportional to the frame size; the
        // extra 4096 gives headroom for group_slices + cursor shells.  Upstream =
        // new_delete (graceful degrade if arena is exhausted, never null).
        // Destruction order: fixpp_msg_destroy resets owned_view_ (line ~338) BEFORE
        // arena_resource_ (line ~350), so MessageView destructs into a live arena.
        auto* clone = new fixpp_msg{};
        constexpr std::size_t kCursorHeadroom = 4096;
        std::size_t clone_arena_size = frame_len + kCursorHeadroom;
        clone->arena_buf_ = std::make_unique<std::byte[]>(clone_arena_size);
        clone->arena_resource_ = std::make_unique<std::pmr::monotonic_buffer_resource>(
            clone->arena_buf_.get(), clone_arena_size, std::pmr::new_delete_resource());
        auto* clone_mr = clone->arena_resource_.get();

        // Build the owned dict-free MessageView<Index> over the cloned bytes.
        fixpp::wire::frame_view fv =
            maybe_fv.value_or(fixpp::wire::frame_view_access::make(owned_frame.get(), frame_len, 0, frame_len));
        auto clone_view =
            std::make_unique<fixpp::wire::MessageView<fixpp::wire::access_mode::Index>>(fv, clone_mr);

        clone->tag_ = FIXPP_HANDLE_TAG_MSG;
        clone->flavour = FixppMsgFlavour::inbound;  // reads via view (get_* API)
        clone->view = clone_view.get();              // points to the owned view
        clone->accumulator = nullptr;
        // token is default-constructed (expired) — clone is session-independent (D-9).
        // dict_ is nullptr for clone (no outbound mutation path).
        clone->owned_frame_ = std::move(owned_frame);
        clone->owned_view_ = std::move(clone_view);

        *clone_out = reinterpret_cast<fixpp_msg_t*>(clone);
        return FIXPP_ERR_OK;
    } catch (...) {  // LCOV_EXCL_LINE — OOM during clone construction; untestable in unit tests
        return FIXPP_ERR_CAPI_CONFIG_INVALID;  // LCOV_EXCL_LINE
    }  // LCOV_EXCL_LINE
}

// ── fixpp_msg_set_string ──────────────────────────────────────────────────────
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_string(fixpp_msg_t* msg, uint16_t tag,
                                               const char* value, size_t len) {
    if (msg == nullptr) return FIXPP_ERR_NULL_HANDLE;
    if (value == nullptr) return FIXPP_ERR_NULL_HANDLE;
    // Check-before-deref (E-9 / D-9): dead tag → INVALID; inbound → INVALID; token expired → INVALID.
    if (fixpp_error_t c = check_outbound_msg(msg); c != FIXPP_ERR_OK) return c;

    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    // Dict validation (framing tag + DICT_CONFIG). String setter: always OK on any dict type.
    if (fixpp_error_t c = check_dict(h, tag, SetterFlavour::String); c != FIXPP_ERR_OK) return c;

    // Steady-state thunk: abort on exception escape ([2i §5.2]).
    auto& acc = *h->accumulator;
    auto& entry = upsert_entry(acc.entries, acc.arena_, tag);
    const auto* bytes = reinterpret_cast<const std::byte*>(value);
    entry.value_bytes.assign(bytes, bytes + len);
    return FIXPP_ERR_OK;
}

// ── fixpp_msg_set_bytes ───────────────────────────────────────────────────────
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_bytes(fixpp_msg_t* msg, uint16_t tag,
                                              const uint8_t* bytes, size_t len) {
    if (msg == nullptr) return FIXPP_ERR_NULL_HANDLE;
    if (bytes == nullptr && len > 0) return FIXPP_ERR_NULL_HANDLE;
    if (fixpp_error_t c = check_outbound_msg(msg); c != FIXPP_ERR_OK) return c;

    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    // Framing tag check (INV-3). No dict type check for set_bytes (type-agnostic escape).
    if (is_framing_tag(tag)) return FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN;

    auto& acc = *h->accumulator;
    auto& entry = upsert_entry(acc.entries, acc.arena_, tag);
    const auto* bdata = reinterpret_cast<const std::byte*>(bytes);
    entry.value_bytes.assign(bdata, bdata + len);
    return FIXPP_ERR_OK;
}

// ── fixpp_msg_set_int ─────────────────────────────────────────────────────────
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_int(fixpp_msg_t* msg, uint16_t tag, int64_t value) {
    if (msg == nullptr) return FIXPP_ERR_NULL_HANDLE;
    if (fixpp_error_t c = check_outbound_msg(msg); c != FIXPP_ERR_OK) return c;

    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    if (fixpp_error_t c = check_dict(h, tag, SetterFlavour::Int); c != FIXPP_ERR_OK) return c;

    // Serialise int64 as ASCII decimal into a small stack buffer, then store.
    char buf[24];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    if (ec != std::errc{}) return FIXPP_ERR_WIRE_INVALID_FRAME;  // extremely unlikely

    auto& acc = *h->accumulator;
    auto& entry = upsert_entry(acc.entries, acc.arena_, tag);
    const auto* bdata = reinterpret_cast<const std::byte*>(buf);
    std::size_t blen = static_cast<std::size_t>(ptr - buf);
    entry.value_bytes.assign(bdata, bdata + blen);
    return FIXPP_ERR_OK;
}

// ── fixpp_msg_set_double ──────────────────────────────────────────────────────
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_double(fixpp_msg_t* msg, uint16_t tag,
                                                    double value) {
    if (msg == nullptr) return FIXPP_ERR_NULL_HANDLE;
    if (fixpp_error_t c = check_outbound_msg(msg); c != FIXPP_ERR_OK) return c;

    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    if (fixpp_error_t c = check_dict(h, tag, SetterFlavour::Float); c != FIXPP_ERR_OK) return c;

    // Serialise double via snprintf "%.10g" (FIX convention for float fields).
    char buf[64];
    int n = std::snprintf(buf, sizeof(buf), "%.10g", value);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(buf)) return FIXPP_ERR_WIRE_INVALID_FRAME;  // LCOV_EXCL_LINE — impossible for finite doubles

    auto& acc = *h->accumulator;
    auto& entry = upsert_entry(acc.entries, acc.arena_, tag);
    const auto* bdata = reinterpret_cast<const std::byte*>(buf);
    entry.value_bytes.assign(bdata, bdata + static_cast<std::size_t>(n));
    return FIXPP_ERR_OK;
}

// ── fixpp_msg_set_decimal ─────────────────────────────────────────────────────
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_decimal(fixpp_msg_t* msg, uint16_t tag,
                                                fixpp_decimal_t value) {
    if (msg == nullptr) return FIXPP_ERR_NULL_HANDLE;
    if (fixpp_error_t c = check_outbound_msg(msg); c != FIXPP_ERR_OK) return c;

    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    if (fixpp_error_t c = check_dict(h, tag, SetterFlavour::Float); c != FIXPP_ERR_OK) return c;

    // Serialise the decimal via fixpp_decimal_format (the C-ABI decimal formatter).
    char buf[64];
    std::size_t written = 0;
    fixpp_error_t rc = fixpp_decimal_format(value, buf, sizeof(buf), &written);
    if (rc != FIXPP_ERR_OK) return rc;

    auto& acc = *h->accumulator;
    auto& entry = upsert_entry(acc.entries, acc.arena_, tag);
    const auto* bdata = reinterpret_cast<const std::byte*>(buf);
    entry.value_bytes.assign(bdata, bdata + written);
    return FIXPP_ERR_OK;
}

// ── US4: recursive group serialisation + builder/entry resolution ─────────────

// Recursive payload size: scalar "<tag>=<value>\x01"; group "<NoXXX>=<count>\x01"
// then each instance's fields (recursive).
static std::size_t compute_entries_size(const std::pmr::vector<AccumulatorEntry>& entries) {
    std::size_t total = 0;
    for (const auto& e : entries) {
        if (e.is_group) {
            char nb[8];
            int nl = static_cast<int>(std::to_chars(nb, nb + sizeof(nb), e.tag).ptr - nb);
            char cb[16];
            int cl = static_cast<int>(
                std::to_chars(cb, cb + sizeof(cb), e.instances.size()).ptr - cb);
            total += static_cast<std::size_t>(nl) + 1 + static_cast<std::size_t>(cl) + 1;
            for (const auto& inst : e.instances) total += compute_entries_size(inst.fields);
        } else {
            char tb[8];
            int tl = static_cast<int>(std::to_chars(tb, tb + sizeof(tb), e.tag).ptr - tb);
            total += static_cast<std::size_t>(tl) + 1 + e.value_bytes.size() + 1;
        }
    }
    return total;
}

// Recursive serialise (INV-4: NoXXX=count then per-instance delimiter-first).
static bool serialise_entries(std::byte* buf, std::size_t cap, std::size_t& pos,
                              const std::pmr::vector<AccumulatorEntry>& entries) noexcept {
    for (const auto& e : entries) {
        if (e.is_group) {
            char cb[16];
            int cl = static_cast<int>(
                std::to_chars(cb, cb + sizeof(cb), e.instances.size()).ptr - cb);
            if (!append_field(buf, cap, pos, e.tag, reinterpret_cast<const std::byte*>(cb),
                              static_cast<std::size_t>(cl)))
                return false;  // LCOV_EXCL_LINE — buffer exact-sized in commit; unreachable
            for (const auto& inst : e.instances)
                if (!serialise_entries(buf, cap, pos, inst.fields)) return false;  // LCOV_EXCL_LINE — buffer exact-sized
        } else {
            if (!append_field(buf, cap, pos, e.tag, e.value_bytes.data(), e.value_bytes.size()))
                return false;  // LCOV_EXCL_LINE — buffer exact-sized in commit; unreachable
        }
    }
    return true;
}

// Re-resolve a builder's group AccumulatorEntry BY INDEX (stable under the
// vector reallocations that add_entry / group_begin trigger).
static AccumulatorEntry* resolve_group(fixpp_group_builder* b) noexcept {
    if (b->parent == nullptr) {
        return &b->msg->accumulator->entries[b->group_field_index];
    }
    AccumulatorEntry* pg = resolve_group(b->parent->builder);
    GroupInstance& inst = pg->instances[b->parent->instance_index];
    return &inst.fields[b->group_field_index];
}

static GroupInstance* resolve_instance(fixpp_entry* e) noexcept {
    AccumulatorEntry* g = resolve_group(e->builder);
    return &g->instances[e->instance_index];
}

// Validity: a builder is live ⇒ builder->open AND its owning outbound msg is live.
static fixpp_error_t check_builder(fixpp_group_builder* b) noexcept {
    if (b == nullptr) return FIXPP_ERR_NULL_HANDLE;
    if (!b->open) return FIXPP_ERR_INVALID_HANDLE;
    return check_outbound_msg(reinterpret_cast<fixpp_msg_t*>(b->msg));
}

static fixpp_error_t check_entry(fixpp_entry* e) noexcept {
    if (e == nullptr) return FIXPP_ERR_NULL_HANDLE;
    return check_builder(e->builder);
}

// ── fixpp_msg_remove_tag ──────────────────────────────────────────────────────
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_remove_tag(fixpp_msg_t* msg, uint16_t tag) {
    if (msg == nullptr) return FIXPP_ERR_NULL_HANDLE;
    if (fixpp_error_t c = check_outbound_msg(msg); c != FIXPP_ERR_OK) return c;

    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    auto& entries = h->accumulator->entries;
    // Erase the entry with `tag` if present (idempotent: absent → no-op).
    auto it = std::find_if(entries.begin(), entries.end(),
                           [tag](const AccumulatorEntry& e) { return e.tag == tag; });
    if (it != entries.end()) {
        entries.erase(it);
    }
    return FIXPP_ERR_OK;
}

// ── Group grammar validation (INV-4, Fix 2) ───────────────────────────────────
//
// Validates that every group instance is non-empty and delimiter-first (INV-4 legs
// (c)/(d)).  Called from fixpp_msg_commit AFTER the open-builder check.
// Returns TYPE_MISMATCH on violation (consistent with group_begin's non-group-tag
// reject; no new symbol or error code needed).
// Guarded on dict_ presence for the delimiter check (no-dict → empty check only).
static fixpp_error_t validate_group_grammar(const std::pmr::vector<AccumulatorEntry>& entries,
                                             const fixpp::dict::Dictionary* dict) noexcept {
    for (const auto& e : entries) {
        if (!e.is_group) continue;
        for (const auto& inst : e.instances) {
            // INV-4 leg (c): each instance must have at least one field.
            if (inst.fields.empty()) return FIXPP_ERR_TYPE_MISMATCH;
            // INV-4 leg (d): first field must be the group delimiter (dict-gated).
            if (dict) {
                uint16_t delim = dict->group_first_field(e.tag);
                if (delim != 0 && inst.fields[0].tag != delim) {
                    return FIXPP_ERR_TYPE_MISMATCH;
                }
            }
            // Recurse into nested groups within this instance.
            if (fixpp_error_t c = validate_group_grammar(inst.fields, dict);
                c != FIXPP_ERR_OK) return c;
        }
    }
    return FIXPP_ERR_OK;
}

// ── fixpp_msg_commit ──────────────────────────────────────────────────────────
//
// Serialise the accumulator into an app-payload in the session arena.
// Format: "35=<type>\x01<tag>=<value>\x01..." (no framing tags).
// Steady-state thunk: abort on exception escape ([2i §5.2]).
// Returns TYPE_MISMATCH when group grammar is violated (INV-4: empty instance or
// non-delimiter-first instance).
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_commit(fixpp_msg_t* msg, const uint8_t** payload_out,
                                           size_t* len_out) {
    if (payload_out != nullptr) *payload_out = nullptr;
    if (len_out != nullptr) *len_out = 0;
    if (msg == nullptr || payload_out == nullptr || len_out == nullptr)
        return FIXPP_ERR_NULL_HANDLE;

    if (fixpp_error_t c = check_outbound_msg(msg); c != FIXPP_ERR_OK) return c;

    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    const auto& acc = *h->accumulator;

    // An open (unended) group builder ⇒ not a sealed, committable state (analyze C2).
    if (!acc.open_builders.empty()) return FIXPP_ERR_INVALID_HANDLE;

    // INV-4 group grammar: non-empty + delimiter-first per instance.
    if (fixpp_error_t c = validate_group_grammar(acc.entries, h->dict_.get());
        c != FIXPP_ERR_OK) return c;

    // First pass: compute total payload size = "35=<mt>\x01" + entries (recursive,
    // scalars + groups, US4).
    std::size_t total = 0;
    {
        std::string_view mt = acc.msg_type;
        total += 3 + mt.size() + 1;  // "35=" + mt + "\x01"
    }
    total += compute_entries_size(acc.entries);

    if (total > kFrameCap) return FIXPP_ERR_WIRE_LIMIT_EXCEEDED;

    // Allocate the committed payload from the per-message arena (zero-global-heap;
    // the span aliases the arena per the contract). Freed wholesale when the arena
    // is reclaimed in fixpp_msg_destroy.
    // Codex #4: fixpp_session_send blocks on fut.get() AND Engine::send deep-copies
    // at entry, so destroying the msg IMMEDIATELY after send returns is safe.
    auto* buf = static_cast<std::byte*>(h->arena_resource_->allocate(total, alignof(std::byte)));

    // Second pass: serialise.
    std::size_t pos = 0;

    // Write "35=<mt>\x01"
    {
        std::string_view mt = acc.msg_type;
        const char* prefix = "35=";
        std::memcpy(buf + pos, prefix, 3);
        pos += 3;
        std::memcpy(buf + pos, mt.data(), mt.size());
        pos += mt.size();
        buf[pos++] = static_cast<std::byte>('\x01');
    }

    // Write each entry (US4: scalars + groups, recursive).
    if (!serialise_entries(buf, total, pos, acc.entries)) {  // LCOV_EXCL_LINE
        return FIXPP_ERR_WIRE_LIMIT_EXCEEDED;  // LCOV_EXCL_LINE — buffer exact-sized; unreachable
    }  // LCOV_EXCL_LINE

    *payload_out = reinterpret_cast<const uint8_t*>(buf);
    *len_out = pos;
    return FIXPP_ERR_OK;
}

// ── CA-010-write group builder (US4 / T016) ───────────────────────────────────
// Builders + entries are ARENA-allocated (per-message monotonic arena) — never raw
// new — and hold INDICES re-resolved per call (stable under vector reallocation).

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_group_begin(fixpp_msg_t* msg, uint16_t group_tag,
                                                fixpp_group_builder_t** builder_out) {
    if (builder_out != nullptr) *builder_out = nullptr;
    if (builder_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    if (fixpp_error_t c = check_outbound_msg(msg); c != FIXPP_ERR_OK) return c;
    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    if (is_framing_tag(group_tag)) return FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN;
    // group_tag must be a NumInGroup count tag (group_first_field == 0 ⇒ not a group).
    if (h->dict_ && h->dict_->group_first_field(group_tag) == 0) return FIXPP_ERR_TYPE_MISMATCH;

    auto& acc = *h->accumulator;
    acc.entries.emplace_back(acc.arena_);
    auto idx = static_cast<std::uint32_t>(acc.entries.size() - 1);
    acc.entries.back().tag = group_tag;
    acc.entries.back().is_group = true;

    auto* b = std::pmr::polymorphic_allocator<fixpp_group_builder>(acc.arena_)
                  .new_object<fixpp_group_builder>();
    b->msg = h;
    b->parent = nullptr;
    b->group_field_index = idx;
    b->open = true;
    acc.open_builders.push_back(b);
    *builder_out = reinterpret_cast<fixpp_group_builder_t*>(b);
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_group_builder_add_entry(fixpp_group_builder_t* builder,
                                                       fixpp_entry_t** entry_out) {
    if (entry_out != nullptr) *entry_out = nullptr;
    if (entry_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    auto* b = reinterpret_cast<fixpp_group_builder*>(builder);
    if (fixpp_error_t c = check_builder(b); c != FIXPP_ERR_OK) return c;

    auto* arena = b->msg->accumulator->arena_;
    AccumulatorEntry* g = resolve_group(b);
    g->instances.emplace_back(arena);
    auto inst_idx = static_cast<std::uint32_t>(g->instances.size() - 1);

    auto* e = std::pmr::polymorphic_allocator<fixpp_entry>(arena).new_object<fixpp_entry>();
    e->builder = b;
    e->instance_index = inst_idx;
    *entry_out = reinterpret_cast<fixpp_entry_t*>(e);
    return FIXPP_ERR_OK;
}

// Shared entry-field setter: framing-tag reject (INV-3) + upsert into the instance.
static fixpp_error_t entry_set_bytes_impl(fixpp_entry_t* entry, uint16_t tag,
                                          const std::byte* data, std::size_t len) {
    auto* e = reinterpret_cast<fixpp_entry*>(entry);
    if (fixpp_error_t c = check_entry(e); c != FIXPP_ERR_OK) return c;
    if (is_framing_tag(tag)) return FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN;
    auto* arena = e->builder->msg->accumulator->arena_;
    GroupInstance* inst = resolve_instance(e);
    auto& fe = upsert_entry(inst->fields, arena, tag);
    fe.is_group = false;
    fe.value_bytes.assign(data, data + len);
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_string(fixpp_entry_t* entry, uint16_t tag,
                                                 const char* value, size_t len) {
    if (value == nullptr) return FIXPP_ERR_NULL_HANDLE;
    return entry_set_bytes_impl(entry, tag, reinterpret_cast<const std::byte*>(value), len);
}

FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_int(fixpp_entry_t* entry, uint16_t tag,
                                              int64_t value) {
    char buf[24];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    if (ec != std::errc{}) return FIXPP_ERR_WIRE_INVALID_FRAME;
    return entry_set_bytes_impl(entry, tag, reinterpret_cast<const std::byte*>(buf),
                                static_cast<std::size_t>(ptr - buf));
}

FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_double(fixpp_entry_t* entry, uint16_t tag,
                                                 double value) {
    char buf[64];
    int n = std::snprintf(buf, sizeof(buf), "%.10g", value);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(buf)) return FIXPP_ERR_WIRE_INVALID_FRAME;  // LCOV_EXCL_LINE — impossible for finite doubles
    return entry_set_bytes_impl(entry, tag, reinterpret_cast<const std::byte*>(buf),
                                static_cast<std::size_t>(n));
}

FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_decimal(fixpp_entry_t* entry, uint16_t tag,
                                                  fixpp_decimal_t value) {
    char buf[64];
    std::size_t written = 0;
    fixpp_error_t rc = fixpp_decimal_format(value, buf, sizeof(buf), &written);
    if (rc != FIXPP_ERR_OK) return rc;
    return entry_set_bytes_impl(entry, tag, reinterpret_cast<const std::byte*>(buf), written);
}

FIXPP_API_EXPORT fixpp_error_t fixpp_entry_group_begin(fixpp_entry_t* entry, uint16_t group_tag,
                                                  fixpp_group_builder_t** builder_out) {
    if (builder_out != nullptr) *builder_out = nullptr;
    if (builder_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    auto* e = reinterpret_cast<fixpp_entry*>(entry);
    if (fixpp_error_t c = check_entry(e); c != FIXPP_ERR_OK) return c;
    auto* h = e->builder->msg;
    if (is_framing_tag(group_tag)) return FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN;
    if (h->dict_ && h->dict_->group_first_field(group_tag) == 0) return FIXPP_ERR_TYPE_MISMATCH;

    auto* arena = h->accumulator->arena_;
    GroupInstance* inst = resolve_instance(e);
    inst->fields.emplace_back(arena);
    auto idx = static_cast<std::uint32_t>(inst->fields.size() - 1);
    inst->fields.back().tag = group_tag;
    inst->fields.back().is_group = true;

    auto* b = std::pmr::polymorphic_allocator<fixpp_group_builder>(arena)
                  .new_object<fixpp_group_builder>();
    b->msg = h;
    b->parent = e;  // nested within this entry
    b->group_field_index = idx;
    b->open = true;
    h->accumulator->open_builders.push_back(b);
    *builder_out = reinterpret_cast<fixpp_group_builder_t*>(b);
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_group_end(fixpp_msg_t* msg, fixpp_group_builder_t* builder) {
    if (msg == nullptr || builder == nullptr) return FIXPP_ERR_NULL_HANDLE;
    if (fixpp_error_t c = check_outbound_msg(msg); c != FIXPP_ERR_OK) return c;
    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    auto* b = reinterpret_cast<fixpp_group_builder*>(builder);
    if (b->msg != h) return FIXPP_ERR_INVALID_HANDLE;  // builder belongs to another msg
    auto& stack = h->accumulator->open_builders;
    // LIFO: builder MUST be the innermost still-open builder (E-4).
    if (stack.empty() || stack.back() != b || !b->open) return FIXPP_ERR_INVALID_HANDLE;
    stack.pop_back();
    b->open = false;  // invalidates the builder + its entries (validity ⇒ builder->open)
    return FIXPP_ERR_OK;
}

}  // extern "C"
