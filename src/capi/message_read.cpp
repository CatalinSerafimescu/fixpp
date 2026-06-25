// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/capi/message_read.cpp — CA-008 (T006/US1) + CA-010-read (T013/US3)
//
// Thin thunks over wire::MessageView<Index>. All accessors:
//   - abort on any escaping C++ exception ([2i §5.2] steady-state rule)
//   - allocate nothing on the global heap (SC-003 / [const §VIII.5])
//   - return FIXPP_ERR_NULL_HANDLE for NULL msg or output pointers
//   - return FIXPP_ERR_INVALID_HANDLE for dead/tombstoned handles or wrong flavour
//
// Group cursors (fixpp_group_t) are stack-allocated in a local that lives in
// the calling function's scope; the pointer returned to C is into an arena-
// allocated copy so the consumer can keep it alive through the dispatch window.

#include "fix/c_api/message.h"
#include "fix/c_api/export.h"

#include <cassert>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string_view>
#include <system_error>

#include <fixpp/core/error.hpp>
#include <fixpp/wire/parser.hpp>  // MessageView, field_iterator, group_slice

#include "capi_internal.hpp"

namespace {

// ── Handle-validation helpers ───────────────────────────────────────────────

// Positive-tag guard for the CA-008 read path (inbound/clone handles):
//   1. NULL check → NULL_HANDLE
//   2. Positive tag check: tag_ must be exactly FIXPP_HANDLE_TAG_MSG → INVALID_HANDLE
//      for any other value (ENGINE, DICT, DEAD, or foreign handle).  Mirrors the
//      check_msg_for_field_iter guard added in 052 (gate-b/r1: F2 fix).
//   3. View pointer check: outbound flavour has view==nullptr → INVALID_HANDLE.
// Returns the inbound view pointer, or nullptr + sets *err.
const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>*
check_inbound_msg(const fixpp_msg_t* msg, fixpp_error_t* err) noexcept {
    if (msg == nullptr) {
        *err = FIXPP_ERR_NULL_HANDLE;
        return nullptr;
    }
    const auto* h = reinterpret_cast<const fixpp_msg*>(msg);
    if (h->tag_ != FIXPP_HANDLE_TAG_MSG) {
        *err = FIXPP_ERR_INVALID_HANDLE;
        return nullptr;
    }
    if (h->view == nullptr) {
        // outbound flavour or unset — no wire view to read
        *err = FIXPP_ERR_INVALID_HANDLE;
        return nullptr;
    }
    return h->view;
}

// Positive-tag guard for the CA-053 field-iteration path (FR-006 / FR-007).
// Unlike check_inbound_msg (which excludes only DEAD), this uses a POSITIVE check:
// tag_ must be exactly FIXPP_HANDLE_TAG_MSG.  Any other tag — ENGINE, DICT, DEAD,
// or a foreign handle — returns FIXPP_ERR_INVALID_HANDLE regardless of the view
// pointer.  The view-null check below still catches outbound-flavour handles that
// carry the MSG tag but have no wire view.
const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>*
check_msg_for_field_iter(const fixpp_msg_t* msg, fixpp_error_t* err) noexcept {
    if (msg == nullptr) {
        *err = FIXPP_ERR_NULL_HANDLE;
        return nullptr;
    }
    const auto* h = reinterpret_cast<const fixpp_msg*>(msg);
    if (h->tag_ != FIXPP_HANDLE_TAG_MSG) {
        *err = FIXPP_ERR_INVALID_HANDLE;
        return nullptr;
    }
    if (h->view == nullptr) {
        *err = FIXPP_ERR_INVALID_HANDLE;
        return nullptr;
    }
    return h->view;
}

// Map a wire::expected_t error to a C-ABI error code for tag lookups.
fixpp_error_t map_get_error(fixpp::core::error e) noexcept {
    switch (e) {
        case fixpp::core::error::wire_required_field_missing:
            return FIXPP_ERR_TAG_NOT_FOUND;
        default:  // LCOV_EXCL_LINE — MessageView::get() only returns wire_required_field_missing on failure; other error codes are future extensions
            return FIXPP_ERR_WIRE_INVALID_FRAME;  // LCOV_EXCL_LINE
    }
}

// Parse a string_view as int64_t (decimal). Returns false on failure.
bool parse_int64(std::string_view sv, int64_t& out) noexcept {
    if (sv.empty()) return false;
    const char* first = sv.data();
    const char* last  = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc{} && ptr == last;
}

// Parse a string_view as double. Returns false on failure.
bool parse_double(std::string_view sv, double& out) noexcept {
    if (sv.empty()) return false;
    const char* first = sv.data();
    const char* last  = sv.data() + sv.size();
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc{} && ptr == last;
#else
    // Fallback: strtod — safe here since field bytes are null-terminated
    // by the SOH that follows them (but we have a length, not a NUL).
    // Copy to a small stack buffer to NUL-terminate.
    char buf[64];
    if (sv.size() >= sizeof(buf)) return false;
    std::memcpy(buf, first, sv.size());
    buf[sv.size()] = '\0';
    char* end = nullptr;
    double v = std::strtod(buf, &end);
    if (end == buf || end != buf + sv.size()) return false;
    out = v;
    return true;
#endif
}

// ── Field scan over a raw group slice byte span ─────────────────────────────
//
// group_slice::data/len is a sub-frame of the original wire buffer: the bytes
// for one group instance starting at the delimiter tag= prefix and ending before
// the next instance's delimiter (or end of the original frame).
//
// We walk it with MessageView::field_iterator which scans tag=value<SOH> pairs.

// Find field `tag` inside the raw bytes of group instance slice `sl`.
// Returns a string_view aliasing sl.data on success.
// Returns an empty optional if not found.
std::optional<std::string_view> scan_slice_for_tag(
    const fixpp::wire::group_slice& sl, std::uint16_t tag) noexcept {
    if (sl.data == nullptr || sl.len == 0) return std::nullopt;
    auto bytes = std::span<const std::byte>{sl.data, sl.len};
    // field_iterator scans tag=value<SOH> pairs over a byte span.
    fixpp::wire::MessageView<fixpp::wire::access_mode::Iter>::field_iterator it{bytes, 0};
    fixpp::wire::MessageView<fixpp::wire::access_mode::Iter>::field_iterator end_it{bytes, bytes.size()};
    while (!(it == end_it)) {
        auto const& f = *it;
        if (f.tag == tag) {
            auto sv = std::string_view{
                reinterpret_cast<const char*>(f.value.data()),
                f.value.size()};
            return sv;
        }
        ++it;
    }
    return std::nullopt;
}

// fixpp_group concrete accessor (not declared in the header — internal helper)
const fixpp_group* as_group(const fixpp_group_t* g) noexcept {
    return reinterpret_cast<const fixpp_group*>(g);
}

// Check that entry index i is in range; return the slice or error.
const fixpp::wire::group_slice* group_entry(
    const fixpp_group_t* g, std::size_t i, fixpp_error_t* err) noexcept {
    if (g == nullptr) { *err = FIXPP_ERR_NULL_HANDLE; return nullptr; }
    const auto* grp = as_group(g);
    if (i >= grp->slices.size()) { *err = FIXPP_ERR_INDEX_OUT_OF_RANGE; return nullptr; }
    return &grp->slices[i];
}

}  // namespace

