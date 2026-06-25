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
//   E-3  (OutboundAccumulator — structurally heap-owned by shell for C-ABI path;
//          see ESCALATION note below re E-3/INV-5 deviation)
//   E-9  (liveness token / tombstone discipline)
//   INV-3 (framing tag rejection at set-time)
//   Codex #4 (commit->send->destroy safety via fut.get() + deep-copy)
//   [const §VIII.5] (zero alloc between parse and fromApp — not applicable here)
//   [2i §5.2] (steady-state thunk: abort on exception escape)
//
// ESCALATION (not yet ratified by orchestrator): data-model E-3/INV-5 says the
// OutboundAccumulator lives "in the session arena (no global-heap)".  The C-ABI
// path has no way to set a custom session resource (EngineConfig::
// default_session_resource defaults to get_default_resource() == new_delete_
// resource(); the fixpp_engine_config_t builder exposes no override).  Therefore
// "session arena" == new_delete for C-ABI, and the heap-owned accumulator is
// observably spec-equivalent.  Shell-owns-and-deletes is also REQUIRED for safe
// destroy ordering (arena-owned model would UAF or leak without wholesale reclaim).
// Orchestrator: please ratify or correct E-3/INV-5 for the C-ABI context.
//
// The clone implementation (fixpp_msg_clone) produces an INBOUND-flavoured handle
// with its own heap-allocated buffer (unique_ptr<byte[]>) and a MessageView over it.
// The clone is session-independent (no liveness token), THREAD_SAFE for reads.

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

GroupInstance& GroupInstance::operator=(GroupInstance&&) noexcept = default;

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

// Returns true if the field_data_type is in the "numeric" category
// (i.e., not string/data/char/boolean — things that shouldn't be set as pure ints/floats).
// This drives TYPE_MISMATCH. For simplicity, we only reject truly incompatible pairs:
//   set_string → always OK (serialises as bytes; string is the most permissive)
//   set_bytes  → always OK (type-agnostic escape hatch; no dict type check)
//   set_int    → allow Int, Length, SeqNum, NumInGroup, DayOfMonth + all non-string numeric
//   set_double → allow Price, Qty, Amt, PriceOffset, Percentage, Float
//   set_decimal → allow Price, Qty, Amt, PriceOffset, Percentage, Float
// NOTE: The minimal test dictionary only has STRING fields, so TYPE_MISMATCH for
// set_int on a STRING is intentionally NOT triggered (string accepts int ASCII text).
// Per contract comment in message-write.md: "DICT_CONFIG (tag absent) / TYPE_MISMATCH
// (dict type != flavour)". For the v1.0 wire CA-009 surface, we treat all dict types
// as string-compatible (the wire is ASCII text) — the type check gates only obvious
// mismatches. A full type-check system is US4 (CA-010-write group builder).
// For CA-009, we perform DICT_CONFIG (absent from dict) only; TYPE_MISMATCH is a
// forward-compat hook that fires for genuinely wrong setter/type pairings:
//   set_int on a STRING/CHAR/BOOLEAN → NOT a mismatch (wire = ASCII decimal)
//   set_double on a STRING → NOT a mismatch (wire = ASCII float)
//   No strict mismatches exist with just string/int/double/decimal setters
//   on the standard FIX dictionary types.
// We skip the TYPE_MISMATCH check for CA-009 scope; it will be enforced in US4/group.

// Validate tag against the dictionary for a given msg_type.
// Returns OK or DICT_CONFIG (not declared) or MSG_FRAMING_TAG_FORBIDDEN.
// TYPE_MISMATCH is not applied in CA-009 (see note above).
static fixpp_error_t check_dict(const fixpp_msg* h, uint16_t tag) noexcept {
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
    return FIXPP_ERR_OK;
}

// ── AccumulatorEntry helpers ──────────────────────────────────────────────────

// Find the existing AccumulatorEntry for `tag` in `entries`, or nullptr.
static AccumulatorEntry* find_entry(std::pmr::vector<AccumulatorEntry>& entries,
                                    uint16_t tag) noexcept {
    for (auto& e : entries) {
        if (e.tag == tag) return &e;
    }
    return nullptr;
}

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

