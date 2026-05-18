#pragma once
// include/fixpp/core/error.hpp
// Engine-wide error enum for fixpp::core. Owned by 2k; this feature (001-core-decimal)
// contributes the four decimal variants per data-model.md Entity 5.
// 002-dictionary-xml-loader contributes the three dict_* variants per its
// research.md D-10. Variant numbering is additive (no renumbering of existing
// slots) per `[const §X.4]` forwards-compat.

#include <cstdint>
#include <expected>

namespace fixpp::core {

enum class error : std::uint8_t {
    // slot 0 reserved for ok (never stored in unexpected)
    out_of_memory = 1,

    // decimal variants — owned by 001-core-decimal, contributes to 2k
    decimal_invalid_input = 10,
    decimal_overflow = 11,
    decimal_precision_loss = 12,
    decimal_buffer_too_small = 13,

    // dict variants — owned by 002-dictionary-xml-loader (research.md D-10);
    // additive at unused slots per `[const §X.4]`. Each exception type in
    // `<fixpp/dict/error.hpp>` carries the matching variant via `code()`.
    dict_xml_parse_failed = 20,
    dict_unknown_version = 21,
    dict_xml_oom = 22,

    // dict codegen / reify variants — owned by 003-dictionary-codegen
    // (research.md D-10/D-21; data-model "Error mapping"; spec AC-VP6).
    // Additive at unused slots 23..28 per `[const §X.4]`; non-renumbering;
    // existing slots above preserved verbatim. The 2b/wire "field absent"
    // error from `MessageView::get<1128>()` is 2b-owned, NOT a slot here
    // (cross-feature note, contracts/version_profile.hpp / spec A6).
    dict_reify_msg_type_mismatch = 23,
    dict_reify_unknown_msg_type = 24,
    dict_reify_oom = 25,
    dict_unresolved_application_version = 26,
    dict_unknown_appl_ver_id = 27,
    dict_no_dictionary_for_application_version = 28,
    // R6 placeholder: owning_<Msg>::from_view wired to accept view but the frozen
    // wire stub carries no frame bytes (2b swaps in the real body). This distinct
    // error code means tests cannot go green for the wrong reason: a positive
    // oracle must assert exactly this code until 2b lands (gate-b/r1 RC#1).
    dict_reify_wire_body_not_ready = 29,  // cutover-obsolete once 2b lands: the
    // real OffsetTable-backed MessageView carries frame bytes, so
    // owning_<Msg>::from_view no longer needs this "no body yet" sentinel.
    // Slot KEPT (non-renumbering, `[const §X.4]`); annotated, not removed.

    // wire variants — owned by 004-wire-codec (data-model "Error mapping";
    // contracts/wire_errors.hpp; `[2b §6.7]`). Appended at unused slots 30..42,
    // non-renumbering (`[const §X.4]`). v0.1's wire_tag_count_exceeded was
    // dropped (distinct-tag cap removed, RC#1) and is NOT reintroduced. The
    // 2i C-ABI FIXPP_ERR_WIRE_* coalescing + tools/abi_history audit-trail
    // entry are deferred to 2i under the same time-bounded waiver shape as
    // 002/003 (no C-ABI surface added here — research D-13; this is C++
    // core::error, not the C-ABI fixpp_error_t).
    wire_frame_too_large = 30,           // [2b §6.1.3]
    wire_invalid_body_length = 31,       // [2b §6.1.3]
    wire_checksum_mismatch = 32,         // [2b §6.1.5]
    wire_framing_resync = 33,            // [2b §6.1.2]
    wire_invalid_field_format = 34,      // [2b §6.2]
    wire_offset_table_full = 35,         // [2b §1.2/§4.4]
    wire_group_too_large = 36,           // [2b §1.2/§4.4]
    wire_tag_out_of_range = 37,          // [2b §1.2]
    wire_required_field_missing = 38,    // [2b §6.5.4]
    wire_header_out_of_order = 39,       // [2b §6.5.1]
    wire_field_value_out_of_range = 40,  // [2b §6.5.3]
    wire_field_value_truncated = 41,     // [2b §6.5 rule 3]/[2b §6.7]: the
    // validator's §6.5-rule-3 type-check site RE-MAPS 2a/001's
    // decimal_precision_loss (=12) onto this wire-domain slot so the
    // Session-Reject path carries a wire_* code (re-mapped, not redefined).
    wire_unexpected_tag = 42,  // [2b §6.5.5] SessionRejectReason=2

    // sync variants — owned by 006-async-mutex (data-model.md Error mapping;
    // contracts/sync_errors.hpp; [2f §6.5]). Appended at unused slots 43–46,
    // non-renumbering ([const §X.4]). C-ABI FIXPP_ERR_SYNC_* coalescing +
    // tools/abi_history audit-trail entry deferred to 2i (same time-bounded
    // waiver shape as 002/003/004; no C-ABI surface added here).
    sync_lock_aborted         = 43,  // [2f §4.5] — cancellation won the
                                     //   CAS-arbitration race against the drain;
                                     //   waiter was not granted ownership.
                                     //   Joins FIXPP_ERR_CANCELLED at the C ABI.
    sync_lock_alloc_failed    = 44,  // [2f §4.3] — PMR fallback's allocate()
                                     //   threw std::bad_alloc (mr exhausted) or
                                     //   the embedded inline 32-byte slot_storage_
                                     //   buffer overflowed and null_memory_resource
                                     //   rejected the allocation (trap_throw per
                                     //   [2a §4.2]).
    sync_lock_outside_session = 45,  // [2f §4.3.2] — async_lock_via_session_
                                     //   executor called outside a session
                                     //   serialisation domain (bound executor is
                                     //   not a session_executor value).
    sync_lock_drained         = 46,  // [2f §4.7.2] NEW v1.1 / RC-B —
                                     //   cancel_and_drain() set draining_ = true;
                                     //   subsequent async_lock(...) fast-fails
                                     //   without enqueuing.
};

template <class T>
using expected_t = std::expected<T, error>;

}  // namespace fixpp::core