extern "C" {

// ── CA-008 implementation ────────────────────────────────────────────────────

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_string(const fixpp_msg_t* msg, uint16_t tag,
                                              const char** value_out, size_t* len_out) {
    if (value_out == nullptr || len_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t err = FIXPP_ERR_OK;
    const auto* view = check_inbound_msg(msg, &err);
    if (view == nullptr) return err;

    auto res = view->get(tag);
    if (!res) return map_get_error(res.error());

    auto sv = res->as_string();
    *value_out = sv.data();
    *len_out   = sv.size();
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_bytes(const fixpp_msg_t* msg, uint16_t tag,
                                             const uint8_t** bytes_out, size_t* len_out) {
    if (bytes_out == nullptr || len_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t err = FIXPP_ERR_OK;
    const auto* view = check_inbound_msg(msg, &err);
    if (view == nullptr) return err;

    auto res = view->get(tag);
    if (!res) return map_get_error(res.error());

    auto sp = res->bytes();
    *bytes_out = reinterpret_cast<const uint8_t*>(sp.data());
    *len_out   = sp.size();
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_int(const fixpp_msg_t* msg, uint16_t tag,
                                           int64_t* value_out) {
    if (value_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t err = FIXPP_ERR_OK;
    const auto* view = check_inbound_msg(msg, &err);
    if (view == nullptr) return err;

    auto res = view->get(tag);
    if (!res) return map_get_error(res.error());

    int64_t v = 0;
    if (!parse_int64(res->as_string(), v)) return FIXPP_ERR_WIRE_INVALID_FRAME;
    *value_out = v;
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_double(const fixpp_msg_t* msg, uint16_t tag,
                                              double* value_out) {
    if (value_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t err = FIXPP_ERR_OK;
    const auto* view = check_inbound_msg(msg, &err);
    if (view == nullptr) return err;

    auto res = view->get(tag);
    if (!res) return map_get_error(res.error());

    double v = 0.0;
    if (!parse_double(res->as_string(), v)) return FIXPP_ERR_WIRE_INVALID_FRAME;
    *value_out = v;
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_decimal(const fixpp_msg_t* msg, uint16_t tag,
                                               fixpp_decimal_t* value_out) {
    if (value_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t err = FIXPP_ERR_OK;
    const auto* view = check_inbound_msg(msg, &err);
    if (view == nullptr) return err;

    // Decimal parse needs a scratch PMR resource. Use a function-local
    // monotonic buffer on the stack (zero global-heap, SC-003 / [const §VIII.5]).
    alignas(std::max_align_t) std::byte scratch_buf[512];
    std::pmr::monotonic_buffer_resource scratch{scratch_buf, sizeof(scratch_buf),
                                                 std::pmr::null_memory_resource()};

    auto res = view->get_decimal(tag, &scratch);
    if (!res) {
        auto e = res.error();
        if (e == fixpp::core::error::wire_required_field_missing) return FIXPP_ERR_TAG_NOT_FOUND;
        if (e == fixpp::core::error::decimal_precision_loss) return FIXPP_ERR_DECIMAL_PRECISION_LOSS;
        return FIXPP_ERR_DECIMAL_INVALID;
    }

    // Copy the decimal_t to the POD out-parameter.
    // fixpp_decimal_t is the same PoD as fixpp::decimal_t ([2a §5.1] / [const §X.3]).
    static_assert(sizeof(*value_out) == sizeof(res->value()),
                  "fixpp_decimal_t layout must match fixpp::decimal_t");
    std::memcpy(value_out, &res.value(), sizeof(*value_out));
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_has_tag(const fixpp_msg_t* msg, uint16_t tag,
                                           bool* present_out) {
    if (present_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t err = FIXPP_ERR_OK;
    const auto* view = check_inbound_msg(msg, &err);
    if (view == nullptr) return err;

    *present_out = view->get(tag).has_value();
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_version(const fixpp_msg_t* msg,
                                           fixpp_resolved_msg_version_t* version_out) {
    if (version_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t err = FIXPP_ERR_OK;
    const auto* view = check_inbound_msg(msg, &err);
    if (view == nullptr) return err;

    // tag 8 = BeginString (always present in a valid FIX message)
    auto bs_res = view->get(8);
    if (!bs_res) {
        // A message without tag 8 is structurally invalid; treat as WIRE_INVALID_FRAME
        // but still fill defaults so the output is well-defined.
        version_out->begin_string     = nullptr;
        version_out->begin_string_len = 0;
        version_out->appl_ver_id      = nullptr;
        version_out->appl_ver_id_len  = 0;
        return FIXPP_ERR_TAG_NOT_FOUND;
    }
    auto bs_sv = bs_res->as_string();
    version_out->begin_string     = bs_sv.data();
    version_out->begin_string_len = bs_sv.size();

    // tag 1137 = DefaultApplVerID (optional; present only for FIXT sessions)
    auto av_res = view->get(1137);
    if (av_res) {
        auto av_sv = av_res->as_string();
        version_out->appl_ver_id     = av_sv.data();
        version_out->appl_ver_id_len = av_sv.size();
    } else {
        version_out->appl_ver_id     = nullptr;
        version_out->appl_ver_id_len = 0;
    }
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_msg_type(const fixpp_msg_t* msg,
                                                const char** value_out, size_t* len_out) {
    if (value_out == nullptr || len_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t err = FIXPP_ERR_OK;
    const auto* view = check_inbound_msg(msg, &err);
    if (view == nullptr) return err;

    auto sv = view->msg_type();
    if (sv.empty()) return FIXPP_ERR_TAG_NOT_FOUND;
    *value_out = sv.data();
    *len_out   = sv.size();
    return FIXPP_ERR_OK;
}

// ── CA-010-read implementation ───────────────────────────────────────────────

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_group(const fixpp_msg_t* msg, uint16_t group_tag,
                                             const fixpp_group_t** group_out,
                                             size_t* count_out) {
    if (group_out == nullptr || count_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    *group_out = nullptr;
    *count_out = 0;

    fixpp_error_t err = FIXPP_ERR_OK;
    const auto* view = check_inbound_msg(msg, &err);
    if (view == nullptr) return err;

    const auto& offsets = view->offsets();

    // Check if the tag is present at all in the message (as any field).
    auto found = offsets.find(group_tag);
    if (!found) {
        // Tag entirely absent → TAG_NOT_FOUND (E-2)
        return FIXPP_ERR_TAG_NOT_FOUND;
    }

    // Obtain group slices. If group_slices returns empty AND the tag was found,
    // the tag is present as a scalar (not a NoXxx group delimiter) → TYPE_MISMATCH.
    auto slices = offsets.group_slices(group_tag);
    if (slices.empty()) {
        // Tag found in offsets but no group slices → not a group tag → TYPE_MISMATCH
        return FIXPP_ERR_TYPE_MISMATCH;
    }

    // Allocate the fixpp_group cursor shell from the parse arena backing the
    // OffsetTable (the same per-message PMR resource that owns the group_slices
    // the cursor references).  The arena has dispatch-window lifetime and is
    // reclaimed wholesale when it resets or destructs, so the cursor is
    // automatically reclaimed — zero global-heap (FR-002 / SC-003) and no
    // explicit free needed.  [D5 deviation: OffsetTable::resource() exposes the
    // backing mr; deviation is recorded in .specify/2b-wire.md.]
    auto* arena = offsets.resource();
    auto* grp = std::pmr::polymorphic_allocator<fixpp_group>(arena).new_object<fixpp_group>();
    grp->slices      = slices;
    grp->parent_view = view;
    grp->arena       = nullptr;  // not needed for scalar reads

    *group_out = reinterpret_cast<const fixpp_group_t*>(grp);
    *count_out = slices.size();
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_group_get_field_string(const fixpp_group_t* g, size_t i,
                                                      uint16_t tag, const char** v_out,
                                                      size_t* len_out) {
    if (g == nullptr || v_out == nullptr || len_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t idx_err = FIXPP_ERR_OK;
    const auto* sl = group_entry(g, i, &idx_err);
    if (sl == nullptr) return idx_err;

    auto sv = scan_slice_for_tag(*sl, tag);
    if (!sv) return FIXPP_ERR_TAG_NOT_FOUND;
    *v_out   = sv->data();
    *len_out = sv->size();
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_group_get_field_int(const fixpp_group_t* g, size_t i,
                                                   uint16_t tag, int64_t* v_out) {
    if (g == nullptr || v_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t idx_err = FIXPP_ERR_OK;
    const auto* sl = group_entry(g, i, &idx_err);
    if (sl == nullptr) return idx_err;

    auto sv = scan_slice_for_tag(*sl, tag);
    if (!sv) return FIXPP_ERR_TAG_NOT_FOUND;
    int64_t v = 0;
    if (!parse_int64(*sv, v)) return FIXPP_ERR_WIRE_INVALID_FRAME;
    *v_out = v;
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_group_get_field_double(const fixpp_group_t* g, size_t i,
                                                      uint16_t tag, double* v_out) {
    if (g == nullptr || v_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t idx_err = FIXPP_ERR_OK;
    const auto* sl = group_entry(g, i, &idx_err);
    if (sl == nullptr) return idx_err;

    auto sv = scan_slice_for_tag(*sl, tag);
    if (!sv) return FIXPP_ERR_TAG_NOT_FOUND;
    double v = 0.0;
    if (!parse_double(*sv, v)) return FIXPP_ERR_WIRE_INVALID_FRAME;
    *v_out = v;
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_group_get_field_decimal(const fixpp_group_t* g, size_t i,
                                                       uint16_t tag, fixpp_decimal_t* v_out) {
    if (g == nullptr || v_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t idx_err = FIXPP_ERR_OK;
    const auto* sl = group_entry(g, i, &idx_err);
    if (sl == nullptr) return idx_err;

    auto sv = scan_slice_for_tag(*sl, tag);
    if (!sv) return FIXPP_ERR_TAG_NOT_FOUND;

    // Parse decimal from the raw string bytes using the 2a trait.
    auto byte_span = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(sv->data()), sv->size()};

    alignas(std::max_align_t) std::byte scratch_buf[512];
    std::pmr::monotonic_buffer_resource scratch{scratch_buf, sizeof(scratch_buf),
                                                 std::pmr::null_memory_resource()};

    auto res = fixpp::core::detail::trap_throw(
        [&byte_span, &scratch]() { return fixpp::decimal_t::parse(byte_span, &scratch); });
    if (!res) {  // LCOV_EXCL_LINE — trap_throw outer failure fires only on OOM (exception from parse); unreachable in unit tests
        auto e = res.error();  // LCOV_EXCL_LINE
        if (e == fixpp::core::error::decimal_precision_loss) return FIXPP_ERR_DECIMAL_PRECISION_LOSS;  // LCOV_EXCL_LINE
        return FIXPP_ERR_DECIMAL_INVALID;  // LCOV_EXCL_LINE
    }  // LCOV_EXCL_LINE
    if (!(*res)) {
        auto e = (*res).error();
        if (e == fixpp::core::error::decimal_precision_loss) return FIXPP_ERR_DECIMAL_PRECISION_LOSS;
        return FIXPP_ERR_DECIMAL_INVALID;
    }

    std::memcpy(v_out, &(*res).value(), sizeof(*v_out));
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_group_get_nested_group(const fixpp_group_t* g, size_t i,
                                                      uint16_t nested_tag,
                                                      const fixpp_group_t** nested_out,
                                                      size_t* nested_count_out) {
    if (g == nullptr || nested_out == nullptr || nested_count_out == nullptr)
        return FIXPP_ERR_NULL_HANDLE;
    *nested_out = nullptr;
    *nested_count_out = 0;

    fixpp_error_t idx_err = FIXPP_ERR_OK;
    const auto* sl = group_entry(g, i, &idx_err);
    if (sl == nullptr) return idx_err;

    // Scan the INSTANCE SLICE for nested group instances.
    // OffsetTable::group_slices(nested_tag) returns only first-occurrence instances
    // (first-occurrence-indexed), so for two outer entries each with a nested group,
    // entry 1's nested instances would be absent.  Correct fix: scan *sl directly.
    //
    // Algorithm: walk the instance bytes with field_iterator; find nested_tag (count),
    // then identify the delimiter (first tag after nested_tag) and collect one slice
    // per delimiter occurrence.  All storage from the parent arena (zero global-heap).
    const auto* parent_grp = as_group(g);
    auto* nested_arena = parent_grp->parent_view->offsets().resource();

    using FI = fixpp::wire::MessageView<fixpp::wire::access_mode::Iter>::field_iterator;
    auto bytes = std::span<const std::byte>{sl->data, sl->len};

    // Phase 1: locate nested_tag count field and the delimiter that follows it.
    std::uint16_t delim_tag = 0;
    const std::byte* post_count_ptr = nullptr;  // start of delimiter field bytes
    {
        FI it{bytes, 0};
        FI end_it{bytes, bytes.size()};
        bool found_count = false;
        while (!(it == end_it)) {
            const auto& f = *it;
            if (f.tag == nested_tag) {
                found_count = true;
                ++it;
                if (!(it == end_it)) {
                    delim_tag = (*it).tag;
                    // The delimiter field's tag digits start at the beginning of the
                    // buffer span used by this iterator position.  Compute the byte
                    // pointer to the tag= prefix: value.data() - tag_digits - '=' (1).
                    // Simpler: the value span aliases sl->data; we know the value
                    // starts at value.data() inside sl->data..sl->data+sl->len.
                    // Walk back from value.data() past '=' and tag digits to find tag=.
                    const std::byte* vp = (*it).value.data();
                    // Back up over '=': the byte immediately before value is '='.
                    const std::byte* eq = vp - 1;
                    // Back up over tag digits (1..5 digits for tags 1..65535).
                    const std::byte* tp = eq;
                    while (tp > sl->data && static_cast<unsigned char>(*(tp - 1)) >= '0' &&
                           static_cast<unsigned char>(*(tp - 1)) <= '9') {
                        --tp;
                    }
                    post_count_ptr = tp;
                }
                break;
            }
            ++it;
        }
        if (!found_count) return FIXPP_ERR_TAG_NOT_FOUND;
        if (delim_tag == 0 || post_count_ptr == nullptr) {
            // nested_tag present but no instances follow (count field is last) — empty group.
            return FIXPP_ERR_OK;
        }
    }

    // Phase 2: scan from post_count_ptr to end of *sl, collecting instance slices.
    // Each new occurrence of delim_tag starts a new instance; previous instance ends
    // just before the new delimiter's tag= prefix.
    // Collect into a small stack vector; then copy to arena.
    constexpr std::size_t kMaxNested = 256;
    fixpp::wire::group_slice stack_slices[kMaxNested];
    std::size_t slice_count = 0;

    {
        auto sub_bytes = std::span<const std::byte>{
            post_count_ptr,
            static_cast<std::size_t>(sl->data + sl->len - post_count_ptr)};
        FI it{sub_bytes, 0};
        FI end_it{sub_bytes, sub_bytes.size()};

        const std::byte* inst_start = post_count_ptr;
        bool in_instance = false;

        while (!(it == end_it)) {
            const auto& f = *it;
            if (f.tag == delim_tag) {
                if (in_instance) {
                    // Close previous instance: ends just before this delimiter's tag=.
                    // LCOV_EXCL_START — structurally unreachable via the public API:
                    // OffsetTable::group() tracks `seen_in_instance` for every member tag;
                    // when any member tag (including the nested delimiter) repeats within the
                    // same outer entry, it closes the outer entry boundary — so an outer
                    // entry slice produced by `group_slices()` can never contain ≥2
                    // occurrences of the nested-group delimiter tag.  Verified empirically:
                    // both "flat" dict (524/525 as members of 453) and "proper" dict
                    // (524/525 only under 539) return nc=0 on a 539=2 wire — the outer
                    // slice is truncated before the second 524 in both cases.
                    const std::byte* vp = f.value.data();
                    const std::byte* eq = vp - 1;
                    const std::byte* tp = eq;
                    while (tp > sub_bytes.data() &&
                           static_cast<unsigned char>(*(tp - 1)) >= '0' &&
                           static_cast<unsigned char>(*(tp - 1)) <= '9') {
                        --tp;
                    }
                    // tp now points to the tag= prefix of the current delimiter.
                    // Add the slice for the PREVIOUS instance. Fail CLOSED on a
                    // pathologically large nested group (kMaxNested = DoS bound) —
                    // never a silent truncation of nested_count_out.
                    if (slice_count >= kMaxNested) return FIXPP_ERR_WIRE_LIMIT_EXCEEDED;  // LCOV_EXCL_LINE — DoS bound: requires ≥256 nested group instances in a single outer entry
                    std::size_t inst_len = static_cast<std::size_t>(tp - inst_start);  // LCOV_EXCL_LINE
                    stack_slices[slice_count++] = {inst_start, inst_len};  // LCOV_EXCL_LINE
                    inst_start = tp;  // LCOV_EXCL_LINE — new instance starts at this delimiter
                } else {
                    in_instance = true;
                    // inst_start already set to post_count_ptr (= first delimiter).
                }
            }
            ++it;
        }
        // Close the last instance (ends at end of sub_bytes).
        if (in_instance) {
            std::size_t inst_len = static_cast<std::size_t>(sub_bytes.data() + sub_bytes.size() - inst_start);
            if (slice_count >= kMaxNested) return FIXPP_ERR_WIRE_LIMIT_EXCEEDED;  // LCOV_EXCL_LINE — fail-closed (no silent truncation; DoS bound: requires ≥256 instances to reach close-last path)
            stack_slices[slice_count++] = {inst_start, inst_len};
        }
    }

    if (slice_count == 0) return FIXPP_ERR_TAG_NOT_FOUND;

    // Copy slices to arena (zero global-heap; reclaimed with the dispatch arena).
    auto* arena_slices = static_cast<fixpp::wire::group_slice*>(
        nested_arena->allocate(slice_count * sizeof(fixpp::wire::group_slice),
                               alignof(fixpp::wire::group_slice)));
    std::memcpy(arena_slices, stack_slices, slice_count * sizeof(fixpp::wire::group_slice));

    auto* nested_grp = std::pmr::polymorphic_allocator<fixpp_group>(nested_arena).new_object<fixpp_group>();
    nested_grp->slices      = {arena_slices, slice_count};
    nested_grp->parent_view = parent_grp->parent_view;
    nested_grp->arena       = nullptr;

    *nested_out       = reinterpret_cast<const fixpp_group_t*>(nested_grp);
    *nested_count_out = slice_count;
    return FIXPP_ERR_OK;
}

// ── CA-053 implementation (US3 field iteration) ──────────────────────────────

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_field_count(const fixpp_msg_t* msg,
                                                size_t* count_out) {
    if (count_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    *count_out = 0;
    fixpp_error_t err = FIXPP_ERR_OK;
    const auto* view = check_msg_for_field_iter(msg, &err);
    if (view == nullptr) return err;
    *count_out = view->offsets().entries().size();
    return FIXPP_ERR_OK;
}

FIXPP_API_EXPORT fixpp_error_t fixpp_msg_field_at(const fixpp_msg_t* msg, size_t index,
                                             fixpp_msg_field_t* field_out) {
    if (field_out == nullptr) return FIXPP_ERR_NULL_HANDLE;
    fixpp_error_t err = FIXPP_ERR_OK;
    const auto* view = check_msg_for_field_iter(msg, &err);
    if (view == nullptr) return err;

    const auto& entries = view->offsets().entries();
    if (index >= entries.size()) return FIXPP_ERR_INDEX_OUT_OF_RANGE;

    const auto& e = entries[index];
    // wire_base points to the start of the full FIX frame; e.offset is the
    // byte offset from frame start to the value (after '=', before SOH).
    const auto* wire_base = reinterpret_cast<const uint8_t*>(view->bytes().data());
    field_out->tag   = e.tag;
    field_out->value = wire_base + e.offset;
    field_out->len   = e.length;
    return FIXPP_ERR_OK;
}

}  // extern "C"