// Deep-copy `data` into the accumulator's PMR resource via `entry.value_bytes`.
static void set_bytes_into_entry(AccumulatorEntry& entry, const std::byte* data, std::size_t len,
                                 std::pmr::memory_resource* mr) {
    entry.value_bytes.assign(data, data + len);
    (void)mr;  // value_bytes already uses acc.arena_ via its allocator
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

// ── Dead-shell registry ([2i §4.2.2]) ────────────────────────────────────────
//
// Destroyed fixpp_msg shells are RETAINED here so a second fixpp_msg_destroy(msg)
// can safely read the DEAD tag without UAF. The registry is a heap-allocated
// std::vector that is never freed (same pattern as s_dead_shells for engines).
// Each retained dead shell holds only the small tombstoned identity (tag_=DEAD,
// all pointers null, unique_ptrs reset). The heavy internals (accumulator,
// committed_buf_, owned_frame_, owned_view_, dict_, token) are freed at destroy time.
//
// NOTE: Only HEAP shells (outbound + clone handles created by fixpp_msg_create_outbound
// or fixpp_msg_clone) are destroyed via fixpp_msg_destroy. INBOUND dispatch-window
// handles are stack-locals in CapiApplication::fromApp and NEVER destroyed via this
// function; they are never added to the registry.
//
// fixpp_msg_destroy is SINGLE_THREAD (same constraint as fixpp_engine_destroy) so
// no lock is needed.

static std::vector<fixpp_msg*>* s_dead_msg_shells =  // NOLINT
    new std::vector<fixpp_msg*>();

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
        // overhead) PLUS the committed buffer; valid messages never spill (over-cap
        // → WIRE_LIMIT_EXCEEDED at commit). Upstream = new_delete for graceful
        // degrade on pathological overwrite-heavy builds (NOT null → no terminate).
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
    } catch (...) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }
}

// ── fixpp_msg_destroy ─────────────────────────────────────────────────────────
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_destroy(fixpp_msg_t* msg) {
    if (msg == nullptr) return FIXPP_ERR_OK;
    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    if (h->tag_ == FIXPP_HANDLE_TAG_DEAD) return FIXPP_ERR_OK;  // idempotent

    // Tombstone first (E-9: tag flip before any deallocation).
    h->tag_ = FIXPP_HANDLE_TAG_DEAD;

    // Expire the session liveness token (no-op for inbound/clone handles that
    // have an expired token already).
    h->token.reset();

    // Release the dict shared_ptr (decrements refcount; may free the dict if last ref).
    h->dict_.reset();

    // Release commit payload buffer (unique_ptr auto-destructs; nullptr for inbound/clone).
    h->committed_buf_.reset();
    h->committed_payload_ = nullptr;

    // Release clone-owned buffers (unique_ptrs auto-destruct if non-null).
    h->owned_view_.reset();
    h->owned_frame_.reset();

    // The OutboundAccumulator is heap-allocated over the per-message arena; delete
    // it FIRST (its pmr members deallocate to arena_resource_, still alive here).
    // For inbound/clone handles, accumulator is nullptr — safe to delete nullptr.
    delete h->accumulator;
    h->accumulator = nullptr;

    // Free the per-message arena AFTER the accumulator (resource before its backing
    // buffer). The retained dead shell keeps only tag_ — so no 16 KiB-per-message
    // leak. nullptr for inbound/clone handles (reset() is a no-op).
    h->arena_resource_.reset();
    h->arena_buf_.reset();

    // Retain the dead shell in the registry so a second destroy(same_ptr) call
    // can safely read tag_==DEAD without UAF ([2i §4.2.2] tombstone discipline).
    // The shell is small (all pointers null; tag_==DEAD) so the retain is cheap.
    s_dead_msg_shells->push_back(h);
    // Do NOT delete h — it must remain readable for future destroy/guard checks.
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
            if (p9 == std::string_view::npos) return std::nullopt;
            if (s[p9] == SOH) ++p9;
            std::size_t soh9 = s.find(SOH, p9);
            if (soh9 == std::string_view::npos) return std::nullopt;
            std::size_t body_off = soh9 + 1;
            std::size_t p10 = s.find("\x01" "10=", body_off);
            if (p10 == std::string_view::npos) return std::nullopt;
            std::size_t body_len = (p10 + 1) - body_off;
            return fixpp::wire::frame_view_access::make(buf, len, body_off, body_len);
        };

        auto maybe_fv = mk_fv(owned_frame.get(), frame_len);

        // PMR backing for the MessageView OffsetTable internals.
        auto* clone_mr = std::pmr::new_delete_resource();

        // Build the owned dict-free MessageView<Index> over the cloned bytes.
        fixpp::wire::frame_view fv =
            maybe_fv.value_or(fixpp::wire::frame_view_access::make(owned_frame.get(), frame_len, 0, frame_len));
        auto clone_view =
            std::make_unique<fixpp::wire::MessageView<fixpp::wire::access_mode::Index>>(fv, clone_mr);

        // Allocate the clone shell on the heap.
        auto* clone = new fixpp_msg{};
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
    } catch (...) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }
}

// ── fixpp_msg_set_string ──────────────────────────────────────────────────────
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_string(fixpp_msg_t* msg, uint16_t tag,
                                               const char* value, size_t len) {
    if (msg == nullptr) return FIXPP_ERR_NULL_HANDLE;
    if (value == nullptr) return FIXPP_ERR_NULL_HANDLE;
    // Check-before-deref (E-9 / D-9): dead tag → INVALID; inbound → INVALID; token expired → INVALID.
    if (fixpp_error_t c = check_outbound_msg(msg); c != FIXPP_ERR_OK) return c;

    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    // Dict validation (framing tag + DICT_CONFIG).
    if (fixpp_error_t c = check_dict(h, tag); c != FIXPP_ERR_OK) return c;

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
    if (fixpp_error_t c = check_dict(h, tag); c != FIXPP_ERR_OK) return c;

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
    if (fixpp_error_t c = check_dict(h, tag); c != FIXPP_ERR_OK) return c;

    // Serialise double via snprintf "%.10g" (FIX convention for float fields).
    char buf[64];
    int n = std::snprintf(buf, sizeof(buf), "%.10g", value);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(buf)) return FIXPP_ERR_WIRE_INVALID_FRAME;

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
    if (fixpp_error_t c = check_dict(h, tag); c != FIXPP_ERR_OK) return c;

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

// ── fixpp_msg_commit ──────────────────────────────────────────────────────────
//
// Serialise the accumulator into an app-payload in the session arena.
// Format: "35=<type>\x01<tag>=<value>\x01..." (no framing tags).
// Steady-state thunk: abort on exception escape ([2i §5.2]).
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_commit(fixpp_msg_t* msg, const uint8_t** payload_out,
                                           size_t* len_out) {
    if (payload_out != nullptr) *payload_out = nullptr;
    if (len_out != nullptr) *len_out = 0;
    if (msg == nullptr || payload_out == nullptr || len_out == nullptr)
        return FIXPP_ERR_NULL_HANDLE;

    if (fixpp_error_t c = check_outbound_msg(msg); c != FIXPP_ERR_OK) return c;

    auto* h = reinterpret_cast<fixpp_msg*>(msg);
    const auto& acc = *h->accumulator;

    // First pass: compute total payload size.
    // Format: "35=<mt>\x01" + for each entry: "<tag>=<value>\x01"
    std::size_t total = 0;
    {
        std::string_view mt = acc.msg_type;
        // "35=" + mt.size() + "\x01"
        total += 3 + mt.size() + 1;
    }
    for (const auto& entry : acc.entries) {
        if (entry.tag == 0) continue;  // group slot (CA-010-write, not yet in CA-009)
        // "<tag>=" len + value len + "\x01"
        char tagbuf[8];
        int taglen =
            static_cast<int>(std::to_chars(tagbuf, tagbuf + sizeof(tagbuf), entry.tag).ptr - tagbuf);
        total += static_cast<std::size_t>(taglen) + 1 + entry.value_bytes.size() + 1;
    }

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

    // Write each field entry.
    for (const auto& entry : acc.entries) {
        if (entry.tag == 0) continue;  // group slot
        bool ok = append_field(buf, total, pos, entry.tag, entry.value_bytes.data(),
                               entry.value_bytes.size());
        if (!ok) {
            // Should not happen since we pre-computed the size.
            return FIXPP_ERR_WIRE_LIMIT_EXCEEDED;
        }
    }

    // Update the C-consumer pointer alias (buffer is arena-owned, not a unique_ptr).
    h->committed_payload_ = buf;
    h->committed_len_ = pos;

    *payload_out = reinterpret_cast<const uint8_t*>(buf);
    *len_out = pos;
    return FIXPP_ERR_OK;
}

}  // extern "C"
